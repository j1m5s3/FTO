#pragma once

#include "CoreMinimal.h"
#include "City/FTOPathMover.h"
#include "Interaction/FTOInteractable.h"
#include "FTOPedestrian.generated.h"

class AFTOCityGenerator;
class UCapsuleComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * An ambient citizen strolling the sidewalks. Officers can chat to them;
 * sometimes they tip you off about trouble nobody has reported yet.
 */
UCLASS()
class FTO_API AFTOPedestrian : public AFTOPathMover, public IFTOInteractable
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

protected:
	virtual void BeginPlay() override;
	virtual void OnArrived() override;
	virtual void TickCosmetics(float DeltaSeconds) override;

	void WalkToNextCorner();

	UFUNCTION() void OnRep_Look();

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UCapsuleComponent> Capsule;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Head;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	/** Packed look: colour + size, rolled on the server. */
	UPROPERTY(ReplicatedUsing=OnRep_Look) int32 LookSeed = 0;

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
