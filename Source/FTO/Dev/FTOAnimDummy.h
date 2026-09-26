#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Animation/FTOAnimatedActor.h"
#include "FTOAnimDummy.generated.h"

class USkeletalMeshComponent;
class UTextRenderComponent;

/**
 * Development-only mannequin that loops one animation action (and optional aim pose),
 * for eyeballing clips in engine. Spawned by the FTOAnimGallery console command.
 */
UCLASS(NotPlaceable)
class FTO_API AFTOAnimDummy : public AActor, public IFTOAnimatedActor
{
	GENERATED_BODY()

public:
	AFTOAnimDummy();

	void Setup(EFTOAnimAction InAction, EFTOAimPose InAim, bool bOfficer);

	virtual EFTOAnimAction GetAnimAction() const override { return Action; }
	virtual float GetAnimSpeed() const override { return 0.f; }
	virtual EFTOAimPose GetAimPose() const override { return Aim; }

protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Caption;

	EFTOAnimAction Action = EFTOAnimAction::None;
	EFTOAimPose Aim = EFTOAimPose::None;
};
