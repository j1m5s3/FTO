#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FTOFootsteps.generated.h"

class ACharacter;

/**
 * Footsteps for anyone who walks: a step every stride's worth of ground covered (a longer, heavier stride running),
 * on whatever's underfoot (FTOAudio::SurfaceOf), plus a thump when a character lands a jump. Purely cosmetic: every
 * machine works it out from how the owner is moving, so nothing replicates. Crowds further off than CrowdRange stay
 * quiet (there are hundreds of them).
 */
UCLASS(ClassGroup=(FTO), meta=(BlueprintSpawnableComponent))
class FTO_API UFTOFootsteps : public UActorComponent
{
	GENERATED_BODY()

public:
	UFTOFootsteps();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Loudness of a walking step (running ones are a bit louder). */
	UPROPERTY(EditAnywhere, Category="Footsteps") float Volume = 0.7f;
	/** Ground covered per step walking, and running (faster than RunSpeed). */
	UPROPERTY(EditAnywhere, Category="Footsteps") float WalkStride = 75.f;
	UPROPERTY(EditAnywhere, Category="Footsteps") float RunStride = 120.f;
	UPROPERTY(EditAnywhere, Category="Footsteps") float RunSpeed = 380.f;
	/** Only heard within this of the local camera (0 = always): for the crowd. */
	UPROPERTY(EditAnywhere, Category="Footsteps") float CrowdRange = 0.f;

protected:
	virtual void BeginPlay() override;

	UFUNCTION() void HandleLanded(const FHitResult& Hit);

	/** Play one step (or a landing) where the owner's feet are. */
	void PlayStep(bool bRunning, bool bLanding);
	bool IsOnFeet() const;
	bool IsNearListener() const;

	UPROPERTY(Transient) TObjectPtr<ACharacter> Character;
	FVector LastLocation = FVector::ZeroVector;
	float Travelled = 0.f;
	bool bHaveLast = false;
};
