#include "Weapons/FTOArmoryRack.h"
#include "Components/BoxComponent.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/FTOBallistics.h"

AFTOArmoryRack::AFTOArmoryRack()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	// Something for an officer's interact search to find (it doesn't block anyone, or any bullet).
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->InitBoxExtent(FVector(30.f, 90.f, 60.f));
	Box->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Box->SetCollisionResponseToChannel(ECC_FTOProjectile, ECR_Ignore);
	Box->SetGenerateOverlapEvents(false);
	RootComponent = Box;
}

void AFTOArmoryRack::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOArmoryRack, Weapon);
}

bool AFTOArmoryRack::CanInteract(const AFTOCharacter* Officer) const
{
	return Officer && Weapon != EFTOWeapon::None && !Officer->GetCurrentVehicle();
}

FText AFTOArmoryRack::GetInteractPrompt(const AFTOCharacter* Officer) const
{
	const FText Name = FTOWeapons::DisplayName(Weapon);
	if (Officer && Officer->HasWeapon(Weapon))
	{
		return FText::Format(INVTEXT("Restock {0} ammo"), Name);
	}
	if (Officer && !Officer->HasFreeSlot())
	{
		const EFTOWeapon InHand = Officer->GetDrawnWeapon() != EFTOWeapon::None ? Officer->GetDrawnWeapon() : Officer->GetWeaponInSlot(FTOWeapons::MaxSlots - 1);
		return FText::Format(INVTEXT("Swap your {0} for a {1}"), FTOWeapons::DisplayName(InHand), Name);
	}
	return FText::Format(INVTEXT("Take a {0}"), Name);
}

void AFTOArmoryRack::Interact(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (!CanInteract(Officer))
	{
		return;
	}
	const bool bRestock = Officer->HasWeapon(Weapon);
	Officer->GiveWeapon(Weapon);
	Officer->PlayTimedAction(EFTOAnimAction::Interact, 0.8f);
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastPlaySound(AFTOGameState::Sounds().Reload, GetActorLocation(), 0.9f);
	}
	if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(Officer->GetController()))
	{
		const FText Name = FTOWeapons::DisplayName(Weapon);
		PC->ClientToast(bRestock ? FText::Format(INVTEXT("{0} restocked."), Name)
			: FText::Format(INVTEXT("Signed out a {0}. Right mouse to raise it, left to fire, R to reload."), Name), FLinearColor(0.6f, 0.85f, 1.f));
	}
}
