#include "Physics/FTODebris.h"
#include "Art/FTOArt.h"
#include "Components/AudioComponent.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/FTOGameState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Physics/FTODestruction.h"

namespace
{
	/** Seconds a piece spends shrinking away at the end of its life. */
	constexpr float ShrinkSeconds = 0.6f;
	const FLinearColor WaterColor(0.55f, 0.8f, 1.f);
}

UFTODebris* UFTODebris::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UFTODebris>() : nullptr;
}

TStatId UFTODebris::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFTODebris, STATGROUP_Tickables);
}

void UFTODebris::Deinitialize()
{
	for (FFountain& Spout : Fountains)
	{
		if (UAudioComponent* Sound = Spout.Sound.Get())
		{
			Sound->Stop();
		}
	}
	Fountains.Reset();
	Super::Deinitialize();
}

AActor* UFTODebris::GetHost()
{
	if (Host)
	{
		return Host;
	}
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Host = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	USceneComponent* Root = NewObject<USceneComponent>(Host, TEXT("Root"));
	Host->SetRootComponent(Root);
	Root->RegisterComponent();

	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, FTOArt::BaseMaterialPath);
	GlassMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/FTO/Materials/M_FTOGlass.M_FTOGlass"));
	DecalMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/FTO/Materials/M_FTODecal.M_FTODecal"));
	Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	return Host;
}

// ------------------------------------------------------------------------------------------
// Rigid bodies
// ------------------------------------------------------------------------------------------

int32 UFTODebris::TakePiece()
{
	GetHost();
	int32 Free = PieceLives.IndexOfByPredicate([](const FPieceLife& Life) { return !Life.bInUse; });
	if (Free == INDEX_NONE && Pieces.Num() < MaxPieces)
	{
		// Another body for the pool: physics only, and only against the world and other debris (it never trips
		// anyone up or stops a car, and bullets fly straight through it).
		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(Host);
		Piece->SetupAttachment(Host->GetRootComponent());
		Piece->SetUsingAbsoluteLocation(true);
		Piece->SetUsingAbsoluteRotation(true);
		Piece->SetUsingAbsoluteScale(true);
		Piece->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		Piece->SetCollisionObjectType(ECC_PhysicsBody);
		Piece->SetCollisionResponseToAllChannels(ECR_Ignore);
		Piece->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Piece->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		Piece->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Piece->SetGenerateOverlapEvents(false);
		Piece->SetCastShadow(true);
		Piece->SetVisibility(false);
		Piece->RegisterComponent();
		Pieces.Add(Piece);
		PieceMaterials.Add(nullptr);
		PieceLives.AddDefaulted();
		Free = Pieces.Num() - 1;
	}
	if (Free == INDEX_NONE)
	{
		// All in use: the one that's been flying longest makes way.
		float Oldest = -1.f;
		for (int32 i = 0; i < PieceLives.Num(); ++i)
		{
			if (PieceLives[i].Age > Oldest)
			{
				Oldest = PieceLives[i].Age;
				Free = i;
			}
		}
		ReleasePiece(Free);
	}
	return Free;
}

void UFTODebris::ReleasePiece(int32 Index)
{
	UStaticMeshComponent* Piece = Pieces[Index];
	Piece->SetSimulatePhysics(false);
	Piece->SetVisibility(false);
	PieceLives[Index].bInUse = false;
}

void UFTODebris::Throw(UStaticMesh* Mesh, const FTransform& Where, const FLinearColor& Color, const FVector& Velocity, const FVector& Spin,
	float Life, float Mass, UMaterialInterface* Material)
{
	if (!Mesh || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const int32 Index = TakePiece();
	UStaticMeshComponent* Piece = Pieces[Index];
	Piece->SetStaticMesh(Mesh);
	Piece->EmptyOverrideMaterials();
	if (Material)
	{
		for (int32 Slot = 0; Slot < Piece->GetNumMaterials(); ++Slot)
		{
			Piece->SetMaterial(Slot, Material);
		}
	}
	else
	{
		TObjectPtr<UMaterialInstanceDynamic>& Paint = PieceMaterials[Index];
		if (!Paint)
		{
			Paint = UMaterialInstanceDynamic::Create(BaseMaterial, Piece);
		}
		FTOArt::SetColor(Paint, Color);
		Piece->SetMaterial(FTOArt::BodySlot(Piece), Paint);
	}
	Piece->SetWorldTransform(Where, false, nullptr, ETeleportType::TeleportPhysics);
	Piece->SetVisibility(true);
	Piece->SetSimulatePhysics(true);
	if (Mass > 0.f)
	{
		Piece->SetMassOverrideInKg(NAME_None, Mass, true);
	}
	Piece->SetPhysicsLinearVelocity(Velocity);
	Piece->SetPhysicsAngularVelocityInDegrees(Spin);
	Piece->WakeRigidBody();

	FPieceLife& Record = PieceLives[Index];
	Record.Age = 0.f;
	Record.Life = Life;
	Record.Scale = Where.GetScale3D();
	Record.bInUse = true;
}

void UFTODebris::Chunks(const FVector& At, const FVector& Push, const FLinearColor& Color, int32 Count, float Size, float Life)
{
	GetHost();
	const FVector Dir = Push.GetSafeNormal();
	const float Speed = FMath::Clamp(Push.Size(), 200.f, 1400.f);
	for (int32 i = 0; i < Count; ++i)
	{
		// Chunky and uneven: each one a slightly different box, off in its own direction.
		const FVector Scale = FVector(FMath::FRandRange(0.6f, 1.2f), FMath::FRandRange(0.5f, 1.f), FMath::FRandRange(0.4f, 0.9f)) * (Size / 100.f);
		const FVector Out = (Dir + FMath::VRand() * 0.8f + FVector(0.f, 0.f, 0.6f)).GetSafeNormal();
		Throw(Cube, FTransform(FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f), At + FMath::VRand() * Size * 0.5f, Scale),
			Color, Out * Speed * FMath::FRandRange(0.5f, 1.f), FMath::VRand() * FMath::FRandRange(200.f, 700.f), Life * FMath::FRandRange(0.8f, 1.2f));
	}
}

void UFTODebris::Shards(const FTransform& Pane, const FBox& LocalBounds, const FVector& Hit, const FVector& Push, int32 Count)
{
	GetHost();
	const FVector Dir = Push.GetSafeNormal();
	for (int32 i = 0; i < Count; ++i)
	{
		// Thin slivers from all over the pane, the ones nearest the hit flying hardest.
		const FVector Local(FMath::FRandRange(LocalBounds.Min.X, LocalBounds.Max.X), FMath::FRandRange(LocalBounds.Min.Y, LocalBounds.Max.Y),
			FMath::FRandRange(LocalBounds.Min.Z, LocalBounds.Max.Z));
		const FVector Spot = Pane.TransformPosition(Local);
		const float Near = FMath::Clamp(1.f - FVector::Dist(Spot, Hit) / 250.f, 0.2f, 1.f);
		const FVector Scale(FMath::FRandRange(0.08f, 0.22f), FMath::FRandRange(0.06f, 0.18f), 0.008f);
		const FVector Velocity = Dir * FMath::FRandRange(150.f, 550.f) * Near + FMath::VRand() * 120.f + FVector(0.f, 0.f, FMath::FRandRange(0.f, 150.f));
		Throw(Cube, FTransform(FRotator(FMath::FRandRange(-90.f, 90.f), FMath::FRandRange(0.f, 360.f), FMath::FRandRange(-90.f, 90.f)), Spot, Scale),
			FLinearColor::White, Velocity, FMath::VRand() * FMath::FRandRange(300.f, 900.f), FMath::FRandRange(2.5f, 4.f), 0.2f, GlassMaterial);
	}
}

void UFTODebris::TickPieces(float DeltaTime)
{
	for (int32 i = 0; i < PieceLives.Num(); ++i)
	{
		FPieceLife& Life = PieceLives[i];
		if (!Life.bInUse)
		{
			continue;
		}
		Life.Age += DeltaTime;
		const float Left = Life.Life - Life.Age;
		if (Left <= 0.f)
		{
			ReleasePiece(i);
		}
		else if (Left < ShrinkSeconds)
		{
			Pieces[i]->SetWorldScale3D(Life.Scale * FMath::Max(0.01f, Left / ShrinkSeconds));
		}
	}
}

int32 UFTODebris::NumPieces() const
{
	int32 Count = 0;
	for (const FPieceLife& Life : PieceLives)
	{
		Count += Life.bInUse ? 1 : 0;
	}
	return Count;
}

// ------------------------------------------------------------------------------------------
// Bullet holes
// ------------------------------------------------------------------------------------------

void UFTODebris::BulletHole(const FVector& At, const FVector& Normal, USceneComponent* On)
{
	if (GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	GetHost();
	// (Nothing in street furniture that breaks: the hole would be left hanging in the air when it goes.)
	if (!DecalMaterial || Normal.IsNearlyZero() || (On && AFTODestruction::KindOf(Cast<UPrimitiveComponent>(On)) != EFTOBreakKind::None))
	{
		return;
	}
	Holes.RemoveAll([](const TWeakObjectPtr<UDecalComponent>& Hole) { return !Hole.IsValid(); });
	while (Holes.Num() >= MaxHoles)
	{
		if (UDecalComponent* Oldest = Holes[0].Get())
		{
			Oldest->DestroyComponent();
		}
		Holes.RemoveAt(0);
	}

	// A decal projects along its X: into the surface, spun at random so they don't all match.
	// Moving things (cars) own their holes, so they go when the car does.
	const bool bMoving = On && On->Mobility == EComponentMobility::Movable && On->GetOwner();
	UDecalComponent* Hole = NewObject<UDecalComponent>(bMoving ? On->GetOwner() : Host.Get());
	Hole->SetDecalMaterial(DecalMaterial);
	Hole->DecalSize = FVector(8.f, 5.f, 5.f) * FMath::FRandRange(0.8f, 1.2f);
	Hole->SetFadeScreenSize(0.002f);
	Hole->SetupAttachment(Host->GetRootComponent());
	Hole->SetUsingAbsoluteLocation(true);
	Hole->SetUsingAbsoluteRotation(true);
	Hole->RegisterComponent();
	FRotator Facing = (-Normal).Rotation();
	Facing.Roll = FMath::FRandRange(0.f, 360.f);
	Hole->SetWorldLocationAndRotation(At, Facing);
	// Moving things (cars) carry their holes with them.
	if (bMoving)
	{
		Hole->SetUsingAbsoluteLocation(false);
		Hole->SetUsingAbsoluteRotation(false);
		Hole->AttachToComponent(On, FAttachmentTransformRules::KeepWorldTransform);
	}
	Hole->SetFadeOut(20.f, 4.f, false);
	Holes.Add(Hole);
}

int32 UFTODebris::NumHoles() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<UDecalComponent>& Hole : Holes)
	{
		Count += Hole.IsValid() ? 1 : 0;
	}
	return Count;
}

// ------------------------------------------------------------------------------------------
// Fountains
// ------------------------------------------------------------------------------------------

void UFTODebris::Fountain(const FVector& At, float Seconds)
{
	if (GetWorld()->GetNetMode() == NM_DedicatedServer || Seconds <= 0.f)
	{
		return;
	}
	GetHost();
	const float Now = GetWorld()->GetTimeSeconds();
	// The same hydrant again just keeps it going.
	for (FFountain& Spout : Fountains)
	{
		if (FVector::DistSquared(Spout.At, At) < FMath::Square(50.f))
		{
			Spout.Until = FMath::Max(Spout.Until, Now + Seconds);
			return;
		}
	}
	if (Fountains.Num() >= 4)
	{
		return;
	}

	// Enough droplets for one more fountain (each set is re-used when its fountain dries up).
	if (!WaterMaterial && GlassMaterial)
	{
		WaterMaterial = UMaterialInstanceDynamic::Create(GlassMaterial, Host);
		WaterMaterial->SetVectorParameterValue(TEXT("Color"), WaterColor);
		WaterMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.6f);
	}
	TArray<int32> Taken;
	for (const FFountain& Spout : Fountains)
	{
		Taken.Add(Spout.FirstDrop);
	}
	int32 First = 0;
	while (Taken.Contains(First))
	{
		First += DropsPerFountain;
	}
	while (DropMeshes.Num() < First + DropsPerFountain)
	{
		UStaticMeshComponent* Drop = NewObject<UStaticMeshComponent>(Host);
		Drop->SetupAttachment(Host->GetRootComponent());
		Drop->SetUsingAbsoluteLocation(true);
		Drop->SetUsingAbsoluteScale(true);
		Drop->SetStaticMesh(Sphere);
		Drop->SetMaterial(0, WaterMaterial);
		Drop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Drop->SetCastShadow(false);
		Drop->SetVisibility(false);
		Drop->RegisterComponent();
		DropMeshes.Add(Drop);
		Drops.AddDefaulted();
	}

	FFountain& Spout = Fountains.AddDefaulted_GetRef();
	Spout.At = At;
	Spout.Until = Now + Seconds;
	Spout.FirstDrop = First;
	for (int32 i = 0; i < DropsPerFountain; ++i)
	{
		// Staggered so the column is full from the start.
		Drops[First + i].Delay = i * 0.05f;
		Drops[First + i].Location = At;
		DropMeshes[First + i]->SetVisibility(false);
	}
	if (USoundBase* Gush = AFTOGameState::Sounds().GushLoop)
	{
		Spout.Sound = UGameplayStatics::SpawnSoundAtLocation(this, Gush, At, FRotator::ZeroRotator, 0.7f, 1.f, 0.f, AFTOGameState::Sounds().World);
	}
}

void UFTODebris::TickFountains(float DeltaTime)
{
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 f = Fountains.Num() - 1; f >= 0; --f)
	{
		FFountain& Spout = Fountains[f];
		const bool bRunning = Now < Spout.Until;
		bool bAnyAirborne = false;
		for (int32 i = Spout.FirstDrop; i < Spout.FirstDrop + DropsPerFountain; ++i)
		{
			FDrop& Drop = Drops[i];
			UStaticMeshComponent* Mesh = DropMeshes[i];
			if (Drop.Delay > 0.f)
			{
				Drop.Delay -= DeltaTime;
				continue;
			}
			// Up out of the stub and back down, then round again while the water's on.
			Drop.Velocity.Z -= 980.f * DeltaTime;
			Drop.Location += Drop.Velocity * DeltaTime;
			if (Drop.Location.Z < Spout.At.Z || !Mesh->IsVisible())
			{
				if (!bRunning)
				{
					Mesh->SetVisibility(false);
					continue;
				}
				Drop.Location = Spout.At;
				Drop.Velocity = FVector(FMath::FRandRange(-90.f, 90.f), FMath::FRandRange(-90.f, 90.f), FMath::FRandRange(480.f, 620.f));
				Mesh->SetVisibility(true);
			}
			bAnyAirborne = true;
			const float Size = 0.1f + 0.06f * FMath::Clamp(Drop.Velocity.Z / 600.f, 0.f, 1.f);
			Mesh->SetWorldLocation(Drop.Location);
			Mesh->SetWorldScale3D(FVector(Size, Size, Size * 1.4f));
		}
		if (!bRunning && !bAnyAirborne)
		{
			if (UAudioComponent* Sound = Spout.Sound.Get())
			{
				Sound->FadeOut(0.5f, 0.f);
			}
			Fountains.RemoveAtSwap(f);
		}
	}
}

void UFTODebris::Tick(float DeltaTime)
{
	if (!Host)
	{
		return;
	}
	TickPieces(DeltaTime);
	TickFountains(DeltaTime);
}
