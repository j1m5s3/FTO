#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/FTOAnimatedActor.h"
#include "FTOCharacterAnimInstance.generated.h"

class UAnimSequence;

/**
 * Worker-thread side of UFTOCharacterAnimInstance. Samples the clips directly and blends
 * them per bone, so characters animate without any Animation Blueprint asset.
 *
 * Layers, bottom to top: locomotion (idle/walk/run by speed) -> airborne -> full-body action
 * -> upper-body pose (a weapon up, or hands cuffed behind) with aim pitch bent through the spine and head.
 */
struct FFTOCharacterAnimProxy : public FAnimInstanceProxy
{
	FFTOCharacterAnimProxy() = default;
	explicit FFTOCharacterAnimProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	// Clips (kept alive by the anim instance's UPROPERTYs)
	UAnimSequence* Idle = nullptr;
	UAnimSequence* Walk = nullptr;
	UAnimSequence* Run = nullptr;
	UAnimSequence* Jump = nullptr;
	UAnimSequence* Fall = nullptr;
	UAnimSequence* AimPistol = nullptr;
	UAnimSequence* AimRifle = nullptr;
	UAnimSequence* HandsBehind = nullptr;
	TMap<EFTOAnimAction, UAnimSequence*> ActionClips;

	// Inputs, written on the game thread each frame
	float Speed = 0.f;
	bool bInAir = false;
	EFTOAnimAction Action = EFTOAnimAction::None;
	/** Changes whenever Action starts over. */
	uint32 Serial = 0;
	EFTOAimPose Aim = EFTOAimPose::None;
	float AimPitch = 0.f;

	// Ground speeds (cm/s) at which the walk and run clips' strides match 1:1 with no sliding (Epic's walk and jog).
	float WalkReferenceSpeed = 300.f;
	float RunReferenceSpeed = 600.f;

	/** A pose to ease out of (local space, one transform per mesh bone), e.g. where a ragdoll came to rest. */
	TArray<FTransform> FromPose;
	/** How much of FromPose still shows, fading to 0 at FromFadeRate per second. */
	float FromWeight = 0.f;
	float FromFadeRate = 0.f;

	/** Show Action fully from this frame on, with no fade in (someone who appears already mid-pose). */
	void SnapToAction(EFTOAnimAction InAction);

private:
	void Sample(UAnimSequence* Sequence, float Time, FPoseContext& Out) const;
	static void Blend(FPoseContext& InOut, const FPoseContext& Other, float Alpha);
	void ApplyAimLayer(FPoseContext& Output);
	/** Lean the chest, neck and head forward (negative) or back by Degrees in all, about the body's side-to-side axis. */
	void Bend(FPoseContext& Output, float Degrees) const;
	UAnimSequence* ClipFor(EFTOAnimAction InAction) const;
	UAnimSequence* ClipFor(EFTOAimPose InAim) const;

	float SmoothedSpeed = 0.f;
	float IdleTime = 0.f;
	float LocoPhase = 0.f;		// shared by walk and run so feet stay in sync while blending
	float AirTime = 0.f;
	float AirWeight = 0.f;
	float ActionTime = 0.f;
	float ActionWeight = 0.f;
	EFTOAnimAction ShownAction = EFTOAnimAction::None;
	uint32 ShownSerial = 0;
	/** The action being crossfaded out of (straight into the next, without standing up in between). */
	EFTOAnimAction FadingAction = EFTOAnimAction::None;
	float FadingTime = 0.f;
	/** 0 = all FadingAction, 1 = all ShownAction. */
	float CrossAlpha = 1.f;
	float AimTime = 0.f;
	float AimWeight = 0.f;
	float SmoothedPitch = 0.f;
	EFTOAimPose ShownAim = EFTOAimPose::None;
};

/**
 * Native animation for every FTO character. Any skeletal mesh on Epic's mannequin skeleton can use it (walking,
 * running, jumping and the weapon poses are Epic's own animations; the rest are ours); the owning actor implements
 * IFTOAnimatedActor to say what it's doing.
 */
UCLASS(Transient, NotBlueprintable)
class FTO_API UFTOCharacterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UFTOCharacterAnimInstance();

	UPROPERTY(EditAnywhere, Category="Clips") TObjectPtr<UAnimSequence> IdleClip;
	UPROPERTY(EditAnywhere, Category="Clips") TObjectPtr<UAnimSequence> WalkClip;
	UPROPERTY(EditAnywhere, Category="Clips") TObjectPtr<UAnimSequence> RunClip;
	UPROPERTY(EditAnywhere, Category="Clips") TObjectPtr<UAnimSequence> JumpClip;
	UPROPERTY(EditAnywhere, Category="Clips") TObjectPtr<UAnimSequence> FallClip;
	UPROPERTY(EditAnywhere, Category="Clips") TObjectPtr<UAnimSequence> AimPistolClip;
	UPROPERTY(EditAnywhere, Category="Clips") TObjectPtr<UAnimSequence> AimRifleClip;
	UPROPERTY(EditAnywhere, Category="Clips") TObjectPtr<UAnimSequence> HandsBehindClip;
	/** One clip per EFTOAnimAction (A_FTO_<ActionName>). */
	UPROPERTY(EditAnywhere, Category="Clips") TMap<EFTOAnimAction, TObjectPtr<UAnimSequence>> ActionClips;

	/**
	 * Start from LocalPose (one parent-relative transform per mesh bone) and ease into the animation over
	 * Duration seconds, so a character getting up from a ragdoll doesn't snap upright.
	 */
	void BlendFromPose(const TArray<FTransform>& LocalPose, float Duration);

	/** Start out fully in Action (no fade in), e.g. a suspect who appears already kneeling in cuffs. */
	void SnapToAction(EFTOAnimAction Action);

protected:
	/** The owner's knockdown component, if it has one: a blow taken (or thrown) shows over whatever they're doing. */
	TWeakObjectPtr<class UFTOKnockdownComponent> Knockdown;

	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
