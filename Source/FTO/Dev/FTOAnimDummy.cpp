#include "Dev/FTOAnimDummy.h"
#include "Animation/FTOCharacterAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"

AFTOAnimDummy::AFTOAnimDummy()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	Body->SetRelativeRotation(FRotator(0.f, -90.f, 0.f)); // Blender models face +Y
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Body->SetAnimInstanceClass(UFTOCharacterAnimInstance::StaticClass());

	Caption = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Caption"));
	Caption->SetupAttachment(RootComponent);
	Caption->SetRelativeLocation(FVector(0.f, 0.f, 215.f));
	Caption->SetHorizontalAlignment(EHTA_Center);
	Caption->SetWorldSize(22.f);
	Caption->SetTextRenderColor(FColor::White);
}

void AFTOAnimDummy::Setup(EFTOAnimAction InAction, EFTOAimPose InAim, bool bOfficer)
{
	Action = InAction;
	Aim = InAim;

	const TCHAR* MeshPath = bOfficer
		? TEXT("/Game/FTO/Characters/Officer/SK_Officer.SK_Officer")
		: TEXT("/Game/FTO/Characters/Civilians/SK_Civilian_03.SK_Civilian_03");
	Body->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr, MeshPath));

	FString Label = StaticEnum<EFTOAnimAction>()->GetNameStringByValue(int64(Action));
	if (Aim != EFTOAimPose::None)
	{
		Label = StaticEnum<EFTOAimPose>()->GetNameStringByValue(int64(Aim));
	}
	Caption->SetText(FText::FromString(Label));
}
