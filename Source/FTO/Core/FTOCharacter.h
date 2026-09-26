#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "FTOCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
struct FInputActionValue;

/**
 * A player officer. Until real art lands the officer is a chunky "bean cop"
 * built from engine primitives, tinted with the player's badge colour.
 */
UCLASS()
class FTO_API AFTOCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AFTOCharacter();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void OnRep_PlayerState() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Re-tints the uniform from the owning player state's badge colour. */
	void RefreshOfficerColor();

	UFUNCTION(BlueprintPure, Category="FTO")
	bool IsSprinting() const { return bSprinting; }

	UPROPERTY(EditDefaultsOnly, Category="FTO|Movement")
	float WalkSpeed = 500.f;

	UPROPERTY(EditDefaultsOnly, Category="FTO|Movement")
	float SprintSpeed = 850.f;

protected:
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

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> UniformMaterial;

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

	/** Interact is routed to gameplay systems in later features. */
	virtual void InteractPressed();
	virtual void InteractReleased();
	virtual void WhistlePressed();
};
