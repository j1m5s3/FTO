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
class UFTOKnockdownComponent;
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

	/** Server: an officer blew a whistle: stop, hands up, look at them for a moment. */
	void FreezeFor(const AActor* Officer, float Seconds);

	/** Server: carry on with whatever we were doing (after a chat, a whistle, a tumble...). */
	virtual void Resume();

	// IFTOInteractable
	virtual bool CanInteract(const AFTOCharacter* Officer) const override;
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual void Interact(AFTOCharacter* Officer) override;
	virtual FVector GetInteractLocation() const override { return GetActorLocation(); }

	// IFTOAnimatedActor
	virtual EFTOAnimAction GetAnimAction() const override;
	virtual float GetAnimSpeed() const override { return GetCurrentSpeed(); }

	virtual bool IsMovementFrozen() const override;
	UFTOKnockdownComponent* GetKnockdown() const { return Knockdown; }

	/** What we look like, the way a witness would put it ("green top, bald with a beard"). */
	FString DescribeLook() const;

	/** Capsule half-height; the path runs this far above the sidewalk. */
	static constexpr float HalfHeight = 92.f;

protected:
	virtual void BeginPlay() override;
	virtual void OnArrived() override;

	void WalkToNextCorner();

	/** Server: turn to look at an officer who's talking to (or whistling at) us. */
	virtual void FaceOfficer(const AActor* Officer);
	/** A line for an officer who stops for a chat and has no tip-off coming. */
	virtual FString GetSmallTalk();

	UFUNCTION() void OnRep_Look();
	/** Dress the body from LookSeed (every machine). */
	virtual void ApplyLook();
	/** Tint every material slot of Body with Color. */
	void PaintBody(const FLinearColor& Color);

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UCapsuleComponent> Capsule;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UFTOKnockdownComponent> Knockdown;

	void HandleRecovered();

	/** Civilian variants to pick from (Tools/Blender/build_civilians.py). */
	UPROPERTY() TArray<TObjectPtr<USkeletalMesh>> Looks;

	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	/** Picks the variant and shirt colour; rolled on the server. */
	UPROPERTY(ReplicatedUsing=OnRep_Look) int32 LookSeed = 0;

	/** Stopped for a chat with an officer. */
	UPROPERTY(Replicated) bool bChatting = false;

	/** Startled by a whistle. */
	UPROPERTY(Replicated) bool bHandsUp = false;

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
