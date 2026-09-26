#pragma once

#include "CoreMinimal.h"
#include "City/FTOPathMover.h"
#include "Interaction/FTOInteractable.h"
#include "Animation/FTOAnimatedActor.h"
#include "FTOPedestrian.generated.h"

class AFTOCityGenerator;
class UCapsuleComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * An ambient citizen strolling the sidewalks. Officers can chat to them;
 * sometimes they tip you off about trouble nobody has reported yet.
 */
UCLASS()
class FTO_API AFTOPedestrian : public AFTOPathMover, public IFTOInteractable, public IFTOAnimatedActor
{
	GENERATED_BODY()

public:
	AFTOPedestrian();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: start walking the ring around this block. */
	void StartWandering(AFTOCityGenerator* InCity, int32 InBlockX, int32 InBlockY, int32 InCorner, int32 InSeed);

	// IFTOInteractable
	virtual bool CanInteract(const AFTOCharacter* Officer) const override;
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual void Interact(AFTOCharacter* Officer) override;
	virtual FVector GetInteractLocation() const override { return GetActorLocation(); }

	// IFTOAnimatedActor
	virtual EFTOAnimAction GetAnimAction() const override { return bChatting ? EFTOAnimAction::Interact : EFTOAnimAction::None; }
	virtual float GetAnimSpeed() const override { return GetCurrentSpeed(); }

	/** Capsule half-height; the path runs this far above the sidewalk. */
	static constexpr float HalfHeight = 92.f;

protected:
	virtual void BeginPlay() override;
	virtual void OnArrived() override;

	void WalkToNextCorner();

	UFUNCTION() void OnRep_Look();

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UCapsuleComponent> Capsule;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USkeletalMeshComponent> Body;

	/** Civilian variants to pick from (Tools/Blender/build_civilians.py). */
	UPROPERTY() TArray<TObjectPtr<USkeletalMesh>> Looks;

	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	/** Picks the variant and shirt colour; rolled on the server. */
	UPROPERTY(ReplicatedUsing=OnRep_Look) int32 LookSeed = 0;

	/** Stopped for a chat with an officer. */
	UPROPERTY(Replicated) bool bChatting = false;

	UPROPERTY(Transient) TObjectPtr<AFTOCityGenerator> City;

	FRandomStream Rng;
	int32 BlockX = 0;
	int32 BlockY = 0;
	int32 Corner = 0;
	int32 Direction = 1;
	float WalkSpeed = 150.f;
	float ChatCooldownUntil = 0.f;
	FTimerHandle ResumeTimer;
};
