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
 * -> upper-body aim (weapons) with pitch bent through the spine and head.
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
	UAnimSequence* AimPistol = nullptr;
	UAnimSequence* AimRifle = nullptr;
	TMap<EFTOAnimAction, UAnimSequence*> ActionClips;

	// Inputs, written on the game thread each frame
	float Speed = 0.f;
	bool bInAir = false;
	EFTOAnimAction Action = EFTOAnimAction::None;
	EFTOAimPose Aim = EFTOAimPose::None;
	float AimPitch = 0.f;

	// Ground speeds (cm/s) at which the walk and run clips' strides match 1:1 with no sliding.
	float WalkReferenceSpeed = 200.f;
	float RunReferenceSpeed = 475.f;

private:
	void Sample(UAnimSequence* Sequence, float Time, FPoseContext& Out) const;
	static void Blend(FPoseContext& InOut, const FPoseContext& Other, float Alpha);
	void ApplyAimLayer(FPoseContext& Output);
	UAnimSequence* ClipFor(EFTOAnimAction InAction) const;

	float SmoothedSpeed = 0.f;
	float IdleTime = 0.f;
	float LocoPhase = 0.f;		// shared by walk and run so feet stay in sync while blending
	float AirTime = 0.f;
	float AirWeight = 0.f;
	float ActionTime = 0.f;
	float ActionWeight = 0.f;
	EFTOAnimAction ShownAction = EFTOAnimAction::None;
	float AimTime = 0.f;
	float AimWeight = 0.f;
	float SmoothedPitch = 0.f;
	EFTOAimPose ShownAim = EFTOAimPose::None;
};

/**
 * Native animation for every FTO character. Any skeletal mesh on the officer skeleton can use it;
 * the owning actor implements IFTOAnimatedActor to say what it's doing.
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
	UPROPERTY(EditAnywhere, Category="Clips") TObjectPtr<UAnimSequence> AimPistolClip;
	UPROPERTY(EditAnywhere, Category="Clips") TObjectPtr<UAnimSequence> AimRifleClip;
	/** One clip per EFTOAnimAction (A_Officer_<ActionName>). */
	UPROPERTY(EditAnywhere, Category="Clips") TMap<EFTOAnimAction, TObjectPtr<UAnimSequence>> ActionClips;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
