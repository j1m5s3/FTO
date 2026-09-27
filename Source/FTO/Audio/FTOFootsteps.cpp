#include "Audio/FTOFootsteps.h"
#include "Audio/FTOAudio.h"
#include "Core/FTOGameState.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Physics/FTOKnockdownComponent.h"

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
	if (Character)
	{
		Character->LandedDelegate.AddDynamic(this, &UFTOFootsteps::HandleLanded);
	}
}

bool UFTOFootsteps::IsOnFeet() const
{
	const AActor* Owner = GetOwner();
	if (!Owner || Owner->IsHidden())
	{
		return false; // sat in a car
	}
	if (const UFTOKnockdownComponent* Knockdown = Owner->FindComponentByClass<UFTOKnockdownComponent>(); Knockdown && (Knockdown->IsDown() || Knockdown->IsDazed()))
	{
		return false;
	}
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
	// Standing still, off our feet, or teleported: start the stride afresh.
	if (!IsOnFeet() || Speed < 40.f || Moved > 400.f)
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
	if (Sound)
	{
		const float Loud = Volume * (bLanding ? 1.4f : (bRunning ? 1.15f : 1.f));
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Feet, Loud, FMath::FRandRange(0.93f, 1.07f), 0.f, AFTOGameState::Sounds().World);
	}
}
