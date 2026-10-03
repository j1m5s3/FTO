#include "Weapons/FTOBallistics.h"
#include "Audio/FTOAudio.h"
#include "Art/FTOArt.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/FTOGameState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Physics/FTODebris.h"
#include "Physics/FTODestruction.h"
#include "Physics/FTOImpact.h"
#include "Physics/FTOKnockdownComponent.h"
#include "Physics/FTOVehicleDamage.h"
#include "Core/FTOCharacter.h"
#include "City/FTOTrafficCar.h"
#include "GameFramework/Pawn.h"
#include "Sound/SoundBase.h"

namespace
{
	constexpr float Gravity = 980.f;
	/** Rounds fly in steps no longer than this (each step is traced, so nothing is skipped). */
	constexpr float MaxStep = 1.f / 120.f;
	constexpr int32 NumTracers = 48;
	constexpr int32 NumPuffs = 32;

	const FLinearColor TracerColor(1.f, 0.82f, 0.4f);
	const FLinearColor WireColor(0.55f, 0.8f, 1.f);
	const FLinearColor DustColor(0.72f, 0.68f, 0.6f);
	const FLinearColor GlassColor(0.75f, 0.92f, 1.f);
	const FLinearColor SparkColor(1.f, 0.72f, 0.2f);
	const FLinearColor FlashColor(1.f, 0.8f, 0.35f);

	bool IsGlass(const UPrimitiveComponent* Component)
	{
		const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component);
		return Mesh && Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetName().EndsWith(TEXT("_Glass"));
	}
}

AController* UFTOBallistics::InstigatorOf(const FRound& Round)
{
	// Officers' rounds count against the police; a perp's stray shots are the perp's.
	const APawn* Shooter = Cast<APawn>(Round.Shooter.Get());
	return Shooter ? Shooter->GetController() : nullptr;
}

UFTOBallistics* UFTOBallistics::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UFTOBallistics>() : nullptr;
}

TStatId UFTOBallistics::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFTOBallistics, STATGROUP_Tickables);
}

void UFTOBallistics::Deinitialize()
{
	Rounds.Reset();
	Super::Deinitialize();
}

void UFTOBallistics::Fire(AActor* Shooter, EFTOWeapon Weapon, const FVector& Origin, const FVector& Aim, int32 Seed, bool bAimed, bool bAuthoritative, bool bShow)
{
	if (Weapon == EFTOWeapon::None)
	{
		return;
	}
	const FFTOWeaponSpec& Spec = FTOWeapons::Spec(Weapon);
	const float Spread = bAimed ? Spec.Spread : Spec.Spread + Spec.HipSpread;
	for (int32 Pellet = 0; Pellet < Spec.Pellets; ++Pellet)
	{
		FRound& Round = Rounds.AddDefaulted_GetRef();
		Round.Location = Round.Origin = Round.LastDrawn = Origin;
		Round.Velocity = FTOWeapons::RoundDirection(Aim, Spread, Seed, Pellet) * Spec.MuzzleVelocity;
		Round.Weapon = Weapon;
		Round.Shooter = Shooter;
		Round.bAuthoritative = bAuthoritative;
		Round.bShow = bShow;
		Round.Tracer = bShow ? TakeTracer(Weapon) : INDEX_NONE;
	}
	if (bShow)
	{
		Flash(Origin);
		if (USoundBase* Bang = FTOWeapons::ShotSound(Weapon))
		{
			UGameplayStatics::PlaySoundAtLocation(this, FTOAudio::Vary(Bang), Origin, 1.f, FMath::FRandRange(0.97f, 1.03f), 0.f, AFTOGameState::Sounds().World);
		}
	}
}

void UFTOBallistics::Tick(float DeltaTime)
{
	for (int32 i = Rounds.Num() - 1; i >= 0; --i)
	{
		FRound& Round = Rounds[i];
		// Spent last frame: its final stretch has been on screen for a frame, now it can go.
		if (Round.Weapon == EFTOWeapon::None)
		{
			ReleaseTracer(Round.Tracer);
			Rounds.RemoveAtSwap(i);
			continue;
		}
		bool bFlying = true;
		for (float Left = FMath::Min(DeltaTime, 0.1f); bFlying && Left > 0.f; Left -= MaxStep)
		{
			bFlying = Advance(Round, FMath::Min(Left, MaxStep));
		}
		if (Round.bShow)
		{
			DrawTracer(Round);
		}
		if (!bFlying)
		{
			Round.Weapon = EFTOWeapon::None;
		}
	}
	TickLooks(DeltaTime);
}

bool UFTOBallistics::Advance(FRound& Round, float Dt)
{
	// Gravity, and drag that grows with the square of the speed.
	const FFTOWeaponSpec& Spec = FTOWeapons::Spec(Round.Weapon);
	const float Speed = Round.Velocity.Size();
	Round.Velocity += (FVector(0.f, 0.f, -Gravity) - Round.Velocity * (Spec.Drag * Speed)) * Dt;
	const FVector From = Round.Location;
	const FVector To = From + Round.Velocity * Dt;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOBallistics), false, Round.Shooter.Get());
	// Fired from a car's window: not into the car itself.
	if (const AFTOCharacter* Officer = Cast<AFTOCharacter>(Round.Shooter.Get()); Officer && Officer->GetCurrentVehicle())
	{
		Params.AddIgnoredActor(Officer->GetCurrentVehicle());
	}
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_FTOProjectile, Params))
		{
			break;
		}
		if (IsGlass(Hit.GetComponent()))
		{
			// Straight through the pane, a little slower and wobblier (and the server's round shatters it).
			if (Round.bAuthoritative)
			{
				if (AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld()))
				{
					Wreckage->RoundHit(Hit.GetComponent(), Hit.Item, Hit.ImpactPoint, Round.Velocity, Round.Weapon, InstigatorOf(Round));
				}
			}
			Round.Velocity *= 0.8f;
			Params.AddIgnoredComponent(Hit.GetComponent());
			if (Round.bShow)
			{
				Puff(Hit.ImpactPoint, GlassColor, 14.f, 0.3f, 1.f);
			}
			continue;
		}
		Round.Travelled += FVector::Dist(From, Hit.ImpactPoint);
		Round.Location = Hit.ImpactPoint;
		return Land(Round, Hit);
	}
	Round.Travelled += FVector::Dist(From, To);
	Round.Location = To;
	return Round.Travelled < Spec.Range && Round.Velocity.SizeSquared() > FMath::Square(1500.f);
}

bool UFTOBallistics::Land(FRound& Round, const FHitResult& Hit)
{
	const FFTOWeaponSpec& Spec = FTOWeapons::Spec(Round.Weapon);
	const FVector Along = Round.Velocity.GetSafeNormal();
	AActor* Victim = Hit.GetActor();
	UPrimitiveComponent* Part = Hit.GetComponent();

	// Somebody: the round stops in them, and the server's copy decides what happens to them. A body already on the
	// floor just takes the shove.
	if (Victim && Victim->FindComponentByClass<UFTOKnockdownComponent>())
	{
		if (Round.bAuthoritative)
		{
			FTOImpact::Shot(Victim, Round.Velocity, Round.Weapon, Round.Shooter.Get());
		}
		if (Part && Part->IsSimulatingPhysics())
		{
			Part->AddImpulse(Along * Spec.Push, Hit.BoneName, true);
		}
		return false;
	}
	if (Part && Part->IsSimulatingPhysics())
	{
		Part->AddImpulse(Along * Spec.Push * 0.5f, Hit.BoneName, true);
	}

	// Cars take the damage and the city's breakables give way (the server's round decides both); whatever it is
	// keeps a pock mark (a car carries its holes with it).
	if (Round.bAuthoritative)
	{
		if (UFTOVehicleDamage* Car = Victim ? Victim->FindComponentByClass<UFTOVehicleDamage>() : nullptr)
		{
			Car->ApplyDamage(Spec.CarDamage, Hit.ImpactPoint, InstigatorOf(Round));
			if (AFTOTrafficCar* Traffic = Cast<AFTOTrafficCar>(Car->GetOwner()); Traffic && !Spec.bStun) // (a dart doesn't burst a tyre)
			{
				Traffic->RoundHit(Hit.ImpactPoint, InstigatorOf(Round));
			}
		}
		else if (AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld()))
		{
			Wreckage->RoundHit(Part, Hit.Item, Hit.ImpactPoint, Round.Velocity, Round.Weapon, InstigatorOf(Round));
		}
	}
	if (Round.bShow && !Spec.bStun)
	{
		if (UFTODebris* Debris = UFTODebris::Get(GetWorld()))
		{
			Debris->BulletHole(Hit.ImpactPoint, Hit.ImpactNormal, Part);
		}
	}

	// Walls and cars: met at a shallow angle a round glances off (once), otherwise it stops dead.
	const float HeadOn = -FVector::DotProduct(Along, Hit.ImpactNormal);
	if (HeadOn < 0.26f && Round.Bounces == 0 && !Spec.bStun && Round.Velocity.SizeSquared() > FMath::Square(12000.f))
	{
		++Round.Bounces;
		FRandomStream Scatter(GetTypeHash(Hit.ImpactPoint.GridSnap(10.f)));
		const FVector Mirror = Round.Velocity - 2.f * FVector::DotProduct(Round.Velocity, Hit.ImpactNormal) * Hit.ImpactNormal;
		Round.Velocity = Scatter.VRandCone(Mirror.GetSafeNormal(), FMath::DegreesToRadians(8.f)) * Mirror.Size() * 0.45f;
		Round.Location = Hit.ImpactPoint + Hit.ImpactNormal * 2.f;
		if (Round.bShow)
		{
			Puff(Hit.ImpactPoint, SparkColor, 10.f, 0.18f, 6.f);
			UGameplayStatics::PlaySoundAtLocation(this, FTOAudio::Vary(AFTOGameState::Sounds().Ricochet), Hit.ImpactPoint, 0.8f, Scatter.FRandRange(0.85f, 1.2f), 0.f,
				AFTOGameState::Sounds().World);
		}
		return true;
	}
	if (Round.bShow)
	{
		Puff(Hit.ImpactPoint + Hit.ImpactNormal * 3.f, DustColor, 18.f, 0.35f);
	}
	return false;
}

// ------------------------------------------------------------------------------------------
// Looks
// ------------------------------------------------------------------------------------------

AActor* UFTOBallistics::GetFXHost()
{
	if (FXHost)
	{
		return FXHost;
	}
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	FXHost = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	USceneComponent* Root = NewObject<USceneComponent>(FXHost, TEXT("Root"));
	FXHost->SetRootComponent(Root);
	Root->RegisterComponent();

	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, FTOArt::BaseMaterialPath);
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	auto Make = [this, Root](UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(FXHost);
		Piece->SetupAttachment(Root);
		Piece->SetStaticMesh(Mesh);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetCastShadow(false);
		Piece->SetUsingAbsoluteLocation(true);
		Piece->SetUsingAbsoluteRotation(true);
		Piece->SetUsingAbsoluteScale(true);
		Piece->SetVisibility(false);
		Piece->RegisterComponent();
		return Piece;
	};
	for (int32 i = 0; i < NumTracers; ++i)
	{
		UStaticMeshComponent* Streak = Make(Cylinder);
		Tracers.Add(Streak);
		TracerMaterials.Add(FTOArt::ApplyColor(Streak, BaseMaterial, TracerColor, 8.f));
		TracerInUse.Add(false);
	}
	for (int32 i = 0; i < NumPuffs; ++i)
	{
		UStaticMeshComponent* Cloud = Make(Sphere);
		PuffMeshes.Add(Cloud);
		PuffMaterials.Add(FTOArt::ApplyColor(Cloud, BaseMaterial, DustColor, 0.f));
		Puffs.AddDefaulted();
	}
	FlashLight = NewObject<UPointLightComponent>(FXHost);
	FlashLight->SetupAttachment(Root);
	FlashLight->SetUsingAbsoluteLocation(true);
	FlashLight->SetLightColor(FlashColor);
	FlashLight->SetAttenuationRadius(600.f);
	FlashLight->SetCastShadows(false);
	FlashLight->SetIntensity(0.f);
	FlashLight->RegisterComponent();
	return FXHost;
}

int32 UFTOBallistics::TakeTracer(EFTOWeapon Weapon)
{
	GetFXHost();
	const int32 Free = TracerInUse.IndexOfByKey(false);
	if (Free != INDEX_NONE)
	{
		TracerInUse[Free] = true;
		const bool bWire = Weapon == EFTOWeapon::Taser;
		FTOArt::SetColor(TracerMaterials[Free], bWire ? WireColor : TracerColor, bWire ? 2.f : 8.f);
	}
	return Free;
}

void UFTOBallistics::ReleaseTracer(int32 Index)
{
	if (Tracers.IsValidIndex(Index))
	{
		Tracers[Index]->SetVisibility(false);
		TracerInUse[Index] = false;
	}
}

void UFTOBallistics::DrawTracer(FRound& Round)
{
	if (!Tracers.IsValidIndex(Round.Tracer))
	{
		return;
	}
	UStaticMeshComponent* Streak = Tracers[Round.Tracer];
	// A taser probe trails its wire back to the muzzle; a bullet draws the stretch it covered this frame (a streak of
	// at least a metre or so, never reaching back past the muzzle).
	const bool bWire = Round.Weapon == EFTOWeapon::Taser;
	const FVector Head = Round.Location;
	FVector Tail = bWire ? Round.Origin : Round.LastDrawn;
	FVector Along = Head - Tail;
	float Length = Along.Size();
	if (!bWire)
	{
		Length = FMath::Min(FMath::Clamp(Length, 120.f, 600.f), Round.Travelled);
		Tail = Head - Along.GetSafeNormal() * Length;
	}
	Round.LastDrawn = Head;
	if (Length < 2.f || Along.IsNearlyZero())
	{
		Streak->SetVisibility(false);
		return;
	}
	const float Thickness = bWire ? 0.005f : (Round.Weapon == EFTOWeapon::Shotgun ? 0.012f : 0.02f);
	Streak->SetWorldLocationAndRotation((Head + Tail) * 0.5f, FRotationMatrix::MakeFromZ(Along.GetSafeNormal()).Rotator());
	Streak->SetWorldScale3D(FVector(Thickness, Thickness, Length / 100.f));
	Streak->SetVisibility(true);
}

void UFTOBallistics::Puff(const FVector& At, const FLinearColor& Color, float Size, float Life, float Glow)
{
	GetFXHost();
	const int32 Index = NextPuff;
	NextPuff = (NextPuff + 1) % NumPuffs;
	Puffs[Index] = { 0.f, Life, Size };
	FTOArt::SetColor(PuffMaterials[Index], Color, Glow);
	PuffMeshes[Index]->SetWorldLocation(At);
	PuffMeshes[Index]->SetWorldScale3D(FVector(Size * 0.004f));
	PuffMeshes[Index]->SetVisibility(true);
}

void UFTOBallistics::Flash(const FVector& At)
{
	GetFXHost();
	Puff(At, FlashColor, 16.f, 0.06f, 14.f);
	FlashLight->SetWorldLocation(At);
	FlashLight->SetIntensity(6000.f);
	FlashUntil = GetWorld()->GetTimeSeconds() + 0.05f;
}

void UFTOBallistics::TickLooks(float DeltaTime)
{
	if (!FXHost)
	{
		return;
	}
	for (int32 i = 0; i < Puffs.Num(); ++i)
	{
		FPuff& Cloud = Puffs[i];
		if (Cloud.Life <= 0.f)
		{
			continue;
		}
		Cloud.Age += DeltaTime;
		if (Cloud.Age >= Cloud.Life)
		{
			Cloud.Life = 0.f;
			PuffMeshes[i]->SetVisibility(false);
			continue;
		}
		// Billow out fast, then shrink away.
		const float T = Cloud.Age / Cloud.Life;
		const float Grow = T < 0.3f ? FMath::Lerp(0.4f, 1.f, T / 0.3f) : FMath::Lerp(1.f, 0.2f, (T - 0.3f) / 0.7f);
		PuffMeshes[i]->SetWorldScale3D(FVector(Cloud.Size * 0.01f * Grow));
	}
	if (FlashLight && FlashUntil > 0.f && GetWorld()->GetTimeSeconds() >= FlashUntil)
	{
		FlashLight->SetIntensity(0.f);
		FlashUntil = 0.f;
	}
}
