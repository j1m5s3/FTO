#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/FTOInteractable.h"
#include "Weapons/FTOWeapons.h"
#include "FTOArmoryRack.generated.h"

class UBoxComponent;

/**
 * A rack in the precinct armory that hands out one kind of weapon: walk up and press E to take one (into a free
 * slot, or swapped for the one in hand when all three are full), or to restock its ammo if you already carry it.
 * The rack itself is the kit's SM_GunRack; this is the invisible counter in front of it.
 */
UCLASS()
class FTO_API AFTOArmoryRack : public AActor, public IFTOInteractable
{
	GENERATED_BODY()

public:
	AFTOArmoryRack();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: what this rack hands out. */
	void SetWeapon(EFTOWeapon InWeapon) { Weapon = InWeapon; }
	EFTOWeapon GetWeapon() const { return Weapon; }

	// IFTOInteractable
	virtual bool CanInteract(const AFTOCharacter* Officer) const override;
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual void Interact(AFTOCharacter* Officer) override;
	virtual FVector GetInteractLocation() const override { return GetActorLocation(); }
	virtual float GetInteractRange() const override { return 250.f; }

protected:
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UBoxComponent> Box;
	UPROPERTY(Replicated) EFTOWeapon Weapon = EFTOWeapon::Pistol;
};
