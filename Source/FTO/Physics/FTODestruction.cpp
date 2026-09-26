#include "Physics/FTODestruction.h"
#include "City/FTOCityGenerator.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Core/FTOGameState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Physics/FTODebris.h"
#include "FTO.h"

namespace
{
	/** A break older than this (a late joiner catching up) just leaves the gap, no fireworks. */
	constexpr float FreshSeconds = 3.f;

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
		default:                      return 0.f;
		}
	}
}

void FFTOBrokenPiece::PostReplicatedAdd(const FFTOBrokenList& List)
{
	if (List.Owner)
	{
		List.Owner->Apply(*this);
	}
}

AFTODestruction::AFTODestruction()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);
	Broken.Owner = this;
}

void AFTODestruction::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTODestruction, Broken);
}

void AFTODestruction::BeginPlay()
{
	Super::BeginPlay();
	Broken.Owner = this;
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

bool AFTODestruction::IsBroken(FName Component, int32 Instance) const
{
	const TPair<FName, int32> Key(Component, Instance);
	return BrokenKeys.Contains(Key) || Applied.Contains(Key);
}

bool AFTODestruction::Break(UPrimitiveComponent* Component, int32 Instance, const FVector& Hit, const FVector& Push, AController* ByWhom)
{
	check(HasAuthority());
	const EFTOBreakKind Kind = KindOf(Component);
	const TPair<FName, int32> Key(Component ? Component->GetFName() : NAME_None, Instance);
	if (Kind == EFTOBreakKind::None || Instance == INDEX_NONE || BrokenKeys.Contains(Key))
	{
		return false;
	}
	BrokenKeys.Add(Key);
	Hits.Remove(Key);

	FFTOBrokenPiece& Piece = Broken.Items.AddDefaulted_GetRef();
	Piece.Component = Key.Key;
	Piece.Instance = Instance;
	Piece.Hit = Hit;
	Piece.Push = Push;
	Piece.Time = GetWorld()->GetTimeSeconds();
	Broken.MarkItemDirty(Piece);
	const FFTOBrokenPiece Copy = Piece;
	Apply(Copy);

	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastPlaySound(Kind == EFTOBreakKind::Shatter ? AFTOGameState::Sounds().Glass : AFTOGameState::Sounds().Clang, Hit, 1.f);
		// The police did that? The city won't forget.
		if (Cast<APlayerController>(ByWhom))
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
	const EFTOBreakKind Kind = KindOf(Component);
	if (Kind == EFTOBreakKind::None || Instance == INDEX_NONE || FTOWeapons::Spec(Weapon).bStun)
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
	if (KindOf(Component) == EFTOBreakKind::None || Instance == INDEX_NONE)
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
	Apply(Piece);
}

void AFTODestruction::Apply(const FFTOBrokenPiece& Piece)
{
	const TPair<FName, int32> Key(Piece.Component, Piece.Instance);
	if (Applied.Contains(Key))
	{
		return;
	}
	AFTOCityGenerator* TheCity = FindCity();
	UInstancedStaticMeshComponent* ISM = TheCity ? TheCity->FindInstanced(Piece.Component) : nullptr;
	if (!ISM || !ISM->IsValidInstance(Piece.Instance))
	{
		// The city isn't built here yet (just joined): later.
		if ((!TheCity || !TheCity->IsGeometryBuilt()) &&
			!Pending.ContainsByPredicate([&Piece](const FFTOBrokenPiece& Waiting) { return Waiting.Component == Piece.Component && Waiting.Instance == Piece.Instance; }))
		{
			Pending.Add(Piece);
		}
		return;
	}
	Applied.Add(Key);

	FTransform Was;
	ISM->GetInstanceTransform(Piece.Instance, Was, true);
	FLinearColor Color = FLinearColor::White;
	const int32 First = Piece.Instance * ISM->NumCustomDataFloats;
	if (ISM->NumCustomDataFloats >= 3 && ISM->PerInstanceSMCustomData.IsValidIndex(First + 2))
	{
		Color = FLinearColor(ISM->PerInstanceSMCustomData[First], ISM->PerInstanceSMCustomData[First + 1], ISM->PerInstanceSMCustomData[First + 2]);
	}

	// Tucked away out of sight and reach (instances keep their numbers, so moving it beats removing it).
	const FTransform Gone(Was.GetRotation(), Was.GetLocation() - FVector(0.f, 0.f, 100000.f), FVector(0.001f));
	ISM->UpdateInstanceTransform(Piece.Instance, Gone, true, true, true);

	const AGameStateBase* GS = GetWorld()->GetGameState();
	// (No server clock yet, just joined: it's old news, no replay.)
	const float Age = GS ? GS->GetServerWorldTimeSeconds() - Piece.Time : TNumericLimits<float>::Max();
	const EFTOBreakKind Kind = KindOf(ISM);
	if (Age < FreshSeconds)
	{
		Show(Kind, ISM, Was, Color, Piece);
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
		Debris->Shards(Was, Bounds, Piece.Hit, Push, 18);
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

	default:
		break;
	}
}

void AFTODestruction::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// A late joiner: apply what came in before the city was built.
	if (!Pending.IsEmpty() && FindCity() && City->IsGeometryBuilt())
	{
		TArray<FFTOBrokenPiece> Waiting = MoveTemp(Pending);
		Pending.Reset();
		for (const FFTOBrokenPiece& Piece : Waiting)
		{
			Apply(Piece);
		}
	}
}
