#include "Physics/FTOKnockdownComponent.h"
#include "Animation/FTOCharacterAnimInstance.h"
#include "Core/FTOGameState.h"
#include "Engine/SkeletalMesh.h"
#include "Art/FTOArt.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "FTO.h"

namespace
{
	constexpr int32 NumStars = 3;
	const FName PelvisBone(TEXT("pelvis"));
	const FName HeadBone(TEXT("head"));
	const FName FootLBone(TEXT("foot_l"));
	const FName FootRBone(TEXT("foot_r"));
}

UFTOKnockdownComponent::UFTOKnockdownComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UFTOKnockdownComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UFTOKnockdownComponent, State);
}

void UFTOKnockdownComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
	if (Mesh)
	{
		SavedProfile = Mesh->GetCollisionProfileName();
		SavedRelative = Mesh->GetRelativeTransform();
	}
}

void UFTOKnockdownComponent::EnsureStars()
{
	// Cartoon stars that circle the head while dazed, made the first time they're needed (most of the
	// crowd never gets bowled over).
	AActor* Owner = GetOwner();
	if (!Stars.IsEmpty() || !Owner || !Owner->GetRootComponent())
	{
		return;
	}
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, FTOArt::BaseMaterialPath);
	for (int32 i = 0; i < NumStars; ++i)
	{
		UStaticMeshComponent* Star = NewObject<UStaticMeshComponent>(Owner);
		Star->SetupAttachment(Owner->GetRootComponent());
		Star->SetStaticMesh(Sphere);
		Star->SetRelativeScale3D(FVector(0.09f));
		Star->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Star->SetCastShadow(false);
		Star->SetUsingAbsoluteLocation(true);
		Star->SetVisibility(false);
		Star->RegisterComponent();
		FTOArt::ApplyColor(Star, Base, FLinearColor(1.f, 0.85f, 0.1f), 6.f);
		Stars.Add(Star);
	}
}

// ------------------------------------------------------------------------------------------
// Server API
// ------------------------------------------------------------------------------------------

void UFTOKnockdownComponent::Knockdown(const FVector& LaunchVelocity, float Duration)
{
	check(GetOwner()->HasAuthority());
	State.bDown = true;
	State.Launch = LaunchVelocity;
	++State.Serial;
	RecoverAt = Duration > 0.f ? GetWorld()->GetTimeSeconds() + Duration : 0.f;
	GetOwner()->ForceNetUpdate();
	OnRep_State();

	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastPlaySound(AFTOGameState::Sounds().Bonk, GetOwner()->GetActorLocation(), 0.9f);
	}
}

void UFTOKnockdownComponent::Recover()
{
	check(GetOwner()->HasAuthority());
	if (!State.bDown)
	{
		return;
	}
	State.bDown = false;
	GetOwner()->ForceNetUpdate();
	OnRep_State();
}

FVector UFTOKnockdownComponent::GetBodyLocation() const
{
	return (bRagdolling && Mesh) ? Mesh->GetSocketLocation(PelvisBone) : GetOwner()->GetActorLocation();
}

// ------------------------------------------------------------------------------------------
// Every machine
// ------------------------------------------------------------------------------------------

void UFTOKnockdownComponent::OnRep_State()
{
	if (State.bDown && (!bRagdolling || State.Serial != AppliedSerial))
	{
		const bool bWasDown = bRagdolling;
		AppliedSerial = State.Serial;
		StartRagdoll(State.Launch);
		if (!bWasDown)
		{
			OnKnockedDown.Broadcast();
		}
	}
	else if (!State.bDown && bRagdolling)
	{
		StopRagdoll();
		OnRecovered.Broadcast();
	}
}

void UFTOKnockdownComponent::StartRagdoll(const FVector& LaunchVelocity)
{
	if (!Mesh)
	{
		return;
	}

	// The root (capsule) steps aside; the body takes over under Chaos physics.
	if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()); Root && Root != Mesh)
	{
		Root->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (!bRagdolling)
	{
		Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
		Mesh->SetAllBodiesSimulatePhysics(true);
		Mesh->SetSimulatePhysics(true);
		Mesh->WakeAllRigidBodies();
		bRagdolling = true;
	}
	Mesh->SetAllPhysicsLinearVelocity(LaunchVelocity);
	// A little tumble so nobody lands neatly on their feet (purely cosmetic, so local randomness is fine).
	Mesh->SetAllPhysicsAngularVelocityInDegrees(FMath::VRand() * FMath::FRandRange(180.f, 420.f));
	EnsureStars();
	StarsUntil = TNumericLimits<float>::Max();
}

void UFTOKnockdownComponent::StopRagdoll()
{
	if (!Mesh || !bRagdolling)
	{
		return;
	}
	bRagdolling = false;

	AActor* Owner = GetOwner();
	const FVector Pelvis = Mesh->GetSocketLocation(PelvisBone);
	const FVector Feet = (Mesh->GetSocketLocation(FootLBone) + Mesh->GetSocketLocation(FootRBone)) * 0.5f;

	// Where every bone came to rest, so the animation can pick up from there instead of snapping upright.
	TArray<FTransform> RestingPose;
	RestingPose.SetNum(Mesh->GetNumBones());
	for (int32 Bone = 0; Bone < RestingPose.Num(); ++Bone)
	{
		RestingPose[Bone] = Mesh->GetBoneTransform(Bone);
	}

	Mesh->SetSimulatePhysics(false);
	Mesh->SetAllBodiesSimulatePhysics(false);
	Mesh->SetCollisionProfileName(SavedProfile);

	// Sit up where the body came to rest, facing the way the legs lie.
	const float StandHeight = -SavedRelative.GetLocation().Z;
	FVector StandAt(Pelvis.X, Pelvis.Y, Pelvis.Z + StandHeight * 0.5f);
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOKnockdownGround), false, Owner);
	if (GetWorld()->LineTraceSingleByObjectType(Ground, Pelvis + FVector(0.f, 0.f, 60.f), Pelvis - FVector(0.f, 0.f, 400.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		StandAt.Z = Ground.ImpactPoint.Z + StandHeight + 2.f;
	}
	const FVector Legs = (Feet - Pelvis).GetSafeNormal2D();
	const FRotator Facing = Legs.IsNearlyZero() ? FRotator(0.f, Owner->GetActorRotation().Yaw, 0.f) : FRotator(0.f, Legs.Rotation().Yaw, 0.f);
	Owner->SetActorLocationAndRotation(StandAt, Facing, false, nullptr, ETeleportType::TeleportPhysics);

	if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Owner->GetRootComponent()); Root && Root != Mesh)
	{
		Mesh->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
		Mesh->SetRelativeTransform(SavedRelative);
		Root->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// The resting pose in the mesh's new frame, bone by bone relative to its parent (the unskinned root stays
	// put, so the whole body eases from lying to sitting rather than sliding across), handed to the animation.
	if (UFTOCharacterAnimInstance* Anim = Cast<UFTOCharacterAnimInstance>(Mesh->GetAnimInstance()); Anim && Mesh->GetSkeletalMeshAsset())
	{
		const FTransform Component = Mesh->GetComponentTransform();
		TArray<FTransform> ComponentSpace;
		ComponentSpace.SetNum(RestingPose.Num());
		for (int32 Bone = 0; Bone < RestingPose.Num(); ++Bone)
		{
			ComponentSpace[Bone] = Bone == 0 ? FTransform::Identity : RestingPose[Bone].GetRelativeTransform(Component);
		}
		const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		TArray<FTransform> Local;
		Local.SetNum(RestingPose.Num());
		for (int32 Bone = 0; Bone < RestingPose.Num(); ++Bone)
		{
			const int32 Parent = Skeleton.GetParentIndex(Bone);
			Local[Bone] = Parent == INDEX_NONE ? ComponentSpace[Bone] : ComponentSpace[Bone].GetRelativeTransform(ComponentSpace[Parent]);
		}
		Anim->BlendFromPose(Local, 0.6f);
	}

	StarsUntil = GetWorld()->GetTimeSeconds() + DazedSeconds;
}

bool UFTOKnockdownComponent::IsDazed() const
{
	return !State.bDown && !bRagdolling && GetWorld() && GetWorld()->GetTimeSeconds() < StarsUntil;
}

void UFTOKnockdownComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Keep the actor (and anything tracking it: camera, markers, relevancy) with the body.
	if (bRagdolling && Mesh)
	{
		const FVector Pelvis = Mesh->GetSocketLocation(PelvisBone);
		GetOwner()->SetActorLocation(Pelvis + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (GetOwner()->HasAuthority() && State.bDown && RecoverAt > 0.f && GetWorld()->GetTimeSeconds() >= RecoverAt)
	{
		Recover();
	}

	UpdateStars(DeltaTime);
}

void UFTOKnockdownComponent::UpdateStars(float DeltaTime)
{
	const bool bShow = Mesh && GetWorld()->GetTimeSeconds() < StarsUntil;
	StarSpin += DeltaTime * 360.f;
	const FVector Head = bShow ? Mesh->GetSocketLocation(HeadBone) + FVector(0.f, 0.f, bRagdolling ? 35.f : 55.f) : FVector::ZeroVector;
	for (int32 i = 0; i < Stars.Num(); ++i)
	{
		UStaticMeshComponent* Star = Stars[i];
		if (Star->IsVisible() != bShow)
		{
			Star->SetVisibility(bShow);
		}
		if (bShow)
		{
			const float Angle = FMath::DegreesToRadians(StarSpin + i * (360.f / NumStars));
			Star->SetWorldLocation(Head + FVector(FMath::Cos(Angle) * 32.f, FMath::Sin(Angle) * 32.f, FMath::Sin(Angle * 2.f) * 5.f));
		}
	}
}
