#include "Audio/FTOFootsteps.h"
#include "Audio/FTOAudio.h"
#include "Animation/FTOAnimatedActor.h"
#include "Core/FTOGameState.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Physics/FTOKnockdownComponent.h"
#include "Sound/SoundConcurrency.h"
#include "UObject/StrongObjectPtr.h"

int32 UFTOFootsteps::StepCount[6] = {};

namespace
{
	/** The crowd's steps share a few voices (the nearest win), so they never crowd out a gunshot or a siren. */
	USoundConcurrency* CrowdConcurrency()
	{
		static TStrongObjectPtr<USoundConcurrency> Shared;
		if (!Shared.IsValid())
		{
			Shared.Reset(NewObject<USoundConcurrency>(GetTransientPackage(), TEXT("FTOCrowdSteps")));
			Shared->Concurrency.MaxCount = 6;
			Shared->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopFarthestThenOldest;
		}
		return Shared.Get();
	}
}

UFTOFootsteps::UFTOFootsteps()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(false);
}

void UFTOFootsteps::BeginPlay()
{
	Super::BeginPlay();
	// Nobody's listening on a dedicated server.
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
		return;
	}
	Character = Cast<ACharacter>(GetOwner());
	Knockdown = GetOwner()->FindComponentByClass<UFTOKnockdownComponent>();
	if (Character)
	{
		Character->LandedDelegate.AddDynamic(this, &UFTOFootsteps::HandleLanded);
	}
	// The crowd doesn't need a check every frame: a stride takes a good few.
	if (CrowdRange > 0.f)
	{
		SetComponentTickInterval(0.08f);
	}
}

bool UFTOFootsteps::IsOnFeet() const
{
	const AActor* Owner = GetOwner();
	if (!Owner || Owner->IsHidden())
	{
		return false;
	}
	if (Knockdown && (Knockdown->IsDown() || Knockdown->IsDazed()))
	{
		return false;
	}
	// Sat down (a chair, a car seat, the back of a cruiser) or riding along on something.
	if (Owner->GetAttachParentActor())
	{
		return false;
	}
	if (const IFTOAnimatedActor* Animated = Cast<IFTOAnimatedActor>(Owner))
	{
		switch (Animated->GetAnimAction())
		{
		case EFTOAnimAction::Sit:
		case EFTOAnimAction::Drive:
		case EFTOAnimAction::Ride:
		case EFTOAnimAction::SitCuffed:
		case EFTOAnimAction::SitHandsUp:
			return false;
		default:
			break;
		}
	}
	// (Seated in a car, an officer isn't walking: the movement mode says so.)
	return !Character || (Character->GetCharacterMovement() && Character->GetCharacterMovement()->IsMovingOnGround());
}

bool UFTOFootsteps::IsNearListener() const
{
	if (CrowdRange <= 0.f)
	{
		return true;
	}
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return false;
	}
	return FVector::DistSquared(PC->PlayerCameraManager->GetCameraLocation(), GetOwner()->GetActorLocation()) < FMath::Square(CrowdRange);
}

void UFTOFootsteps::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const FVector Here = GetOwner()->GetActorLocation();
	const float Moved = bHaveLast ? FVector::Dist2D(Here, LastLocation) : 0.f;
	LastLocation = Here;
	bHaveLast = true;
	const float Speed = DeltaTime > 0.f ? Moved / DeltaTime : 0.f;
	// Standing still, faster than anyone runs (teleported, or getting out of a car), or off our feet: start the
	// stride afresh. Cheapest checks first: the crowd's mostly standing about or far away.
	if (Speed < 40.f || Speed > 1500.f || !IsOnFeet())
	{
		Travelled = 0.f;
		return;
	}
	const bool bRunning = Speed > RunSpeed;
	Travelled += Moved;
	const float Stride = bRunning ? RunStride : WalkStride;
	if (Travelled >= Stride)
	{
		Travelled = FMath::Fmod(Travelled, Stride);
		if (IsNearListener())
		{
			PlayStep(bRunning, false);
		}
	}
}

void UFTOFootsteps::HandleLanded(const FHitResult& Hit)
{
	Travelled = 0.f;
	PlayStep(true, true);
}

void UFTOFootsteps::PlayStep(bool bRunning, bool bLanding)
{
	// What's underfoot.
	const FVector Here = GetOwner()->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOFootstep), false, GetOwner());
	FHitResult Ground;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Ground, Here, Here - FVector(0.f, 0.f, 250.f), ECC_Visibility, Params);
	const EFTOSurface Surface = bHit ? FTOAudio::SurfaceOf(Ground) : EFTOSurface::Concrete;
	const FVector Feet = bHit ? Ground.ImpactPoint : Here - FVector(0.f, 0.f, 90.f);

	USoundBase* Sound = FTOAudio::Step(Surface, bRunning);
	if (bLanding)
	{
		// A jump landing: the heavy thud on hard ground, a heavy step on anything softer.
		if (USoundBase* Thud = Surface == EFTOSurface::Concrete || Surface == EFTOSurface::Tile ? FTOAudio::Pick(TEXT("Land_Concrete")) : nullptr)
		{
			Sound = Thud;
		}
	}
	++StepCount[FMath::Clamp(int32(Surface), 0, 5)];
	if (Sound)
	{
		const float Loud = Volume * (bLanding ? 1.4f : (bRunning ? 1.15f : 1.f));
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Feet, Loud, FMath::FRandRange(0.93f, 1.07f), 0.f, AFTOGameState::Sounds().World,
			CrowdRange > 0.f ? CrowdConcurrency() : nullptr);
	}
}
