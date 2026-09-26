#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FTOInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * Enhanced Input actions and default key bindings built at runtime,
 * so the project boots without any binary input assets.
 * Swap for authored assets later without touching gameplay code.
 */
UCLASS()
class FTO_API UFTOInputConfig : public UObject
{
	GENERATED_BODY()

public:
	/** Creates all actions and the default mapping context. */
	void Build();

	UPROPERTY() TObjectPtr<UInputMappingContext> DefaultContext;

	UPROPERTY() TObjectPtr<UInputAction> Move;
	UPROPERTY() TObjectPtr<UInputAction> Look;
	UPROPERTY() TObjectPtr<UInputAction> Jump;
	UPROPERTY() TObjectPtr<UInputAction> Sprint;
	UPROPERTY() TObjectPtr<UInputAction> Interact;
	UPROPERTY() TObjectPtr<UInputAction> Whistle;
	UPROPERTY() TObjectPtr<UInputAction> Menu;
	UPROPERTY() TObjectPtr<UInputAction> Horn;
	/** In a vehicle: swap between the chase camera and the view from the seat. */
	UPROPERTY() TObjectPtr<UInputAction> Camera;
	/** On foot: flying tackle. */
	UPROPERTY() TObjectPtr<UInputAction> Tackle;
	/** On foot: weapons. Draw brings the last weapon up (or puts it away); Slots are 1, 2, 3. */
	UPROPERTY() TObjectPtr<UInputAction> Draw;
	UPROPERTY() TObjectPtr<UInputAction> Fire;
	UPROPERTY() TObjectPtr<UInputAction> Reload;
	UPROPERTY() TObjectPtr<UInputAction> NextWeapon;
	UPROPERTY() TObjectPtr<UInputAction> PrevWeapon;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> Slots;

private:
	UInputAction* MakeAction(FName Name, int32 ValueType);
	void MapAxis2D(UInputAction* Action, FKey Up, FKey Down, FKey Left, FKey Right);
};
