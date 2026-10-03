#include "Physics/FTODestruction.h"
#include "Core/FTOJuice.h"
#include "Art/FTOArt.h"
#include "Audio/FTOAudio.h"
#include "City/FTOCityGenerator.h"
#include "City/FTOInteriorLife.h"
#include "City/FTOLift.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Physics/FTODebris.h"
#include "Physics/FTOKnockdownComponent.h"
#include "Physics/FTOVehicleDamage.h"
#include "FTO.h"

namespace
{
	/** A break older than this (a late joiner catching up) just leaves the gap, no fireworks. */
	constexpr float FreshSeconds = 3.f;
	/** A collapse older than this is just a heap of rubble. */
	constexpr float FreshCollapseSeconds = 5.f;
	/** How fast a building's pieces fall when it comes down (a touch under gravity: there's a lot in the way). */
	constexpr float CollapseGravity = 980.f;

	struct FBreakable
	{
		const TCHAR* Mesh;
		EFTOBreakKind Kind;
		/** How fast a car has to be going to break it (cm/s). */
		float Speed;
		/** Rounds it takes (0: bullets won't do it). */
		int32 Rounds;
		/** How heavy it is flying (kg), and what its bits look like. */
		float Mass;
		FLinearColor Bits;
	};

	const FLinearColor Wood(0.5f, 0.32f, 0.16f);
	const FLinearColor Concrete(0.62f, 0.61f, 0.58f);
	const FLinearColor Metal(0.3f, 0.32f, 0.35f);
	const FLinearColor Leaves(0.25f, 0.55f, 0.2f);
	const FLinearColor OwnPaint(1.f, 1.f, 1.f, 0.f); // alpha 0: use the instance's own paint
	const FLinearColor BuildingDust(0.47f, 0.44f, 0.40f);
	const FLinearColor SmokeColor(0.09f, 0.085f, 0.08f);
	const FLinearColor FireColor(1.f, 0.45f, 0.08f);

	/** The outdoor city's breakables (glass is anything *_Glass; everything indoors stays put). */
	const FBreakable Breakables[] =
	{
		{ TEXT("SM_Hydrant"),      EFTOBreakKind::Burst,    350.f,  2, 60.f,  OwnPaint },
		{ TEXT("SM_Bin"),          EFTOBreakKind::KnockOff, 300.f,  2, 20.f,  OwnPaint },
		{ TEXT("SM_NewsBox"),      EFTOBreakKind::KnockOff, 300.f,  2, 30.f,  OwnPaint },
		{ TEXT("SM_Mailbox"),      EFTOBreakKind::KnockOff, 350.f,  3, 40.f,  OwnPaint },
		{ TEXT("SM_HouseMailbox"), EFTOBreakKind::KnockOff, 300.f,  2, 10.f,  Wood },
		{ TEXT("SM_ParkingMeter"), EFTOBreakKind::KnockOff, 300.f,  2, 25.f,  Metal },
		{ TEXT("SM_Planter"),      EFTOBreakKind::Smash,    500.f,  6, 0.f,   Concrete },
		{ TEXT("SM_StreetBench"),  EFTOBreakKind::Smash,    450.f,  6, 0.f,   Wood },
		{ TEXT("SM_Fence"),        EFTOBreakKind::Smash,    350.f,  4, 0.f,   OwnPaint },
		{ TEXT("SM_Bush"),         EFTOBreakKind::Smash,    300.f,  0, 0.f,   Leaves },
		{ TEXT("SM_BusStop"),      EFTOBreakKind::Smash,    800.f,  0, 0.f,   Metal },
		{ TEXT("SM_LampPost"),     EFTOBreakKind::Topple,   900.f,  0, 150.f, Metal },
		{ TEXT("SM_TrafficLight"), EFTOBreakKind::Topple,   1000.f, 0, 200.f, Metal },
		{ TEXT("SM_Tree_Round"),   EFTOBreakKind::Topple,   1300.f, 0, 400.f, Leaves },
		{ TEXT("SM_Tree_Pine"),    EFTOBreakKind::Topple,   1300.f, 0, 400.f, Leaves },
		{ TEXT("SM_Tree_Tall"),    EFTOBreakKind::Topple,   1300.f, 0, 400.f, Leaves },
	};

	const FBreakable* FindBreakable(const UPrimitiveComponent* Component)
	{
		const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Component);
		const UStaticMesh* Mesh = ISM ? ISM->GetStaticMesh() : nullptr;
		if (!Mesh || !ISM->GetOwner() || !ISM->GetOwner()->IsA<AFTOCityGenerator>() || ISM->GetName().EndsWith(TEXT("_In")))
		{
			return nullptr;
		}
		const FString Name = Mesh->GetName();
		for (const FBreakable& Entry : Breakables)
		{
			if (Name == Entry.Mesh)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	bool IsCityGlass(const UPrimitiveComponent* Component)
	{
		const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Component);
		return ISM && ISM->GetStaticMesh() && ISM->GetOwner() && ISM->GetOwner()->IsA<AFTOCityGenerator>() &&
			ISM->GetStaticMesh()->GetName().EndsWith(TEXT("_Glass"));
	}

	/** What the city makes of the police breaking it. */
	float ChaosFor(EFTOBreakKind Kind)
	{
		switch (Kind)
		{
		case EFTOBreakKind::Shatter:  return 0.4f;
		case EFTOBreakKind::KnockOff: return 0.5f;
		case EFTOBreakKind::Burst:    return 1.f;
		case EFTOBreakKind::Smash:    return 0.8f;
		case EFTOBreakKind::Topple:   return 1.5f;
		case EFTOBreakKind::Crumble:  return 1.f;
		default:                      return 0.f;
		}
	}

	/** Facing out of each face of a building (AFTOCityGenerator's EFace: +X, -X, +Y, -Y). */
	FVector FaceNormal(int32 Face)
	{
		switch (Face)
		{
		case 0:  return FVector(1.f, 0.f, 0.f);
		case 1:  return FVector(-1.f, 0.f, 0.f);
		case 2:  return FVector(0.f, 1.f, 0.f);
		default: return FVector(0.f, -1.f, 0.f);
		}
	}

	/** Where storey Level's floor is, above the building's street level. */
	float LevelZ(int32 Level)
	{
		return Level <= 0 ? 0.f : FTOKit::GroundHeight + (Level - 1) * FTOKit::UpperHeight;
	}

	/** How tall a wall panel on Level is. */
	float PanelHeight(int32 Level)
	{
		return Level <= 0 ? FTOKit::GroundHeight : FTOKit::UpperHeight;
	}

	FLinearColor InstanceColor(const UInstancedStaticMeshComponent* ISM, int32 Instance)
	{
		const int32 First = Instance * ISM->NumCustomDataFloats;
		if (ISM->NumCustomDataFloats >= 3 && ISM->PerInstanceSMCustomData.IsValidIndex(First + 2))
		{
			return FLinearColor(ISM->PerInstanceSMCustomData[First], ISM->PerInstanceSMCustomData[First + 1], ISM->PerInstanceSMCustomData[First + 2]);
		}
		return FLinearColor::White;
	}

	bool IsPolice(const AController* ByWhom)
	{
		return Cast<APlayerController>(ByWhom) != nullptr;
	}
}

void FFTOBrokenPiece::PostReplicatedAdd(const FFTOBrokenList& List)
{
	if (List.Owner)
	{
		List.Owner->Apply(*this);
	}
}

void FFTOWallHit::PostReplicatedAdd(const FFTOWallHitList& List)
{
	if (List.Owner)
	{
		List.Owner->ShowWallHit(*this);
	}
}

void FFTOWallHit::PostReplicatedChange(const FFTOWallHitList& List)
{
	if (List.Owner)
	{
		List.Owner->ShowWallHit(*this);
	}
}

AFTODestruction::AFTODestruction()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Broken.Owner = this;
	WallHits.Owner = this;
}

void AFTODestruction::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTODestruction, Broken);
	DOREPLIFETIME(AFTODestruction, WallHits);
	DOREPLIFETIME(AFTODestruction, Collapses);
}

void AFTODestruction::BeginPlay()
{
	Super::BeginPlay();
	Broken.Owner = this;
	WallHits.Owner = this;
}

AFTODestruction* AFTODestruction::Get(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AFTODestruction> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

AFTOCityGenerator* AFTODestruction::FindCity()
{
	if (!City)
	{
		for (TActorIterator<AFTOCityGenerator> It(GetWorld()); It; ++It)
		{
			City = *It;
			break;
		}
	}
	return City;
}

EFTOBreakKind AFTODestruction::KindOf(const UPrimitiveComponent* Component)
{
	if (IsCityGlass(Component))
	{
		return EFTOBreakKind::Shatter;
	}
	const FBreakable* Entry = FindBreakable(Component);
	return Entry ? Entry->Kind : EFTOBreakKind::None;
}

float AFTODestruction::BreakSpeed(const UPrimitiveComponent* Component)
{
	const FBreakable* Entry = FindBreakable(Component);
	return Entry ? Entry->Speed : TNumericLimits<float>::Max();
}

bool AFTODestruction::IsLoose(const UPrimitiveComponent* Component)
{
	const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Component);
	return ISM && ISM->GetStaticMesh() && ISM->GetStaticMesh()->GetBounds().BoxExtent.GetMax() < 130.f;
}

bool AFTODestruction::IsBroken(FName Component, int32 Instance) const
{
	const TPair<FName, int32> Key(Component, Instance);
	return BrokenKeys.Contains(Key) || Applied.Contains(Key);
}

void AFTODestruction::AddBroken(FName Component, int32 Instance, const FVector& Hit, const FVector& Push, uint8 How)
{
	const TPair<FName, int32> Key(Component, Instance);
	BrokenKeys.Add(Key);
	Hits.Remove(Key);
	FFTOBrokenPiece& Piece = Broken.Items.AddDefaulted_GetRef();
	Piece.Component = Component;
	Piece.Instance = Instance;
	Piece.Hit = Hit;
	Piece.Push = Push;
	Piece.Time = GetWorld()->GetTimeSeconds();
	Piece.How = How;
	Broken.MarkItemDirty(Piece);
	const FFTOBrokenPiece Copy = Piece;
	Apply(Copy);
}

bool AFTODestruction::Break(UPrimitiveComponent* Component, int32 Instance, const FVector& Hit, const FVector& Push, AController* ByWhom)
{
	check(HasAuthority());
	EnsureIndexed();
	const EFTOBreakKind Kind = KindOf(Component);
	const TPair<FName, int32> Key(Component ? Component->GetFName() : NAME_None, Instance);
	if (!Component || Instance == INDEX_NONE || BrokenKeys.Contains(Key))
	{
		return false;
	}
	if (Kind == EFTOBreakKind::None)
	{
		// A building's wall: the whole panel goes (and whatever it was holding up).
		int32 S = INDEX_NONE;
		int32 P = INDEX_NONE;
		if (!FindPiece(Key.Key, Instance, S, P))
		{
			return false;
		}
		if (GetStructure(S)->Pieces[P].Role == EFTOPieceRole::Inside)
		{
			if (!IsLoose(Component))
			{
				return false;
			}
			// The furniture, sent flying (a car through the shop).
			AddBroken(Key.Key, Instance, Hit, Push, FTOBreakHow::Hit);
			if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
			{
				GS->MulticastPlaySound(AFTOGameState::Sounds().Clang, Hit, 0.8f);
				if (IsPolice(ByWhom))
				{
					GS->AddChaos(ChaosFor(EFTOBreakKind::KnockOff));
				}
			}
			return true;
		}
		if (GetStructure(S)->Pieces[P].Role != EFTOPieceRole::Wall)
		{
			return false;
		}
		const FFTOStructurePiece& Piece = GetStructure(S)->Pieces[P];
		if (Piece.Face < 0 || Piece.Level >= Cells[S].Levels)
		{
			AddBroken(Key.Key, Instance, Hit, Push, FTOBreakHow::Hit);
			return true;
		}
		CrumbleCell(S, Piece.Level * Cells[S].Ring + GetStructure(S)->RingOf(Piece.Face, Piece.Column), Hit, Push, ByWhom);
		return true;
	}
	AddBroken(Key.Key, Instance, Hit, Push, FTOBreakHow::Hit);

	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastPlaySound(Kind == EFTOBreakKind::Shatter ? AFTOGameState::Sounds().Glass : AFTOGameState::Sounds().Clang, Hit, 1.f);
		// The police did that? The city won't forget.
		if (IsPolice(ByWhom))
		{
			GS->AddChaos(ChaosFor(Kind));
			++GS->PropertyBroken;
		}
	}
	UE_LOG(LogFTO, Verbose, TEXT("Broke %s #%d."), *Key.Key.ToString(), Instance);
	return true;
}

void AFTODestruction::RoundHit(UPrimitiveComponent* Component, int32 Instance, const FVector& Hit, const FVector& Velocity, EFTOWeapon Weapon, AController* ByWhom)
{
	check(HasAuthority());
	const FFTOWeaponSpec& Spec = FTOWeapons::Spec(Weapon);
	if (Spec.bStun || !Component || Instance == INDEX_NONE)
	{
		return;
	}
	// A building's wall wears down a little with every round (a rifle's more than a pistol's).
	EFTOPieceRole PieceRole;
	if (GetStructurePiece(Component, Instance, PieceRole) && PieceRole == EFTOPieceRole::Wall)
	{
		DamageWall(Component, Instance, Spec.CarDamage * 0.7f, Hit, -Velocity.GetSafeNormal(), Velocity.GetClampedToMaxSize(500.f), ByWhom);
		return;
	}
	const EFTOBreakKind Kind = KindOf(Component);
	if (Kind == EFTOBreakKind::None)
	{
		return;
	}
	// Glass goes at once; street furniture soaks up a few rounds first (big things never give).
	const FBreakable* Entry = FindBreakable(Component);
	const int32 Needed = Kind == EFTOBreakKind::Shatter ? 1 : (Entry ? Entry->Rounds : 0);
	if (Needed <= 0)
	{
		return;
	}
	int32& Count = Hits.FindOrAdd(TPair<FName, int32>(Component->GetFName(), Instance));
	if (++Count >= Needed)
	{
		Break(Component, Instance, Hit, Velocity.GetClampedToMaxSize(700.f), ByWhom);
	}
}

void AFTODestruction::BreakLocally(UPrimitiveComponent* Component, int32 Instance, const FVector& Hit, const FVector& Push)
{
	EFTOPieceRole PieceRole;
	if (!Component || Instance == INDEX_NONE || (KindOf(Component) == EFTOBreakKind::None && !(GetStructurePiece(Component, Instance, PieceRole) &&
		(PieceRole == EFTOPieceRole::Wall || PieceRole == EFTOPieceRole::Inside))))
	{
		return;
	}
	FFTOBrokenPiece Piece;
	Piece.Component = Component->GetFName();
	Piece.Instance = Instance;
	Piece.Hit = Hit;
	Piece.Push = Push;
	const AGameStateBase* GS = GetWorld()->GetGameState();
	Piece.Time = GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const TPair<FName, int32> Key(Piece.Component, Instance);
	const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Component);
	FTransform Was;
	if (HasAuthority() || Applied.Contains(Key) || !ISM || !ISM->GetInstanceTransform(Instance, Was, true))
	{
		return;
	}
	Apply(Piece);
	// Till the server agrees (it may not, if it saw things differently): put back if it hasn't before long.
	Predicted.Add(Key, TPair<FTransform, float>(Was, GetWorld()->GetTimeSeconds()));
}

bool AFTODestruction::IsPieceNear(const UPrimitiveComponent* Component, int32 Instance, const FVector& Point, float Radius) const
{
	const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Component);
	FTransform Where;
	if (!ISM || !ISM->GetInstanceTransform(Instance, Where, true) || !ISM->GetStaticMesh())
	{
		return false;
	}
	return ISM->GetStaticMesh()->GetBoundingBox().TransformBy(Where).ComputeSquaredDistanceToPoint(Point) < FMath::Square(Radius);
}

void AFTODestruction::Apply(const FFTOBrokenPiece& Piece)
{
	const TPair<FName, int32> Key(Piece.Component, Piece.Instance);
	if (Applied.Contains(Key))
	{
		Predicted.Remove(Key); // (the server's word on one we broke ahead of it)
		return;
	}
	AFTOCityGenerator* TheCity = FindCity();
	UInstancedStaticMeshComponent* ISM = TheCity ? TheCity->FindInstanced(Piece.Component) : nullptr;
	if (!ISM || !ISM->IsValidInstance(Piece.Instance))
	{
		// The city isn't built here yet (just joined): later.
		if ((!TheCity || !TheCity->IsGeometryBuilt()) && !PendingKeys.Contains(Key))
		{
			PendingKeys.Add(Key);
			Pending.Add(Piece);
		}
		return;
	}
	Applied.Add(Key);

	FTransform Was;
	ISM->GetInstanceTransform(Piece.Instance, Was, true);
	const FLinearColor Color = InstanceColor(ISM, Piece.Instance);

	// Tucked away out of sight and reach (instances keep their numbers, so moving it beats removing it).
	const FTransform Gone(Was.GetRotation(), Was.GetLocation() - FVector(0.f, 0.f, 100000.f), FVector(0.001f));
	ISM->UpdateInstanceTransform(Piece.Instance, Gone, true, false, true);

	const AGameStateBase* GS = GetWorld()->GetGameState();
	// (No server clock yet, just joined: it's old news, no replay.)
	const float Age = GS ? GS->GetServerWorldTimeSeconds() - Piece.Time : TNumericLimits<float>::Max();
	EFTOBreakKind Kind = KindOf(ISM);
	int32 S = INDEX_NONE;
	int32 P = INDEX_NONE;
	const bool bBuilding = EnsureIndexed() && FindPiece(Piece.Component, Piece.Instance, S, P);
	if (bBuilding && Kind == EFTOBreakKind::None)
	{
		Kind = GetStructure(S)->Pieces[P].Role == EFTOPieceRole::Inside ? EFTOBreakKind::KnockOff : EFTOBreakKind::Crumble;
	}
	if (Age < FreshSeconds)
	{
		Show(Kind, ISM, Was, Color, Piece);
	}
	if (bBuilding && ISM->GetStaticMesh())
	{
		// Whatever was stuck on it (bullet holes, cracks) goes with it.
		if (UFTODebris* Debris = UFTODebris::Get(GetWorld()))
		{
			Debris->ClearMarks(ISM->GetStaticMesh()->GetBoundingBox().TransformBy(Was).ExpandBy(15.f));
		}
	}
	if (Kind == EFTOBreakKind::Burst && Age < HydrantSeconds)
	{
		if (UFTODebris* Debris = UFTODebris::Get(GetWorld()))
		{
			Debris->Fountain(Was.GetLocation() + FVector(0.f, 0.f, 45.f), HydrantSeconds - FMath::Max(0.f, Age));
		}
	}
}

void AFTODestruction::Show(EFTOBreakKind Kind, const UInstancedStaticMeshComponent* From, const FTransform& Was, const FLinearColor& Color, const FFTOBrokenPiece& Piece)
{
	UFTODebris* Debris = UFTODebris::Get(GetWorld());
	UStaticMesh* Mesh = From->GetStaticMesh();
	if (!Debris || !Mesh)
	{
		return;
	}
	const FBreakable* Entry = FindBreakable(From);
	const FLinearColor Bits = Entry && Entry->Bits.A > 0.f ? Entry->Bits : Color;
	const FVector Push = Piece.Push;
	const FVector Dir = Push.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Push.GetSafeNormal2D();
	const FVector Across = FVector::CrossProduct(FVector::UpVector, Dir);
	const FBox Bounds = Mesh->GetBoundingBox();
	const float Height = (Bounds.Max.Z - Bounds.Min.Z) * Was.GetScale3D().Z;
	const FVector Middle = Was.GetLocation() + FVector(0.f, 0.f, Height * 0.5f);

	switch (Kind)
	{
	case EFTOBreakKind::Shatter:
		Debris->Shards(Was, Bounds, Piece.Hit, Push, Piece.How == FTOBreakHow::Hit ? 18 : 8);
		break;

	case EFTOBreakKind::KnockOff:
	case EFTOBreakKind::Burst:
		// Sent flying whole, tumbling end over end.
		Debris->Throw(Mesh, Was, Color, Push * 0.8f + FVector(0.f, 0.f, 250.f + Push.Size() * 0.3f), Across * FMath::FRandRange(400.f, 800.f) + FMath::VRand() * 150.f,
			6.f, Entry ? Entry->Mass : 20.f);
		Debris->Chunks(Middle, Push * 0.5f, Bits, 3, 10.f, 3.f);
		break;

	case EFTOBreakKind::Topple:
		// Over it goes, top first along the push (a spin about the axis across it swings the top that way).
		Debris->Throw(Mesh, Was, Color, Dir * 120.f + FVector(0.f, 0.f, 60.f), Across * 70.f, 8.f, Entry ? Entry->Mass : 150.f);
		Debris->Chunks(Was.GetLocation() + FVector(0.f, 0.f, 30.f), Push * 0.4f, Bits, 5, 14.f, 4.f);
		break;

	case EFTOBreakKind::Smash:
		// To pieces: chunks of whatever it's made of, big ones and small ones.
		Debris->Chunks(Middle, Push, Bits, 7, 28.f, 5.f);
		Debris->Chunks(Middle, Push * 0.7f, Bits, 5, 12.f, 4.f);
		if (Mesh->GetName() == TEXT("SM_BusStop"))
		{
			Debris->Shards(Was, Bounds, Piece.Hit, Push, 12);
		}
		break;

	case EFTOBreakKind::Crumble:
	{
		// (A building's panels face out of it.)
		const FVector Out = Was.GetRotation().GetForwardVector();
		if (Piece.How == FTOBreakHow::Fall)
		{
			// Nothing under it: off the building it comes, whole, toppling outwards.
			Debris->Throw(Mesh, Was, Color, Out * 160.f + FVector(0.f, 0.f, -40.f), FVector::CrossProduct(FVector::UpVector, Out) * FMath::FRandRange(-60.f, 60.f), 8.f, 900.f);
			Debris->Dust(Middle, 120.f, BuildingDust, 3.f, 4);
			break;
		}
		if (Piece.How == FTOBreakHow::Along)
		{
			// Something that was on the wall: it drops (big things just break up).
			const float Size = (Bounds.Max - Bounds.Min).Size() * Was.GetScale3D().GetMax();
			if (Size < 450.f)
			{
				Debris->Throw(Mesh, Was, Color, Push * 0.25f + Out * 80.f, FMath::VRand() * 120.f, 6.f, 60.f);
			}
			else
			{
				Debris->Chunks(Middle, Push * 0.4f, Color, 4, 22.f, 4.f);
			}
			break;
		}
		// The panel itself: lumps of masonry blown along the push, a couple of big slabs, and a cloud of dust.
		const FVector Blow = Push.IsNearlyZero() ? -Out * 400.f : Push;
		Debris->Chunks(Middle, Blow * 0.6f, Color, 9, 34.f, 6.f);
		Debris->Chunks(Middle, Blow * 0.8f, Color * 0.75f, 7, 14.f, 4.f);
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		for (int32 i = 0; i < 2; ++i)
		{
			const FTransform Slab(FRotator(FMath::FRandRange(-30.f, 30.f), Was.Rotator().Yaw + FMath::FRandRange(-20.f, 20.f), FMath::FRandRange(-30.f, 30.f)),
				Middle + FVector(0.f, 0.f, FMath::FRandRange(-80.f, 80.f)), FVector(0.25f, FMath::FRandRange(0.6f, 0.9f), FMath::FRandRange(0.5f, 0.8f)));
			Debris->Throw(Cube, Slab, Color, Blow * 0.35f + FVector(0.f, 0.f, 120.f), FMath::VRand() * 200.f, 7.f, 300.f);
		}
		Debris->Dust(Was.GetLocation() + FVector(0.f, 0.f, 40.f), 150.f, BuildingDust, 4.5f, 10);
		break;
	}

	default:
		break;
	}
}

// ------------------------------------------------------------------------------------------
// Buildings
// ------------------------------------------------------------------------------------------

const FFTOStructure* AFTODestruction::GetStructure(int32 Index) const
{
	return City && City->GetStructures().IsValidIndex(Index) ? &City->GetStructures()[Index] : nullptr;
}

bool AFTODestruction::EnsureIndexed()
{
	if (bIndexed)
	{
		return true;
	}
	AFTOCityGenerator* TheCity = FindCity();
	if (!TheCity || !TheCity->IsGeometryBuilt())
	{
		return false;
	}
	bIndexed = true;
	const TArray<FFTOStructure>& All = TheCity->GetStructures();
	Cells.SetNum(All.Num());
	for (int32 s = 0; s < All.Num(); ++s)
	{
		const FFTOStructure& S = All[s];
		FCells& C = Cells[s];
		C.Ring = S.RingLength();
		C.Levels = S.Floors + 1;
		const int32 Num = C.Ring * C.Levels;
		C.Pieces.SetNum(Num);
		C.Wall.Init(INDEX_NONE, Num);
		C.Down.Init(false, Num);
		for (int32 p = 0; p < S.Pieces.Num(); ++p)
		{
			const FFTOStructurePiece& Piece = S.Pieces[p];
			PieceIndex.Add(TPair<FName, int32>(Piece.Component, Piece.Instance), FIntPoint(s, p));
			if (Piece.Role == EFTOPieceRole::Wall && Piece.Face >= 0 && Piece.Level < C.Levels)
			{
				const int32 Cell = Piece.Level * C.Ring + S.RingOf(Piece.Face, Piece.Column);
				if (C.Wall[Cell] == INDEX_NONE)
				{
					C.Wall[Cell] = p;
					C.Pieces[Cell].Insert(p, 0);
				}
			}
		}
		// A roller door is two panels wide (and placed on the line between them): it fills the one before too.
		for (int32 p = 0; p < S.Pieces.Num(); ++p)
		{
			const FFTOStructurePiece& Piece = S.Pieces[p];
			if (Piece.Role == EFTOPieceRole::Wall && Piece.Face >= 0 && Piece.Column > 0 && Piece.Level < C.Levels &&
				Piece.Component.ToString().StartsWith(TEXT("SM_Wall_G_Roller")))
			{
				const int32 Before = Piece.Level * C.Ring + S.RingOf(Piece.Face, Piece.Column - 1);
				if (C.Wall[Before] == INDEX_NONE)
				{
					C.Wall[Before] = p;
				}
			}
		}
		// Glass and trim go with the panel they're in or on (a parapet, or a cornice, with the panel below).
		for (int32 p = 0; p < S.Pieces.Num(); ++p)
		{
			const FFTOStructurePiece& Piece = S.Pieces[p];
			if ((Piece.Role != EFTOPieceRole::Glass && Piece.Role != EFTOPieceRole::Trim) || Piece.Face < 0)
			{
				continue;
			}
			for (int32 L = FMath::Min<int32>(Piece.Level, C.Levels - 1); L >= FMath::Max(0, Piece.Level - 1); --L)
			{
				const int32 Cell = L * C.Ring + S.RingOf(Piece.Face, Piece.Column);
				if (C.Wall[Cell] != INDEX_NONE)
				{
					C.Pieces[Cell].Add(p);
					break;
				}
			}
		}
	}
	UE_LOG(LogFTO, Log, TEXT("Destruction: %d buildings, %d pieces."), All.Num(), PieceIndex.Num());
	return true;
}

bool AFTODestruction::FindPiece(FName Component, int32 Instance, int32& OutStructure, int32& OutPiece) const
{
	const FIntPoint* Found = bIndexed ? PieceIndex.Find(TPair<FName, int32>(Component, Instance)) : nullptr;
	if (!Found)
	{
		return false;
	}
	OutStructure = Found->X;
	OutPiece = Found->Y;
	return true;
}

bool AFTODestruction::GetStructurePiece(const UPrimitiveComponent* Component, int32 Instance, EFTOPieceRole& OutRole)
{
	if (!Component || Instance == INDEX_NONE || !Component->GetOwner() || !Component->GetOwner()->IsA<AFTOCityGenerator>() || !EnsureIndexed())
	{
		return false;
	}
	int32 S = INDEX_NONE;
	int32 P = INDEX_NONE;
	if (!FindPiece(Component->GetFName(), Instance, S, P) || IsBroken(Component->GetFName(), Instance))
	{
		return false;
	}
	OutRole = GetStructure(S)->Pieces[P].Role;
	return true;
}

bool AFTODestruction::FindWallOf(const UPrimitiveComponent* Component, int32 Instance, UInstancedStaticMeshComponent*& OutWall, int32& OutInstance)
{
	int32 S = INDEX_NONE;
	int32 P = INDEX_NONE;
	if (!Component || !EnsureIndexed() || !FindPiece(Component->GetFName(), Instance, S, P))
	{
		return false;
	}
	const FFTOStructure& Structure = *GetStructure(S);
	const FFTOStructurePiece& Piece = Structure.Pieces[P];
	const FCells& C = Cells[S];
	if (Piece.Face < 0 || C.Ring <= 0)
	{
		return false;
	}
	const int32 Cell = FMath::Min<int32>(Piece.Level, C.Levels - 1) * C.Ring + Structure.RingOf(Piece.Face, Piece.Column);
	const int32 Wall = C.Wall.IsValidIndex(Cell) ? C.Wall[Cell] : INDEX_NONE;
	if (Wall == INDEX_NONE || IsBroken(Structure.Pieces[Wall].Component, Structure.Pieces[Wall].Instance))
	{
		return false;
	}
	OutWall = City->FindInstanced(Structure.Pieces[Wall].Component);
	OutInstance = Structure.Pieces[Wall].Instance;
	return OutWall != nullptr;
}

void AFTODestruction::WallsInTheWay(const UPrimitiveComponent* Component, int32 Instance, const FVector& Center, float HalfWidth,
	TArray<TPair<UInstancedStaticMeshComponent*, int32>>& OutPieces)
{
	int32 S = INDEX_NONE;
	int32 P = INDEX_NONE;
	if (!Component || !EnsureIndexed() || !FindPiece(Component->GetFName(), Instance, S, P))
	{
		return;
	}
	const FFTOStructure& Structure = *GetStructure(S);
	const FFTOStructurePiece& Piece = Structure.Pieces[P];
	const FCells& C = Cells[S];
	if (Piece.Face < 0 || C.Ring <= 0 || Piece.Level >= C.Levels)
	{
		return;
	}
	// Along the face is round the ring (each face runs on from the last).
	const FVector Out = FaceNormal(Piece.Face);
	const float Offset = FVector::DotProduct(Center - Piece.Location, FVector(-Out.Y, Out.X, 0.f));
	const int32 Ring = Structure.RingOf(Piece.Face, Piece.Column);
	TArray<int32, TInlineAllocator<3>> Places = { Ring };
	if (Offset + HalfWidth > FTOKit::PanelWidth * 0.5f)
	{
		Places.Add((Ring + 1) % C.Ring);
	}
	if (Offset - HalfWidth < -FTOKit::PanelWidth * 0.5f)
	{
		Places.Add((Ring - 1 + C.Ring) % C.Ring);
	}
	for (const int32 Place : Places)
	{
		const int32 Cell = Piece.Level * C.Ring + Place;
		if (C.Wall[Cell] == INDEX_NONE)
		{
			continue;
		}
		for (const int32 p : C.Pieces[Cell])
		{
			const FFTOStructurePiece& Other = Structure.Pieces[p];
			if ((Other.Role == EFTOPieceRole::Wall || Other.Role == EFTOPieceRole::Glass) && !IsBroken(Other.Component, Other.Instance))
			{
				if (UInstancedStaticMeshComponent* ISM = City->FindInstanced(Other.Component))
				{
					OutPieces.Emplace(ISM, Other.Instance);
				}
			}
		}
	}
}

void AFTODestruction::DamageWall(UPrimitiveComponent* Component, int32 Instance, float Amount, const FVector& Hit, const FVector& Normal, const FVector& Push, AController* ByWhom)
{
	check(HasAuthority());
	int32 S = INDEX_NONE;
	int32 P = INDEX_NONE;
	if (!Component || Amount <= 0.f || !EnsureIndexed() || !FindPiece(Component->GetFName(), Instance, S, P))
	{
		return;
	}
	const FFTOStructurePiece& Piece = GetStructure(S)->Pieces[P];
	const TPair<FName, int32> Key(Piece.Component, Piece.Instance);
	if (Piece.Role != EFTOPieceRole::Wall || BrokenKeys.Contains(Key))
	{
		return;
	}
	float& Worn = WallDamage.FindOrAdd(Key);
	Worn += Amount;
	if (Worn >= WallStrength)
	{
		if (Piece.Face >= 0 && Piece.Level < Cells[S].Levels)
		{
			CrumbleCell(S, Piece.Level * Cells[S].Ring + GetStructure(S)->RingOf(Piece.Face, Piece.Column), Hit, Push, ByWhom);
		}
		else
		{
			AddBroken(Key.Key, Key.Value, Hit, Push, FTOBreakHow::Hit);
		}
		return;
	}

	// Still standing, but it shows: everyone sees the cracks spread.
	FFTOWallHit* Item = nullptr;
	if (const int32* At = WallHitIndex.Find(Key))
	{
		Item = &WallHits.Items[*At];
	}
	else
	{
		Item = &WallHits.Items.AddDefaulted_GetRef();
		Item->Component = Key.Key;
		Item->Instance = Key.Value;
		WallHitIndex.Add(Key, WallHits.Items.Num() - 1);
	}
	Item->Damage = uint8(FMath::Clamp(FMath::RoundToInt(Worn), 1, 99));
	Item->Hit = Hit;
	Item->Normal = Normal.GetSafeNormal();
	Item->Time = GetWorld()->GetTimeSeconds();
	WallHits.MarkItemDirty(*Item);
	const FFTOWallHit Copy = *Item;
	ShowWallHit(Copy);
	if (Amount >= 8.f)
	{
		if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
		{
			GS->MulticastPlaySound(FTOAudio::Pick(TEXT("Rubble")), Hit, FMath::Clamp(Amount / 40.f, 0.4f, 1.f));
		}
	}
}

void AFTODestruction::ShowWallHit(const FFTOWallHit& Hit)
{
	AFTOCityGenerator* TheCity = FindCity();
	UInstancedStaticMeshComponent* ISM = TheCity ? TheCity->FindInstanced(Hit.Component) : nullptr;
	if (!ISM || !ISM->IsValidInstance(Hit.Instance))
	{
		if (!TheCity || !TheCity->IsGeometryBuilt())
		{
			PendingHits.Add(Hit);
		}
		return;
	}
	const TPair<FName, int32> Key(Hit.Component, Hit.Instance);
	if (Applied.Contains(Key))
	{
		return; // it's already come down here
	}
	uint8& Shown = WallShown.FindOrAdd(Key);
	const int32 Before = Shown;
	if (Hit.Damage <= Before)
	{
		return;
	}
	Shown = Hit.Damage;
	const int32 Fresh = Hit.Damage - Before;

	// The paint goes dusty and dark as it's knocked about.
	const FLinearColor* Clean = CleanPaint.Find(Key);
	if (!Clean)
	{
		Clean = &CleanPaint.Add(Key, InstanceColor(ISM, Hit.Instance));
	}
	const FLinearColor Worn = FMath::Lerp(*Clean, BuildingDust * 0.6f, 0.55f * Hit.Damage / WallStrength);
	ISM->SetCustomData(Hit.Instance, { Worn.R, Worn.G, Worn.B }, false);

	UFTODebris* Debris = UFTODebris::Get(GetWorld());
	if (!Debris)
	{
		return;
	}
	// Catching up (just joined): the cracks are there, but nothing's flying.
	const AGameStateBase* GS = GetWorld()->GetGameState();
	if (!GS || GS->GetServerWorldTimeSeconds() - Hit.Time > FreshSeconds)
	{
		Debris->Crack(Hit.Hit, FVector(Hit.Normal).IsNearlyZero() ? FVector::UpVector : FVector(Hit.Normal), FMath::Min(70.f + Hit.Damage * 1.2f, 200.f));
		return;
	}
	// A real knock (or a quarter more of it gone) cracks it; a stray round just chips it.
	const FVector Normal = FVector(Hit.Normal).IsNearlyZero() ? FVector::UpVector : FVector(Hit.Normal);
	const bool bKnock = Fresh >= 6;
	if (bKnock || Hit.Damage / 25 > Before / 25)
	{
		Debris->Crack(Hit.Hit, Normal, FMath::Min(70.f + Hit.Damage * 1.2f, 200.f) * FMath::FRandRange(0.85f, 1.1f));
	}
	Debris->Chunks(Hit.Hit + Normal * 10.f, Normal * 350.f, Worn, bKnock ? 5 : 1, bKnock ? 16.f : 7.f, 3.f);
	if (bKnock)
	{
		Debris->Dust(Hit.Hit + Normal * 40.f, 60.f, BuildingDust, 2.5f, 4);
	}
}

void AFTODestruction::DamageAt(const FVector& Point, float Radius, float Amount, AController* ByWhom)
{
	check(HasAuthority());
	AFTOCityGenerator* TheCity = FindCity();
	if (!TheCity || !EnsureIndexed() || Amount <= 0.f)
	{
		return;
	}
	// Every standing panel in reach, harder the closer (gathered first: knocking one down changes what's standing).
	struct FKnock { UInstancedStaticMeshComponent* ISM; int32 Instance; float Amount; FVector Hit; FVector Normal; };
	TArray<FKnock> Knocks;
	const TArray<FFTOStructure>& All = TheCity->GetStructures();
	for (int32 s = 0; s < All.Num(); ++s)
	{
		const FFTOStructure& S = All[s];
		const FCells& C = Cells[s];
		if (!S.Contains2D(Point, Radius + 50.f) || C.CollapsedFrom == 0)
		{
			continue;
		}
		for (int32 Cell = 0; Cell < C.Wall.Num(); ++Cell)
		{
			if (C.Wall[Cell] == INDEX_NONE || C.Down[Cell])
			{
				continue;
			}
			const FFTOStructurePiece& Piece = S.Pieces[C.Wall[Cell]];
			const FVector Out = FaceNormal(Piece.Face);
			const FVector Along(-Out.Y, Out.X, 0.f);
			// The nearest point on the panel's face.
			const FVector Base = Piece.Location;
			const FVector Rel = Point - Base;
			const FVector OnFace = Base + Along * FMath::Clamp(FVector::DotProduct(Rel, Along), -FTOKit::PanelWidth * 0.5f, FTOKit::PanelWidth * 0.5f) +
				FVector(0.f, 0.f, FMath::Clamp(Rel.Z, 0.f, PanelHeight(Piece.Level)));
			const float Distance = FVector::Dist(Point, OnFace);
			if (Distance >= Radius)
			{
				continue;
			}
			UInstancedStaticMeshComponent* ISM = TheCity->FindInstanced(Piece.Component);
			if (ISM)
			{
				// (Cracks on whichever side the knock came from.)
				const FVector Facing = FVector::DotProduct(Rel, Out) >= 0.f ? Out : -Out;
				Knocks.Add({ ISM, Piece.Instance, Amount * (1.f - Distance / Radius), OnFace + Facing * 2.f, Facing });
			}
		}
	}
	for (const FKnock& Knock : Knocks)
	{
		DamageWall(Knock.ISM, Knock.Instance, Knock.Amount, Knock.Hit, Knock.Normal, (Knock.Hit - Point).GetSafeNormal() * 700.f, ByWhom);
	}
}

void AFTODestruction::CrumbleCell(int32 Structure, int32 Cell, const FVector& Hit, const FVector& Push, AController* ByWhom)
{
	FCells& C = Cells[Structure];
	if (!C.Down.IsValidIndex(Cell) || C.Down[Cell])
	{
		return;
	}
	C.Down[Cell] = true;
	// (A roller door's two panels go together.)
	const int32 Level = Cell / FMath::Max(1, C.Ring);
	for (int32 Other = Level * C.Ring; Other < (Level + 1) * C.Ring; ++Other)
	{
		if (C.Wall[Other] == C.Wall[Cell])
		{
			C.Down[Other] = true;
		}
	}
	const FFTOStructure& S = *GetStructure(Structure);
	for (const int32 p : C.Pieces[Cell])
	{
		const FFTOStructurePiece& Piece = S.Pieces[p];
		if (!BrokenKeys.Contains(TPair<FName, int32>(Piece.Component, Piece.Instance)))
		{
			AddBroken(Piece.Component, Piece.Instance, Hit, Push, p == C.Wall[Cell] ? FTOBreakHow::Hit : FTOBreakHow::Along);
		}
	}
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastPlaySound(FTOAudio::Pick(TEXT("WallBreak")), Hit, 1.f);
		if (IsPolice(ByWhom))
		{
			GS->AddChaos(ChaosFor(EFTOBreakKind::Crumble));
			++GS->PropertyBroken;
		}
	}
	Settle(Structure, ByWhom);
}

void AFTODestruction::Settle(int32 Structure, AController* ByWhom)
{
	FCells& C = Cells[Structure];
	const FFTOStructure& S = *GetStructure(Structure);
	if (C.Ring <= 0)
	{
		return;
	}
	TArray<bool> Held;
	Held.Init(false, C.Ring);
	for (int32 L = 0; L < C.Levels && L < C.CollapsedFrom; ++L)
	{
		auto Standing = [&C, L](int32 r)
		{
			const int32 Cell = L * C.Ring + r;
			return C.Wall[Cell] != INDEX_NONE && !C.Down[Cell];
		};
		// Standing on the panel below (the ground floor stands on the ground)...
		TArray<bool> Up;
		Up.Init(false, C.Ring);
		for (int32 r = 0; r < C.Ring; ++r)
		{
			Up[r] = Standing(r) && (L == 0 || Held[r]);
		}
		// ...or propped up by a neighbour that is, a panel or two either side.
		TArray<bool> Propped = Up;
		for (int32 r = 0; r < C.Ring; ++r)
		{
			if (!Up[r])
			{
				continue;
			}
			for (const int32 Dir : { -1, 1 })
			{
				for (int32 k = 1; k <= 2; ++k)
				{
					const int32 n = ((r + Dir * k) % C.Ring + C.Ring) % C.Ring;
					if (!Standing(n))
					{
						break;
					}
					Propped[n] = true;
				}
			}
		}
		// Whatever's left hanging drops off.
		for (int32 r = 0; r < C.Ring; ++r)
		{
			if (!Standing(r) || Propped[r])
			{
				continue;
			}
			const int32 Cell = L * C.Ring + r;
			C.Down[Cell] = true;
			for (const int32 p : C.Pieces[Cell])
			{
				const FFTOStructurePiece& Piece = S.Pieces[p];
				if (!BrokenKeys.Contains(TPair<FName, int32>(Piece.Component, Piece.Instance)))
				{
					AddBroken(Piece.Component, Piece.Instance, Piece.Location, FVector::ZeroVector, p == C.Wall[Cell] ? FTOBreakHow::Fall : FTOBreakHow::Along);
				}
			}
		}
		Held = Propped;

		// Too much of this storey gone (or nearly all of one side of it): down it comes, and everything above.
		int32 Have = 0;
		int32 Left = 0;
		int32 FaceHave[4] = { 0, 0, 0, 0 };
		int32 FaceLeft[4] = { 0, 0, 0, 0 };
		for (int32 r = 0; r < C.Ring; ++r)
		{
			if (C.Wall[L * C.Ring + r] == INDEX_NONE)
			{
				continue;
			}
			const int32 Face = S.FaceAt(r);
			++Have;
			++FaceHave[Face];
			if (Propped[r])
			{
				++Left;
				++FaceLeft[Face];
			}
		}
		bool bGives = Have > 0 && Left < Have * 0.6f;
		for (int32 Face = 0; Face < 4; ++Face)
		{
			// (Under a quarter of a side left: a short side has to go altogether.)
			bGives |= FaceHave[Face] >= 3 && FaceLeft[Face] * 4 < FaceHave[Face];
		}
		if (bGives)
		{
			Collapse(Structure, L, ByWhom);
			return;
		}
	}
}

void AFTODestruction::Collapse(int32 Structure, int32 FromLevel, AController* ByWhom)
{
	check(HasAuthority());
	FCells& C = Cells[Structure];
	if (FromLevel >= C.CollapsedFrom)
	{
		return;
	}
	C.CollapsedFrom = FromLevel;
	const FFTOStructure& S = *GetStructure(Structure);
	for (int32 Cell = FromLevel * C.Ring; Cell < C.Down.Num(); ++Cell)
	{
		C.Down[Cell] = true;
	}
	// Nothing in it can be broken again.
	for (const FFTOStructurePiece& Piece : S.Pieces)
	{
		if (Piece.Level >= FromLevel && Piece.Role != EFTOPieceRole::Foundation)
		{
			const TPair<FName, int32> Key(Piece.Component, Piece.Instance);
			BrokenKeys.Add(Key);
			Hits.Remove(Key);
			WallDamage.Remove(Key);
		}
	}

	FFTOCollapse& Event = Collapses.AddDefaulted_GetRef();
	Event.Structure = Structure;
	Event.FromLevel = FromLevel;
	Event.Time = GetWorld()->GetTimeSeconds();
	const FFTOCollapse Copy = Event;
	ForceNetUpdate();
	ApplyCollapse(Copy);

	const float BaseZ = S.Center.Z + LevelZ(FromLevel);
	// Everyone in it (or on it) is thrown off their feet.
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		const FVector Where = It->GetActorLocation();
		UFTOKnockdownComponent* Knockdown = It->FindComponentByClass<UFTOKnockdownComponent>();
		const AFTOCharacter* Person = Cast<AFTOCharacter>(*It);
		if (!Knockdown || Knockdown->IsDown() || (Person && Person->GetCurrentVehicle()) || !S.Contains2D(Where, 80.f) || Where.Z < BaseZ - 60.f)
		{
			continue;
		}
		const FVector Out = (Where - S.Center).GetSafeNormal2D();
		Knockdown->Knockdown(Out * 350.f + FVector(0.f, 0.f, 200.f), 5.f);
	}
	// Its lift: the stops that came down are gone, and it runs between the floors that are left (if two are).
	TArray<AFTOLift*> Kept;
	TArray<AFTOLift*> Gone;
	for (TActorIterator<AFTOLift> It(GetWorld()); It; ++It)
	{
		if (S.Contains2D(It->GetActorLocation(), 80.f))
		{
			(It->GetFloor() < FromLevel ? Kept : Gone).Add(*It);
		}
	}
	if (Kept.Num() < 2)
	{
		Gone.Append(Kept);
		Kept.Reset();
	}
	for (AFTOLift* Stop : Gone)
	{
		Stop->Destroy();
	}
	if (!Kept.IsEmpty())
	{
		Kept.Sort([](const AFTOLift& A, const AFTOLift& B) { return A.GetFloor() < B.GetFloor(); });
		AFTOLift::LinkStops(Kept);
	}
	// Nobody's going to be in there now.
	if (FromLevel == 0 && S.Building != INDEX_NONE)
	{
		if (AFTOInteriorLife* Life = AFTOInteriorLife::Get(GetWorld()))
		{
			Life->Abandon(S.Building);
		}
	}
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastPlaySound(AFTOGameState::Sounds().Collapse, FVector(S.Center.X, S.Center.Y, BaseZ + 300.f), 1.f);
		if (IsPolice(ByWhom))
		{
			GS->AddChaos(5.f + 1.5f * (C.Levels - FromLevel));
			++GS->PropertyBroken;
		}
	}
	UE_LOG(LogFTO, Log, TEXT("Building %d came down from storey %d (%d storeys)."), Structure, FromLevel, C.Levels);
}

void AFTODestruction::OnRep_Collapses()
{
	for (const FFTOCollapse& Event : Collapses)
	{
		ApplyCollapse(Event);
	}
}

bool AFTODestruction::IsStructureDown(int32 Structure, int32 FromLevel) const
{
	return Collapses.ContainsByPredicate([Structure, FromLevel](const FFTOCollapse& Event) { return Event.Structure == Structure && Event.FromLevel <= FromLevel; });
}

bool AFTODestruction::IsBuildingDown(int32 BuildingIndex) const
{
	if (BuildingIndex == INDEX_NONE)
	{
		return false;
	}
	for (const FFTOCollapse& Event : Collapses)
	{
		const FFTOStructure* S = GetStructure(Event.Structure);
		if (Event.FromLevel == 0 && S && S->Building == BuildingIndex)
		{
			return true;
		}
	}
	return false;
}

float AFTODestruction::GetWallDamage(FName Component, int32 Instance) const
{
	const float* Worn = WallDamage.Find(TPair<FName, int32>(Component, Instance));
	return Worn ? *Worn : 0.f;
}

int32 AFTODestruction::NumRubble() const
{
	return Rubble ? Rubble->GetInstanceCount() : 0;
}

void AFTODestruction::ApplyCollapse(const FFTOCollapse& Event)
{
	const FIntPoint Id(Event.Structure, Event.FromLevel);
	if (AppliedCollapses.Contains(Id) || !EnsureIndexed())
	{
		return; // (or later, once the city's built here: Tick)
	}
	AppliedCollapses.Add(Id);
	const FFTOStructure* S = GetStructure(Event.Structure);
	if (!S)
	{
		return;
	}
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const float Age = GS ? GS->GetServerWorldTimeSeconds() - Event.Time : TNumericLimits<float>::Max();
	const bool bShow = Age < FreshCollapseSeconds && GetNetMode() != NM_DedicatedServer;
	const float BaseZ = S->Center.Z + LevelZ(Event.FromLevel);
	const float Now = GetWorld()->GetTimeSeconds();
	FRandomStream Rng(Event.Structure * 7919 + Event.FromLevel);
	// (The heap's own dice: the same on every machine whatever else rolled.)
	FRandomStream RubbleRng(Event.Structure * 104729 + Event.FromLevel * 31 + 7);

	// Everything from that storey up is tucked away at once (no one stands on it now); copies of it fall for show.
	for (const FFTOStructurePiece& Piece : S->Pieces)
	{
		if (Piece.Level < Event.FromLevel || Piece.Role == EFTOPieceRole::Foundation)
		{
			continue;
		}
		const TPair<FName, int32> Key(Piece.Component, Piece.Instance);
		Predicted.Remove(Key);
		UInstancedStaticMeshComponent* ISM = Applied.Contains(Key) ? nullptr : City->FindInstanced(Piece.Component);
		if (!ISM || !ISM->IsValidInstance(Piece.Instance))
		{
			continue;
		}
		Applied.Add(Key);
		FTransform Was;
		ISM->GetInstanceTransform(Piece.Instance, Was, true);
		if (bShow)
		{
			if (UInstancedStaticMeshComponent* Proxy = ProxyFor(ISM))
			{
				const FLinearColor Color = InstanceColor(ISM, Piece.Instance);
				FFalling& Fall = Falling.AddDefaulted_GetRef();
				Fall.Proxy = Proxy;
				Fall.Index = Proxy->AddInstance(Was, true);
				Proxy->SetCustomData(Fall.Index, { Color.R, Color.G, Color.B }, false);
				Fall.Start = Was;
				// The bottom goes first, the rest pancaking down after it.
				Fall.StartTime = Now + (Piece.Level - Event.FromLevel) * 0.04f + Rng.FRandRange(0.f, 0.15f);
				Fall.DropTo = FMath::Max(0.f, Was.GetLocation().Z - BaseZ) + 450.f;
				Fall.Drift = (S->Center - Was.GetLocation()).GetSafeNormal2D() * Rng.FRandRange(10.f, 60.f) + FVector(Rng.FRandRange(-15.f, 15.f), Rng.FRandRange(-15.f, 15.f), 0.f);
				Fall.Tumble = FRotator(Rng.FRandRange(-12.f, 12.f), Rng.FRandRange(-8.f, 8.f), Rng.FRandRange(-12.f, 12.f));
			}
		}
		const FTransform Gone(Was.GetRotation(), Was.GetLocation() - FVector(0.f, 0.f, 100000.f), FVector(0.001f));
		ISM->UpdateInstanceTransform(Piece.Instance, Gone, true, false, true);
	}
	for (const TWeakObjectPtr<UTextRenderComponent>& Label : S->Labels)
	{
		if (UTextRenderComponent* Text = Label.Get())
		{
			if (Text->GetComponentLocation().Z >= BaseZ - 20.f)
			{
				Text->SetVisibility(false);
			}
		}
	}

	UFTODebris* Debris = UFTODebris::Get(GetWorld());
	if (Debris)
	{
		Debris->ClearMarks(FBox(FVector(S->Center.X - S->HalfX - 80.f, S->Center.Y - S->HalfY - 80.f, BaseZ - 30.f),
			FVector(S->Center.X + S->HalfX + 80.f, S->Center.Y + S->HalfY + 80.f, S->Center.Z + 100000.f)));
	}
	// What's left: a heap of it, higher the more came down.
	const int32 Storeys = Cells[Event.Structure].Levels - Event.FromLevel;
	HeapRubble(Event.Structure, Event.FromLevel, BaseZ, FMath::Clamp(3 + Storeys / 3, 3, 6), RubbleRng);

	if (bShow && Debris)
	{
		// A great cloud of dust rolling out from the bottom, and more as the rest comes down into it.
		const float Reach = FMath::Max(S->HalfX, S->HalfY);
		const FVector Foot(S->Center.X, S->Center.Y, BaseZ);
		Debris->Dust(Foot, Reach * 0.7f, BuildingDust, 4.5f, 20);
		Debris->Dust(Foot, Reach * 0.9f, BuildingDust, 5.f, 30, 1.5f);
		for (const FVector& Corner : { FVector(1.f, 1.f, 0.f), FVector(-1.f, 1.f, 0.f), FVector(-1.f, -1.f, 0.f), FVector(1.f, -1.f, 0.f) })
		{
			Debris->Dust(Foot + FVector(Corner.X * S->HalfX, Corner.Y * S->HalfY, 0.f), 220.f, BuildingDust * 0.9f, 4.5f, 8, 1.5f);
		}
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector Side = FVector(Rng.FRandRange(-1.f, 1.f) * S->HalfX, Rng.FRandRange(-1.f, 1.f) * S->HalfY, 0.f);
			Debris->Chunks(Foot + Side + FVector(0.f, 0.f, 150.f), Side.GetSafeNormal2D() * 600.f, S->Paint, 4, 40.f, 6.f);
		}
	}
}

void AFTODestruction::HeapRubble(int32 Index, int32 FromLevel, float FloorZ, int32 Heaps, FRandomStream& Rng)
{
	const FFTOStructure& S = *GetStructure(Index);
	if (!Rubble)
	{
		// Low, wide lumps (a car can climb the heap a step at a time; people walk over it).
		Rubble = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Rubble"));
		Rubble->SetupAttachment(RootComponent);
		Rubble->SetUsingAbsoluteLocation(true);
		Rubble->SetUsingAbsoluteRotation(true);
		Rubble->SetMobility(EComponentMobility::Movable);
		Rubble->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Rubble->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/FTO/Materials/MI_FTOCity.MI_FTOCity")));
		Rubble->SetCollisionProfileName(TEXT("BlockAll"));
		Rubble->SetCanEverAffectNavigation(false);
		Rubble->SetNumCustomDataFloats(3);
		Rubble->RegisterComponent();
	}
	// A heap left on an upper storey that's now come down too goes with it.
	TArray<FIntPoint>& Heaped = RubbleOf.FindOrAdd(Index);
	for (int32 i = Heaped.Num() - 1; i >= 0; --i)
	{
		if (Heaped[i].Y > FromLevel)
		{
			Rubble->UpdateInstanceTransform(Heaped[i].X, FTransform(FQuat::Identity, FVector(0.f, 0.f, -100000.f), FVector(0.001f)), true, false, true);
			Heaped.RemoveAtSwap(i);
		}
	}
	const float HX = FMath::Max(100.f, S.HalfX - 40.f);
	const float HY = FMath::Max(100.f, S.HalfY - 40.f);
	for (int32 k = 0; k < Heaps; ++k)
	{
		// From the edge in, each ring of lumps a step higher.
		const float Shrink = 1.f - float(k) / (Heaps + 0.5f);
		const float Top = 18.f + 17.f * k;
		const int32 Count = FMath::Clamp(int32(HX * HY * 4.f * Shrink * Shrink / (170.f * 170.f)), 4, 80);
		for (int32 n = 0; n < Count; ++n)
		{
			const float H = Top * Rng.FRandRange(0.85f, 1.05f);
			const FVector Spot(S.Center.X + Rng.FRandRange(-HX, HX) * Shrink, S.Center.Y + Rng.FRandRange(-HY, HY) * Shrink, FloorZ + H * 0.5f);
			const FVector Size(Rng.FRandRange(90.f, 260.f), Rng.FRandRange(80.f, 220.f), H);
			const int32 Lump = Rubble->AddInstance(FTransform(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), Spot, Size / 100.f), true);
			const FLinearColor Color = FMath::Lerp(S.Paint, Concrete, Rng.FRandRange(0.3f, 0.8f)) * Rng.FRandRange(0.55f, 0.85f);
			Rubble->SetCustomData(Lump, { Color.R, Color.G, Color.B }, false);
			Heaped.Add(FIntPoint(Lump, FromLevel));
		}
	}
	// A few broken slabs leaning on the top of the heap.
	const float Peak = 18.f + 17.f * (Heaps - 1);
	for (int32 n = 0; n < Heaps * 2 + 1; ++n)
	{
		const FVector Spot(S.Center.X + Rng.FRandRange(-HX, HX) * 0.3f, S.Center.Y + Rng.FRandRange(-HY, HY) * 0.3f, FloorZ + Peak + 20.f);
		const int32 Slab = Rubble->AddInstance(FTransform(FRotator(Rng.FRandRange(10.f, 35.f), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-12.f, 12.f)), Spot,
			FVector(Rng.FRandRange(1.6f, 2.4f), Rng.FRandRange(1.2f, 1.8f), 0.22f)), true);
		const FLinearColor Color = S.Paint * Rng.FRandRange(0.6f, 0.9f);
		Rubble->SetCustomData(Slab, { Color.R, Color.G, Color.B }, false);
		Heaped.Add(FIntPoint(Slab, FromLevel));
	}
}

UInstancedStaticMeshComponent* AFTODestruction::ProxyFor(const UInstancedStaticMeshComponent* Source)
{
	if (const TObjectPtr<UInstancedStaticMeshComponent>* Found = Proxies.Find(Source->GetFName()))
	{
		return *Found;
	}
	UInstancedStaticMeshComponent* Proxy = NewObject<UInstancedStaticMeshComponent>(this);
	Proxy->SetupAttachment(RootComponent);
	Proxy->SetUsingAbsoluteLocation(true);
	Proxy->SetUsingAbsoluteRotation(true);
	Proxy->SetMobility(EComponentMobility::Movable);
	Proxy->SetStaticMesh(Source->GetStaticMesh());
	Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Proxy->SetCanEverAffectNavigation(false);
	Proxy->SetNumCustomDataFloats(3);
	for (int32 Slot = 0; Slot < Source->GetNumMaterials(); ++Slot)
	{
		Proxy->SetMaterial(Slot, Source->GetMaterial(Slot));
	}
	Proxy->RegisterComponent();
	Proxies.Add(Source->GetFName(), Proxy);
	return Proxy;
}

void AFTODestruction::TickFalling(float DeltaSeconds)
{
	if (Falling.IsEmpty())
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 i = Falling.Num() - 1; i >= 0; --i)
	{
		FFalling& Fall = Falling[i];
		UInstancedStaticMeshComponent* Proxy = Fall.Proxy.Get();
		if (!Proxy)
		{
			Falling.RemoveAtSwap(i);
			continue;
		}
		const float T = Now - Fall.StartTime;
		if (T <= 0.f)
		{
			continue;
		}
		const float Drop = 0.5f * CollapseGravity * T * T;
		if (Drop >= Fall.DropTo)
		{
			// Down into the dust and gone.
			Proxy->UpdateInstanceTransform(Fall.Index, FTransform(FQuat::Identity, FVector(0.f, 0.f, -100000.f), FVector(0.001f)), true, false, false);
			Falling.RemoveAtSwap(i);
			continue;
		}
		FTransform Now3 = Fall.Start;
		Now3.SetLocation(Fall.Start.GetLocation() + Fall.Drift * T - FVector(0.f, 0.f, Drop));
		Now3.SetRotation((Fall.Tumble * T).Quaternion() * Fall.Start.GetRotation());
		Proxy->UpdateInstanceTransform(Fall.Index, Now3, true, false, false);
	}
	if (Falling.IsEmpty())
	{
		for (const TPair<FName, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : Proxies)
		{
			if (Pair.Value)
			{
				Pair.Value->ClearInstances();
			}
		}
	}
}

// ------------------------------------------------------------------------------------------
// Blasts
// ------------------------------------------------------------------------------------------

void AFTODestruction::Blast(const FVector& At, float Radius, float Amount, AController* ByWhom)
{
	check(HasAuthority());
	MulticastBlast(At, Radius);
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		USoundBase* Boom = FTOAudio::Pick(TEXT("Explosion"));
		GS->MulticastPlaySound(Boom ? Boom : AFTOGameState::Sounds().Crash.Get(), At, 1.f);
	}

	// The buildings round about.
	DamageAt(At, Radius, Amount, ByWhom);

	// Windows and street furniture.
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, At, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(Radius * 0.7f));
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Overlap.GetComponent());
		if (!ISM || Overlap.ItemIndex == INDEX_NONE || KindOf(ISM) == EFTOBreakKind::None)
		{
			continue;
		}
		FTransform Was;
		ISM->GetInstanceTransform(Overlap.ItemIndex, Was, true);
		const FVector Push = (Was.GetLocation() - At).GetSafeNormal2D() * 900.f + FVector(0.f, 0.f, 300.f);
		Break(ISM, Overlap.ItemIndex, Was.GetLocation(), Push, ByWhom);
	}

	// People thrown off their feet, and other cars knocked about (which may go up in turn).
	TArray<UFTOVehicleDamage*> Cars;
	int32 Thrown = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const float Distance = FVector::Dist(It->GetActorLocation(), At);
		if (Distance >= Radius)
		{
			continue;
		}
		const float Near = 1.f - Distance / Radius;
		if (UFTOVehicleDamage* Car = It->FindComponentByClass<UFTOVehicleDamage>())
		{
			Cars.Add(Car);
			continue;
		}
		UFTOKnockdownComponent* Knockdown = It->FindComponentByClass<UFTOKnockdownComponent>();
		const AFTOCharacter* Person = Cast<AFTOCharacter>(*It);
		if (Knockdown && !Knockdown->IsDown() && Distance < Radius * 0.8f && !(Person && Person->GetCurrentVehicle()))
		{
			Knockdown->Knockdown((It->GetActorLocation() - At).GetSafeNormal2D() * 700.f * Near + FVector(0.f, 0.f, 450.f * Near + 150.f), 5.f);
			++Thrown;
		}
	}
	for (UFTOVehicleDamage* Car : Cars)
	{
		if (!Car->IsWrecked())
		{
			Car->ApplyDamage(Amount * 0.4f * (1.f - FVector::Dist(Car->GetOwner()->GetActorLocation(), At) / Radius), At, ByWhom);
		}
	}
	UE_LOG(LogFTO, Log, TEXT("Blast at %s: %d people thrown, %d cars caught in it."), *At.ToCompactString(), Thrown, Cars.Num() - 1);
}

void AFTODestruction::MulticastBlast_Implementation(FVector_NetQuantize At, float Radius)
{
	FTOJuice::ShakeAt(GetWorld(), At, 1.f, Radius * 4.f);
	UFTODebris* Debris = UFTODebris::Get(GetWorld());
	if (!Debris)
	{
		return;
	}
	// A ball of fire, black smoke rolling up after it, and bits of car.
	Debris->Dust(At + FVector(0.f, 0.f, 80.f), Radius * 0.2f, FireColor, 0.9f, 12, 0.f, 4.f);
	Debris->Dust(At + FVector(0.f, 0.f, 120.f), Radius * 0.3f, SmokeColor, 5.f, 24, 0.8f);
	Debris->Chunks(At + FVector(0.f, 0.f, 60.f), FVector(0.f, 0.f, 900.f), Metal, 8, 18.f, 5.f);
}

void AFTODestruction::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickFalling(DeltaSeconds);
	// Pieces this driver broke ahead of the server that it never confirmed: back they go.
	if (!Predicted.IsEmpty())
	{
		const float Now = GetWorld()->GetTimeSeconds();
		for (auto It = Predicted.CreateIterator(); It; ++It)
		{
			if (Now - It.Value().Value < 2.5f)
			{
				continue;
			}
			if (UInstancedStaticMeshComponent* ISM = City ? City->FindInstanced(It.Key().Key) : nullptr)
			{
				ISM->UpdateInstanceTransform(It.Key().Value, It.Value().Key, true, false, true);
			}
			Applied.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
	// A late joiner: apply what came in before the city was built.
	if ((!Pending.IsEmpty() || !PendingHits.IsEmpty() || AppliedCollapses.Num() < Collapses.Num()) && FindCity() && City->IsGeometryBuilt())
	{
		TArray<FFTOBrokenPiece> Waiting = MoveTemp(Pending);
		Pending.Reset();
		PendingKeys.Reset();
		for (const FFTOBrokenPiece& Piece : Waiting)
		{
			Apply(Piece);
		}
		TArray<FFTOWallHit> WaitingHits = MoveTemp(PendingHits);
		PendingHits.Reset();
		for (const FFTOWallHit& Hit : WaitingHits)
		{
			ShowWallHit(Hit);
		}
		OnRep_Collapses();
	}
}
