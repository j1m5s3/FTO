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
 */
struct FFTOCharacterAnimProxy : public FAnimInstanceProxy
{
	FFTOCharacterAnimProxy() = default;
	explicit FFTOCharacterAnimProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	// Clips (owned/kept alive by the anim instance's UPROPERTYs)
	UAnimSequence* Idle = nullptr;
	UAnimSequence* Walk = nullptr;
	UAnimSequence* Run = nullptr;
	UAnimSequence* Jump = nullptr;
	UAnimSequence* Interact = nullptr;
	UAnimSequence* Cheer = nullptr;

	// Inputs, written on the game thread each frame
	float Speed = 0.f;
	bool bInAir = false;
	EFTOAnimAction Action = EFTOAnimAction::None;

	// Ground speeds (cm/s) at which the walk and run clips' strides match 1:1 with no sliding.
	float WalkReferenceSpeed = 200.f;
	float RunReferenceSpeed = 475.f;

private:
	void Sample(UAnimSequence* Sequence, float Time, FPoseContext& Out) const;
	static void Blend(FPoseContext& InOut, const FPoseContext& Other, float Alpha);

	float SmoothedSpeed = 0.f;
	float IdleTime = 0.f;
	float LocoPhase = 0.f;		// shared by walk and run so feet stay in sync while blending
	float AirTime = 0.f;
	float AirWeight = 0.f;
	float ActionTime = 0.f;
	float ActionWeight = 0.f;
	EFTOAnimAction ShownAction = EFTOAnimAction::None;
};

/**
 * Native animation for FTO characters: idle/walk/run blended by speed, with jump,
 * interact and cheer layered on top. Any skeletal mesh sharing the officer skeleton can use it.
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
	UPROPERTY(EditAnywhere, Category="Clips") TObjectPtr<UAnimSequence> InteractClip;
	UPROPERTY(EditAnywhere, Category="Clips") TObjectPtr<UAnimSequence> CheerClip;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
