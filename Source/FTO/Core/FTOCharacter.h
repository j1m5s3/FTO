#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Animation/FTOAnimatedActor.h"
#include "FTOCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
struct FInputActionValue;

/**
 * A player officer: the in-house Blender-built cop (Tools/Blender/build_officer.py) with its
 * uniform tinted in the player's badge colour. Falls back to a "bean cop" made of engine
 * primitives if the art hasn't been imported.
 */
UCLASS()
class FTO_API AFTOCharacter : public ACharacter, public IFTOAnimatedActor
{
	GENERATED_BODY()

public:
	AFTOCharacter();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void OnRep_PlayerState() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void Tick(float DeltaSeconds) override;

	/** Server: climb into a vehicle (hidden and carried along until ExitVehicle). */
	void EnterVehicle(AActor* Vehicle);

	/** Server: climb out at the given spot. */
	void ExitVehicle(const FVector& Location, float Yaw);

	UFUNCTION(BlueprintPure, Category="FTO")
	AActor* GetCurrentVehicle() const { return CurrentVehicle; }

	/** Server: play a full-body action for a while (ticket writing, chatting). */
	void PlayTimedAction(EFTOAnimAction Action, float Duration);

	// IFTOAnimatedActor
	virtual EFTOAnimAction GetAnimAction() const override;
	virtual bool IsAnimAirborne() const override;
	virtual float GetAnimSpeed() const override;

	/** The interactable the local officer would use if they pressed Interact now. */
	AActor* GetFocusedInteractable() const { return FocusedInteractable.Get(); }

	/** Re-tints the uniform from the owning player state's badge colour. */
	void RefreshOfficerColor();

	UFUNCTION(BlueprintPure, Category="FTO")
	bool IsSprinting() const { return bSprinting; }

	UPROPERTY(EditDefaultsOnly, Category="FTO|Movement")
	float WalkSpeed = 500.f;

	UPROPERTY(EditDefaultsOnly, Category="FTO|Movement")
	float SprintSpeed = 850.f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** Placeholder body pieces. */
	UPROPERTY(VisibleAnywhere, Category="Components|Placeholder")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category="Components|Placeholder")
	TObjectPtr<UStaticMeshComponent> HeadMesh;

	UPROPERTY(VisibleAnywhere, Category="Components|Placeholder")
	TObjectPtr<UStaticMeshComponent> CapMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> UniformMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeadMaterial;

	/** The vehicle this officer is riding in, if any. */
	UPROPERTY(ReplicatedUsing=OnRep_CurrentVehicle)
	TObjectPtr<AActor> CurrentVehicle;

	UFUNCTION()
	void OnRep_CurrentVehicle();

	void ApplyVehicleState();

	/** Short replicated full-body action (e.g. writing a ticket) everyone should see. */
	UPROPERTY(Replicated)
	EFTOAnimAction TimedAction = EFTOAnimAction::None;

	/** Server world time the timed action ends. */
	UPROPERTY(Replicated)
	float TimedActionEnd = 0.f;

	UPROPERTY(ReplicatedUsing=OnRep_Sprinting)
	bool bSprinting = false;

	UFUNCTION()
	void OnRep_Sprinting();

	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bNewSprinting);

	void ApplySprint();

	// Input handlers
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void SprintStarted();
	void SprintStopped();

	/** Picks the nearest usable interactable in range (local player only). */
	void UpdateFocus();

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target);

	TWeakObjectPtr<AActor> FocusedInteractable;
	float FocusAccumulator = 0.f;

	/** Interact is routed to gameplay systems in later features. */
	virtual void InteractPressed();
	virtual void InteractReleased();
	virtual void WhistlePressed();
};
