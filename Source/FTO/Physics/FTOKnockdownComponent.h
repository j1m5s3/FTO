#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Animation/FTOAnimatedActor.h"
#include "FTOKnockdownComponent.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

DECLARE_MULTICAST_DELEGATE(FFTOKnockdownEvent);

/** A short full-body move over whatever someone's doing: a punch thrown, or a blow taken (with a physical jolt). */
USTRUCT()
struct FFTOMovePlay
{
	GENERATED_BODY()

	UPROPERTY() EFTOAnimAction Action = EFTOAnimAction::None;
	UPROPERTY() float Seconds = 0.f;
	/** A blow taken: the bone it landed on and the shove (cm/s) the upper body takes (none for a move thrown). */
	UPROPERTY() FName Bone;
	UPROPERTY() FVector_NetQuantize10 Impulse = FVector::ZeroVector;
	UPROPERTY() uint8 Serial = 0;
};

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

	// ---- Blows (Combat/FTOFighting) ----
	/** Server: play Action over everything else for Seconds (a punch thrown). */
	void PlayMove(EFTOAnimAction Action, float Seconds);
	/** Server: rocked by a blow: Reaction for Seconds, the upper body jolted along Impulse (cm/s) from Bone. */
	void TakeBlow(EFTOAnimAction Reaction, float Seconds, FName Bone, const FVector& Impulse);
	/** The move or reaction showing right now (None once it's over, or while down). */
	EFTOAnimAction GetMove() const;
	uint8 GetMoveSerial() const { return Move.Serial; }
	/** Mid-punch or mid-reel: can't throw another yet. */
	bool IsBusy() const { return GetMove() != EFTOAnimAction::None; }
	/** Server: how groggy the blows have left them (0-100: at 100 they go down); added to, and the total returned. */
	float AddDaze(float Amount);
	float GetDaze() const { return Daze; }
	USkeletalMeshComponent* GetSkelMesh() const { return Mesh; }

	/** Stars keep circling for a moment after getting up. */
	UPROPERTY(EditAnywhere, Category="Knockdown") float DazedSeconds = 1.5f;

	FFTOKnockdownEvent OnKnockedDown;
	FFTOKnockdownEvent OnRecovered;

protected:
	virtual void BeginPlay() override;

	UFUNCTION() void OnRep_State();
	UFUNCTION() void OnRep_Move();
	/** Every machine: the upper body goes loose for a moment, knocked along by the blow, and the pose reels it back. */
	void StartFlinch(FName Bone, const FVector& Impulse);
	void TickFlinch(float DeltaTime);
	void StopFlinch();

	void StartRagdoll(const FVector& LaunchVelocity);
	void StopRagdoll();
	void EnsureStars();
	void UpdateStars(float DeltaTime);

	UPROPERTY(ReplicatedUsing=OnRep_State) FFTOKnockdownState State;
	UPROPERTY(ReplicatedUsing=OnRep_Move) FFTOMovePlay Move;
	/** Every machine: when the current move started (its own clock). */
	float MoveStart = -100.f;
	/** Server. */
	float Daze = 0.f;
	float LastBlow = -100.f;
	/** Every machine: how loose the upper body still is (0 once the flinch is over). */
	float Flinch = 0.f;
	FName FlinchProfile;

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
