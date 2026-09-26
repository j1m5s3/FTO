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

	UAnimSequence* FindClip(const FString& Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/FTO/Characters/Officer/A_Officer_%s.A_Officer_%s"), *Name, *Name);
		ConstructorHelpers::FObjectFinder<UAnimSequence> Finder(*Path);
		return Finder.Object;
	}

	/** Bones the aim layer takes over (everything from the waist up). */
	const FName UpperBodyBones[] =
	{
		TEXT("spine"), TEXT("head"),
		TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"),
		TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"),
	};
}

// ------------------------------------------------------------------------------------------
// Proxy (worker thread)
// ------------------------------------------------------------------------------------------

UAnimSequence* FFTOCharacterAnimProxy::ClipFor(EFTOAnimAction InAction) const
{
	UAnimSequence* const* Found = ActionClips.Find(InAction);
	return Found ? *Found : nullptr;
}

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
	const float PlayRate = FMath::Clamp(SmoothedSpeed / FMath::Max(1.f, ReferenceSpeed), 0.4f, 1.9f);
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
	ActionTime = Wrap(ActionTime + DeltaSeconds, ClipFor(ShownAction));

	// Weapon pose over the top: same fade-and-switch rule.
	if (Aim != EFTOAimPose::None && (ShownAim == Aim || AimWeight < 0.05f))
	{
		ShownAim = Aim;
		AimWeight = FMath::FInterpTo(AimWeight, 1.f, DeltaSeconds, 12.f);
	}
	else
	{
		AimWeight = FMath::FInterpTo(AimWeight, 0.f, DeltaSeconds, 12.f);
	}
	AimTime = Wrap(AimTime + DeltaSeconds, ShownAim == EFTOAimPose::Rifle ? AimRifle : AimPistol);
	SmoothedPitch = FMath::FInterpTo(SmoothedPitch, AimPitch, DeltaSeconds, 15.f);
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

void FFTOCharacterAnimProxy::ApplyAimLayer(FPoseContext& Output)
{
	if (AimWeight <= 0.01f || ShownAim == EFTOAimPose::None)
	{
		return;
	}

	FPoseContext AimPose(this);
	Sample(ShownAim == EFTOAimPose::Rifle ? AimRifle : AimPistol, AimTime, AimPose);

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto ToCompact = [&Bones](FName Name) -> FCompactPoseBoneIndex
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Name);
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	};

	for (const FName& Name : UpperBodyBones)
	{
		const FCompactPoseBoneIndex Index = ToCompact(Name);
		if (Index.IsValid())
		{
			FTransform Blended;
			Blended.Blend(Output.Pose[Index], AimPose.Pose[Index], AimWeight);
			Output.Pose[Index] = Blended;
		}
	}

	// Look up/down by bending the spine (60%) and head (40%) about their side-to-side axis.
	auto Bend = [&](FName Name, float Degrees)
	{
		const FCompactPoseBoneIndex Index = ToCompact(Name);
		if (Index.IsValid())
		{
			FTransform& Local = Output.Pose[Index];
			Local.SetRotation(Local.GetRotation() * FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Degrees)));
		}
	};
	Bend(TEXT("spine"), -SmoothedPitch * 0.6f * AimWeight);
	Bend(TEXT("head"), -SmoothedPitch * 0.4f * AimWeight);
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
		if (UAnimSequence* Clip = ClipFor(ShownAction))
		{
			FPoseContext ActionPose(this);
			Sample(Clip, ActionTime, ActionPose);
			Blend(Output, ActionPose, ActionWeight);
		}
	}

	// 4. Weapon pose from the waist up.
	ApplyAimLayer(Output);

	return true;
}

// ------------------------------------------------------------------------------------------
// Anim instance (game thread)
// ------------------------------------------------------------------------------------------

UFTOCharacterAnimInstance::UFTOCharacterAnimInstance()
{
	IdleClip = FindClip(TEXT("Idle"));
	WalkClip = FindClip(TEXT("Walk"));
	RunClip = FindClip(TEXT("Run"));
	JumpClip = FindClip(TEXT("Jump"));
	AimPistolClip = FindClip(TEXT("AimPistol"));
	AimRifleClip = FindClip(TEXT("AimRifle"));

	// Every action plays the clip named after it.
	const UEnum* Actions = StaticEnum<EFTOAnimAction>();
	for (int32 i = 0; i < Actions->NumEnums() - 1; ++i) // last entry is the hidden _MAX
	{
		const EFTOAnimAction Action = static_cast<EFTOAnimAction>(Actions->GetValueByIndex(i));
		if (Action != EFTOAnimAction::None)
		{
			if (UAnimSequence* Clip = FindClip(Actions->GetNameStringByIndex(i)))
			{
				ActionClips.Add(Action, Clip);
			}
		}
	}
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
	Proxy.AimPistol = AimPistolClip;
	Proxy.AimRifle = AimRifleClip;
	Proxy.ActionClips.Reset();
	for (const TPair<EFTOAnimAction, TObjectPtr<UAnimSequence>>& Pair : ActionClips)
	{
		Proxy.ActionClips.Add(Pair.Key, Pair.Value.Get());
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
		Proxy.Action = Animated->GetAnimActionFor(GetSkelMeshComponent());
		Proxy.Aim = Animated->GetAimPose();
		Proxy.AimPitch = Animated->GetAimPitch();
	}
	else if (Owner)
	{
		Proxy.Speed = Owner->GetVelocity().Size2D();
		Proxy.bInAir = false;
		Proxy.Action = EFTOAnimAction::None;
		Proxy.Aim = EFTOAimPose::None;
	}
}
