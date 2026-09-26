#include "Animation/FTOCharacterAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	float Wrap(float Time, const UAnimSequence* Sequence)
	{
		const float Length = Sequence ? Sequence->GetPlayLength() : 0.f;
		return Length > KINDA_SMALL_NUMBER ? FMath::Fmod(Time, Length) : 0.f;
	}

	UAnimSequence* FindClip(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<UAnimSequence> Finder(Path);
		return Finder.Object;
	}
}

// ------------------------------------------------------------------------------------------
// Proxy (worker thread)
// ------------------------------------------------------------------------------------------

void FFTOCharacterAnimProxy::Update(float DeltaSeconds)
{
	FAnimInstanceProxy::Update(DeltaSeconds);

	SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, Speed, DeltaSeconds, 10.f);

	IdleTime = Wrap(IdleTime + DeltaSeconds, Idle);

	// Walk and run share one phase; the cycle rate follows how fast we're actually moving.
	const float RunAlpha = FMath::Clamp((SmoothedSpeed - WalkReferenceSpeed) / FMath::Max(1.f, RunReferenceSpeed - WalkReferenceSpeed), 0.f, 1.f);
	const float WalkLength = Walk ? Walk->GetPlayLength() : 1.f;
	const float RunLength = Run ? Run->GetPlayLength() : 1.f;
	const float CycleLength = FMath::Lerp(WalkLength, RunLength, RunAlpha);
	const float ReferenceSpeed = FMath::Lerp(WalkReferenceSpeed, RunReferenceSpeed, RunAlpha);
	const float PlayRate = FMath::Clamp(SmoothedSpeed / FMath::Max(1.f, ReferenceSpeed), 0.4f, 1.6f);
	LocoPhase = FMath::Fmod(LocoPhase + DeltaSeconds * PlayRate / FMath::Max(0.05f, CycleLength), 1.f);

	AirWeight = FMath::FInterpTo(AirWeight, bInAir ? 1.f : 0.f, DeltaSeconds, 12.f);
	AirTime = bInAir ? Wrap(AirTime + DeltaSeconds, Jump) : 0.f;

	// Fade actions in and out; switching action restarts it once the old one has faded.
	if (Action != EFTOAnimAction::None && (ShownAction == Action || ActionWeight < 0.05f))
	{
		if (ShownAction != Action)
		{
			ShownAction = Action;
			ActionTime = 0.f;
		}
		ActionWeight = FMath::FInterpTo(ActionWeight, 1.f, DeltaSeconds, 8.f);
	}
	else
	{
		ActionWeight = FMath::FInterpTo(ActionWeight, 0.f, DeltaSeconds, 8.f);
	}
	ActionTime = Wrap(ActionTime + DeltaSeconds, ShownAction == EFTOAnimAction::Cheer ? Cheer : Interact);
}

void FFTOCharacterAnimProxy::Sample(UAnimSequence* Sequence, float Time, FPoseContext& Out) const
{
	if (!Sequence)
	{
		Out.ResetToRefPose();
		return;
	}
	FAnimationPoseData PoseData(Out);
	Sequence->GetAnimationPose(PoseData, FAnimExtractContext(double(Wrap(Time, Sequence)), false));
}

void FFTOCharacterAnimProxy::Blend(FPoseContext& InOut, const FPoseContext& Other, float Alpha)
{
	if (Alpha <= 0.f)
	{
		return;
	}
	for (const FCompactPoseBoneIndex Bone : InOut.Pose.ForEachBoneIndex())
	{
		FTransform Blended;
		Blended.Blend(InOut.Pose[Bone], Other.Pose[Bone], Alpha);
		InOut.Pose[Bone] = Blended;
	}
}

bool FFTOCharacterAnimProxy::Evaluate(FPoseContext& Output)
{
	// 1. Locomotion: idle -> walk -> run.
	Sample(Idle, IdleTime, Output);

	const float MoveAlpha = FMath::Clamp(SmoothedSpeed / 120.f, 0.f, 1.f);
	if (MoveAlpha > 0.f)
	{
		FPoseContext Moving(this);
		Sample(Walk, LocoPhase * (Walk ? Walk->GetPlayLength() : 0.f), Moving);

		const float RunAlpha = FMath::Clamp((SmoothedSpeed - WalkReferenceSpeed) / FMath::Max(1.f, RunReferenceSpeed - WalkReferenceSpeed), 0.f, 1.f);
		if (RunAlpha > 0.f)
		{
			FPoseContext Running(this);
			Sample(Run, LocoPhase * (Run ? Run->GetPlayLength() : 0.f), Running);
			Blend(Moving, Running, RunAlpha);
		}
		Blend(Output, Moving, MoveAlpha);
	}

	// 2. Airborne.
	if (AirWeight > 0.01f)
	{
		FPoseContext Air(this);
		Sample(Jump, AirTime, Air);
		Blend(Output, Air, AirWeight);
	}

	// 3. Full-body action.
	if (ActionWeight > 0.01f && ShownAction != EFTOAnimAction::None)
	{
		FPoseContext ActionPose(this);
		Sample(ShownAction == EFTOAnimAction::Cheer ? Cheer : Interact, ActionTime, ActionPose);
		Blend(Output, ActionPose, ActionWeight);
	}

	return true;
}

// ------------------------------------------------------------------------------------------
// Anim instance (game thread)
// ------------------------------------------------------------------------------------------

UFTOCharacterAnimInstance::UFTOCharacterAnimInstance()
{
	IdleClip = FindClip(TEXT("/Game/FTO/Characters/Officer/A_Officer_Idle.A_Officer_Idle"));
	WalkClip = FindClip(TEXT("/Game/FTO/Characters/Officer/A_Officer_Walk.A_Officer_Walk"));
	RunClip = FindClip(TEXT("/Game/FTO/Characters/Officer/A_Officer_Run.A_Officer_Run"));
	JumpClip = FindClip(TEXT("/Game/FTO/Characters/Officer/A_Officer_Jump.A_Officer_Jump"));
	InteractClip = FindClip(TEXT("/Game/FTO/Characters/Officer/A_Officer_Interact.A_Officer_Interact"));
	CheerClip = FindClip(TEXT("/Game/FTO/Characters/Officer/A_Officer_Cheer.A_Officer_Cheer"));
}

FAnimInstanceProxy* UFTOCharacterAnimInstance::CreateAnimInstanceProxy()
{
	return new FFTOCharacterAnimProxy(this);
}

void UFTOCharacterAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

void UFTOCharacterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	FFTOCharacterAnimProxy& Proxy = GetProxyOnGameThread<FFTOCharacterAnimProxy>();
	Proxy.Idle = IdleClip;
	Proxy.Walk = WalkClip;
	Proxy.Run = RunClip;
	Proxy.Jump = JumpClip;
	Proxy.Interact = InteractClip;
	Proxy.Cheer = CheerClip;

	if (const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner()))
	{
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Proxy.WalkReferenceSpeed = FMath::Max(100.f, Movement->MaxWalkSpeed);
		}
	}
}

void UFTOCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	FFTOCharacterAnimProxy& Proxy = GetProxyOnGameThread<FFTOCharacterAnimProxy>();
	const AActor* Owner = GetOwningActor();
	if (const IFTOAnimatedActor* Animated = Cast<IFTOAnimatedActor>(Owner))
	{
		Proxy.Speed = Animated->GetAnimSpeed();
		Proxy.bInAir = Animated->IsAnimAirborne();
		Proxy.Action = Animated->GetAnimAction();
	}
	else if (Owner)
	{
		Proxy.Speed = Owner->GetVelocity().Size2D();
		Proxy.bInAir = false;
		Proxy.Action = EFTOAnimAction::None;
	}
}
