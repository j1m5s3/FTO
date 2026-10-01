#include "Animation/FTOCharacterAnimInstance.h"
#include "Physics/FTOKnockdownComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/Paths.h"

namespace
{
	float Wrap(float Time, const UAnimSequence* Sequence)
	{
		const float Length = Sequence ? Sequence->GetPlayLength() : 0.f;
		return Length > KINDA_SMALL_NUMBER ? FMath::Fmod(Time, Length) : 0.f;
	}

	/** One of our clips (Tools/Blender/character_clips.py). */
	UAnimSequence* FindClip(const FString& Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/FTO/Characters/Anims/A_FTO_%s.A_FTO_%s"), *Name, *Name);
		ConstructorHelpers::FObjectFinder<UAnimSequence> Finder(*Path);
		return Finder.Object;
	}

	/** One of Epic's mannequin animations (Content/Characters/Mannequins/Anims, Tools/Unreal/install_epic_content.py). */
	UAnimSequence* FindEpic(const TCHAR* Path)
	{
		const FString Name = FPaths::GetBaseFilename(Path);
		ConstructorHelpers::FObjectFinder<UAnimSequence> Finder(*FString::Printf(TEXT("/Game/Characters/Mannequins/Anims/%s.%s"), Path, *Name));
		return Finder.Object;
	}

	/** Actions whose clip has another name. */
	FString ClipNameFor(const FString& Action)
	{
		static const TMap<FString, FString> Renamed =
		{
			{ TEXT("Punch"), TEXT("Cross") }, { TEXT("IdleBored"), TEXT("Idle_Bored") },
			{ TEXT("KickFront"), TEXT("Kick_Front") }, { TEXT("KickSide"), TEXT("Kick_Side") }, { TEXT("KickRoundhouse"), TEXT("Kick_Roundhouse") },
			{ TEXT("FightGrab"), TEXT("Fight_Grab") }, { TEXT("Block"), TEXT("Block_Loop") },
			{ TEXT("HitLightFront"), TEXT("HitReact_Light_Front") }, { TEXT("HitLightBack"), TEXT("HitReact_Light_Back") },
			{ TEXT("HitLightLeft"), TEXT("HitReact_Light_Left") }, { TEXT("HitLightRight"), TEXT("HitReact_Light_Right") },
			{ TEXT("HitHeavy"), TEXT("HitReact_Heavy") }, { TEXT("FightIdle"), TEXT("Fight_Idle") },
			{ TEXT("FightStepFwd"), TEXT("Fight_Step_Fwd") }, { TEXT("FightStepBack"), TEXT("Fight_Step_Back") },
		};
		const FString* Clip = Renamed.Find(Action);
		return Clip ? *Clip : Action;
	}

	/** Is Bone (a mesh bone index) the upper body: the chest and everything on it, or the hand IK bones? */
	bool IsUpperBody(const FReferenceSkeleton& Skeleton, int32 Bone)
	{
		for (int32 At = Bone; At != INDEX_NONE; At = Skeleton.GetParentIndex(At))
		{
			const FName Name = Skeleton.GetBoneName(At);
			if (Name == TEXT("spine_01") || Name == TEXT("ik_hand_root"))
			{
				return true;
			}
		}
		return false;
	}

	/** How much of a lean each bone takes (chest most, then the neck and head). */
	const TPair<const TCHAR*, float> BendShares[] =
	{
		{ TEXT("spine_01"), 0.08f }, { TEXT("spine_02"), 0.12f }, { TEXT("spine_03"), 0.15f }, { TEXT("spine_04"), 0.15f },
		{ TEXT("spine_05"), 0.1f }, { TEXT("neck_01"), 0.1f }, { TEXT("neck_02"), 0.1f }, { TEXT("head"), 0.2f },
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

UAnimSequence* FFTOCharacterAnimProxy::ClipFor(EFTOAimPose InAim) const
{
	switch (InAim)
	{
	case EFTOAimPose::Pistol: return AimPistol;
	case EFTOAimPose::Rifle:  return AimRifle;
	case EFTOAimPose::Cuffed: return HandsBehind;
	default:                  return nullptr;
	}
}

void FFTOCharacterAnimProxy::SnapToAction(EFTOAnimAction InAction)
{
	Action = InAction;
	ShownAction = InAction;
	ActionTime = 0.f;
	ActionWeight = InAction != EFTOAnimAction::None ? 1.f : 0.f;
	FadingAction = EFTOAnimAction::None;
	CrossAlpha = 1.f;
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
	// The take-off once, then falling for as long as it lasts (walking off a ledge, there's no take-off).
	if (bInAir && AirTime <= 0.f && !bRising && Jump)
	{
		AirTime = Jump->GetPlayLength();
	}
	AirTime = bInAir ? AirTime + DeltaSeconds : 0.f;

	// Fade actions in and out. Going from one action straight to another crossfades between the two (kneeling to
	// kneeling in cuffs mustn't stand up in between).
	if (Action != EFTOAnimAction::None)
	{
		// The same move again (a second jab): start it over, crossfading from where the first one got to.
		if (ShownAction == Action && Serial != ShownSerial)
		{
			const bool bShowing = ActionWeight > 0.05f;
			FadingAction = bShowing ? ShownAction : EFTOAnimAction::None;
			FadingTime = ActionTime;
			CrossAlpha = bShowing ? 0.f : 1.f;
			ActionTime = 0.f;
		}
		if (ShownAction != Action)
		{
			const bool bCrossfade = ShownAction != EFTOAnimAction::None && ActionWeight > 0.05f;
			FadingAction = bCrossfade ? ShownAction : EFTOAnimAction::None;
			FadingTime = ActionTime;
			CrossAlpha = bCrossfade ? 0.f : 1.f;
			ShownAction = Action;
			ActionTime = 0.f;
		}
		ActionWeight = FMath::FInterpTo(ActionWeight, 1.f, DeltaSeconds, 8.f);
		ShownSerial = Serial;
	}
	else
	{
		ActionWeight = FMath::FInterpTo(ActionWeight, 0.f, DeltaSeconds, 8.f);
	}
	ActionTime = Wrap(ActionTime + DeltaSeconds, ClipFor(ShownAction));
	CrossAlpha = FMath::Min(1.f, CrossAlpha + DeltaSeconds * 4.f);
	FadingTime = Wrap(FadingTime + DeltaSeconds, ClipFor(FadingAction));
	if (CrossAlpha >= 1.f)
	{
		FadingAction = EFTOAnimAction::None;
	}

	// Upper-body pose over the top: fade out the old one before switching.
	if (Aim != EFTOAimPose::None && (ShownAim == Aim || AimWeight < 0.05f))
	{
		ShownAim = Aim;
		AimWeight = FMath::FInterpTo(AimWeight, 1.f, DeltaSeconds, 12.f);
	}
	else
	{
		AimWeight = FMath::FInterpTo(AimWeight, 0.f, DeltaSeconds, 12.f);
	}
	AimTime = Wrap(AimTime + DeltaSeconds, ClipFor(ShownAim));
	SmoothedPitch = FMath::FInterpTo(SmoothedPitch, AimPitch, DeltaSeconds, 15.f);

	FromWeight = FMath::Max(0.f, FromWeight - DeltaSeconds * FromFadeRate);
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
	// In place: Epic's walk and jog carry the root forward (root motion), which the capsule does for us.
	if (Out.Pose.GetNumBones() > 0)
	{
		Out.Pose[FCompactPoseBoneIndex(0)].SetTranslation(FVector::ZeroVector);
	}
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
	Sample(ClipFor(ShownAim), AimTime, AimPose);

	// The chest and everything on it (arms, hands and fingers round the grip) from the aim pose. The chest is held
	// as the aim pose has it in the body's own space, not on top of the legs' pelvis, so the gun points where the aim
	// pose points it and doesn't sway with the walk.
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const FReferenceSkeleton& RefSkeleton = Bones.GetReferenceSkeleton();
	const int32 ChestBone = RefSkeleton.FindBoneIndex(TEXT("spine_01"));
	const FCompactPoseBoneIndex Chest = ChestBone == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(ChestBone));
	FQuat ChestLocal = FQuat::Identity;
	if (Chest.IsValid())
	{
		// (Component-space rotation of the chest's parents, in each pose.)
		auto ParentRotation = [&](const FCompactPose& Pose)
		{
			FQuat Rotation = FQuat::Identity;
			for (int32 At = RefSkeleton.GetParentIndex(ChestBone); At != INDEX_NONE; At = RefSkeleton.GetParentIndex(At))
			{
				const FCompactPoseBoneIndex Compact = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(At));
				Rotation = (Compact.IsValid() ? Pose[Compact].GetRotation() : RefSkeleton.GetRefBonePose()[At].GetRotation()) * Rotation;
			}
			return Rotation;
		};
		ChestLocal = ParentRotation(Output.Pose).Inverse() * ParentRotation(AimPose.Pose) * AimPose.Pose[Chest].GetRotation();
	}
	for (const FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
	{
		if (IsUpperBody(RefSkeleton, Bones.MakeMeshPoseIndex(Bone).GetInt()))
		{
			FTransform Target = AimPose.Pose[Bone];
			if (Bone == Chest)
			{
				Target.SetRotation(ChestLocal.GetNormalized());
				Target.SetTranslation(Output.Pose[Bone].GetTranslation());
			}
			FTransform Blended;
			Blended.Blend(Output.Pose[Bone], Target, AimWeight);
			Output.Pose[Bone] = Blended;
		}
	}

	// Look up and down: the whole chest leans with the aim, so the arms (and the gun in them) follow it.
	Bend(Output, SmoothedPitch * AimWeight);
}

void FFTOCharacterAnimProxy::Bend(FPoseContext& Output, float Degrees) const
{
	if (FMath::Abs(Degrees) < 0.1f)
	{
		return;
	}
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const FReferenceSkeleton& RefSkeleton = Bones.GetReferenceSkeleton();
	// The mannequin faces +Y in its own space, so leaning back is a turn about the X axis there. Each bone turns
	// about that axis as its own frame sees it (worked out from where it is in the pose before any of the lean).
	auto ComponentRotation = [&](int32 MeshBone)
	{
		FQuat Rotation = FQuat::Identity;
		for (int32 At = MeshBone; At != INDEX_NONE; At = RefSkeleton.GetParentIndex(At))
		{
			const FCompactPoseBoneIndex Compact = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(At));
			Rotation = (Compact.IsValid() ? Output.Pose[Compact].GetRotation() : RefSkeleton.GetRefBonePose()[At].GetRotation()) * Rotation;
		}
		return Rotation;
	};
	TArray<TPair<FCompactPoseBoneIndex, FQuat>, TInlineAllocator<8>> Turns;
	for (const TPair<const TCHAR*, float>& Share : BendShares)
	{
		const int32 MeshBone = RefSkeleton.FindBoneIndex(Share.Key);
		const FCompactPoseBoneIndex Compact = MeshBone == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBone));
		if (Compact.IsValid())
		{
			const FVector Axis = ComponentRotation(MeshBone).Inverse().RotateVector(FVector::XAxisVector);
			Turns.Emplace(Compact, FQuat(Axis, FMath::DegreesToRadians(Degrees * Share.Value)));
		}
	}
	for (const TPair<FCompactPoseBoneIndex, FQuat>& Turn : Turns)
	{
		FTransform& Local = Output.Pose[Turn.Key];
		Local.SetRotation(Local.GetRotation() * Turn.Value);
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

	// 2. Airborne: the take-off, then falling.
	if (AirWeight > 0.01f)
	{
		FPoseContext Air(this);
		const float TakeOff = Jump ? Jump->GetPlayLength() : 0.f;
		if (AirTime < TakeOff || !Fall)
		{
			Sample(Jump, FMath::Min(AirTime, FMath::Max(0.f, TakeOff - 0.01f)), Air);
		}
		else
		{
			Sample(Fall, AirTime - TakeOff, Air);
		}
		Blend(Output, Air, AirWeight);
	}

	// 3. Full-body action (mid-crossfade, a mix of the old one and the new).
	if (ActionWeight > 0.01f && ShownAction != EFTOAnimAction::None)
	{
		if (UAnimSequence* Clip = ClipFor(ShownAction))
		{
			FPoseContext ActionPose(this);
			Sample(Clip, ActionTime, ActionPose);
			UAnimSequence* Fading = FadingAction != EFTOAnimAction::None ? ClipFor(FadingAction) : nullptr;
			if (Fading && CrossAlpha < 1.f)
			{
				FPoseContext FadingPose(this);
				Sample(Fading, FadingTime, FadingPose);
				Blend(FadingPose, ActionPose, CrossAlpha * CrossAlpha * (3.f - 2.f * CrossAlpha));
				Blend(Output, FadingPose, ActionWeight);
			}
			else
			{
				Blend(Output, ActionPose, ActionWeight);
			}
		}
	}

	// 4. Weapon pose from the waist up.
	ApplyAimLayer(Output);

	// 5. Easing out of a pose we were thrown into (getting up from a ragdoll).
	if (FromWeight > 0.f && FromPose.Num() > 0)
	{
		const float Alpha = FromWeight * FromWeight * (3.f - 2.f * FromWeight);
		const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
		for (const FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
		{
			const int32 MeshIndex = Bones.MakeMeshPoseIndex(Bone).GetInt();
			if (FromPose.IsValidIndex(MeshIndex))
			{
				FTransform Blended;
				Blended.Blend(Output.Pose[Bone], FromPose[MeshIndex], Alpha);
				Output.Pose[Bone] = Blended;
			}
		}
	}

	return true;
}

// ------------------------------------------------------------------------------------------
// Anim instance (game thread)
// ------------------------------------------------------------------------------------------

UFTOCharacterAnimInstance::UFTOCharacterAnimInstance()
{
	// Getting about and holding a gun: Epic's.
	IdleClip = FindEpic(TEXT("Unarmed/MM_Idle"));
	WalkClip = FindEpic(TEXT("Unarmed/Walk/MF_Unarmed_Walk_Fwd"));
	RunClip = FindEpic(TEXT("Unarmed/Jog/MF_Unarmed_Jog_Fwd"));
	JumpClip = FindEpic(TEXT("Unarmed/Jump/MM_Jump"));
	FallClip = FindEpic(TEXT("Unarmed/Jump/MM_Fall_Loop"));
	AimPistolClip = FindEpic(TEXT("Pistol/MF_Pistol_Idle_ADS"));
	AimRifleClip = FindEpic(TEXT("Rifle/MF_Rifle_Idle_ADS"));
	// Everything else: ours.
	HandsBehindClip = FindClip(TEXT("HandsBehind"));

	// Every action plays the clip named after it.
	const UEnum* Actions = StaticEnum<EFTOAnimAction>();
	for (int32 i = 0; i < Actions->NumEnums() - 1; ++i) // last entry is the hidden _MAX
	{
		const EFTOAnimAction Action = static_cast<EFTOAnimAction>(Actions->GetValueByIndex(i));
		if (Action != EFTOAnimAction::None)
		{
			if (UAnimSequence* Clip = FindClip(ClipNameFor(Actions->GetNameStringByIndex(i))))
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
	Proxy.Fall = FallClip;
	Proxy.AimPistol = AimPistolClip;
	Proxy.AimRifle = AimRifleClip;
	Proxy.HandsBehind = HandsBehindClip;
	Proxy.ActionClips.Reset();
	for (const TPair<EFTOAnimAction, TObjectPtr<UAnimSequence>>& Pair : ActionClips)
	{
		Proxy.ActionClips.Add(Pair.Key, Pair.Value.Get());
	}
}

void UFTOCharacterAnimInstance::BlendFromPose(const TArray<FTransform>& LocalPose, float Duration)
{
	FFTOCharacterAnimProxy& Proxy = GetProxyOnGameThread<FFTOCharacterAnimProxy>();
	Proxy.FromPose = LocalPose;
	Proxy.FromWeight = 1.f;
	Proxy.FromFadeRate = 1.f / FMath::Max(0.05f, Duration);
}

void UFTOCharacterAnimInstance::SnapToAction(EFTOAnimAction Action)
{
	GetProxyOnGameThread<FFTOCharacterAnimProxy>().SnapToAction(Action);
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
		Proxy.bRising = Owner && Owner->GetVelocity().Z > 50.f;
		Proxy.Action = Animated->GetAnimActionFor(GetSkelMeshComponent());
		Proxy.Serial = Animated->GetAnimActionSerial();
		Proxy.Aim = Animated->GetAimPose();
		Proxy.AimPitch = Animated->GetAimPitch();
		// A blow thrown or taken (FTOFighting) shows over whatever else they're doing, the weapon put by for it.
		if (!Knockdown.IsValid() && Owner)
		{
			Knockdown = Owner->FindComponentByClass<UFTOKnockdownComponent>();
		}
		const UFTOKnockdownComponent* Blows = Knockdown.Get();
		if (Blows && Blows->GetMove() != EFTOAnimAction::None && Blows->GetSkelMesh() == GetSkelMeshComponent())
		{
			Proxy.Action = Blows->GetMove();
			Proxy.Serial = 1000u + Blows->GetMoveSerial();
			Proxy.Aim = EFTOAimPose::None;
		}
	}
	else if (Owner)
	{
		Proxy.Speed = Owner->GetVelocity().Size2D();
		Proxy.bInAir = false;
		Proxy.Action = EFTOAnimAction::None;
		Proxy.Aim = EFTOAimPose::None;
	}
}
