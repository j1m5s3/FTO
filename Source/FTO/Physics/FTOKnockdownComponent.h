#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FTOKnockdownComponent.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

DECLARE_MULTICAST_DELEGATE(FFTOKnockdownEvent);

USTRUCT()
struct FFTOKnockdownState
{
	GENERATED_BODY()

	UPROPERTY() bool bDown = false;
	/** Launch velocity for the ragdoll (cm/s). */
	UPROPERTY() FVector_NetQuantize10 Launch = FVector::ZeroVector;
	/** Bumps every knockdown so back-to-back ones still replicate. */
	UPROPERTY() uint8 Serial = 0;
};

/**
 * Knocks any character-like actor over: its skeletal mesh goes ragdoll (Chaos physics),
 * cartoon stars circle its head, and it gets back up after a while.
 *
 * The server decides who's down and for how long (replicated state); every machine simulates
 * the ragdoll locally, since the flop itself is just for show. Owners hook OnKnockedDown /
 * OnRecovered to stop and restart their own movement.
 */
UCLASS(ClassGroup=(FTO), meta=(BlueprintSpawnableComponent))
class FTO_API UFTOKnockdownComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFTOKnockdownComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Server: flop over with this launch velocity. Duration <= 0 stays down until Recover(). */
	void Knockdown(const FVector& LaunchVelocity, float Duration);

	/** Server: get back up now. */
	void Recover();

	UFUNCTION(BlueprintPure, Category="Knockdown") bool IsDown() const { return State.bDown; }
	/** Just got up: sat on the ground seeing stars for DazedSeconds (every machine keeps its own clock). */
	bool IsDazed() const;

	/** Where the body actually is right now (pelvis while ragdolling, else the actor). */
	FVector GetBodyLocation() const;

	/** Stars keep circling for a moment after getting up. */
	UPROPERTY(EditAnywhere, Category="Knockdown") float DazedSeconds = 1.5f;

	FFTOKnockdownEvent OnKnockedDown;
	FFTOKnockdownEvent OnRecovered;

protected:
	virtual void BeginPlay() override;

	UFUNCTION() void OnRep_State();

	void StartRagdoll(const FVector& LaunchVelocity);
	void StopRagdoll();
	void EnsureStars();
	void UpdateStars(float DeltaTime);

	UPROPERTY(ReplicatedUsing=OnRep_State) FFTOKnockdownState State;

	UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Mesh;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Stars;

	FName SavedProfile;
	FTransform SavedRelative;
	bool bRagdolling = false;
	uint8 AppliedSerial = 0;
	float RecoverAt = 0.f;
	float StarsUntil = 0.f;
	float StarSpin = 0.f;
};
