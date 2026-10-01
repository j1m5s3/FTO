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
	/** Hand to hand: a kick (punches are Fire with no weapon up). */
	UPROPERTY() TObjectPtr<UInputAction> Kick;
	/** On foot: weapons. Draw brings the last weapon up (or puts it away); Slots are 1, 2, 3. */
	UPROPERTY() TObjectPtr<UInputAction> Draw;
	UPROPERTY() TObjectPtr<UInputAction> Fire;
	UPROPERTY() TObjectPtr<UInputAction> Reload;
	UPROPERTY() TObjectPtr<UInputAction> NextWeapon;
	UPROPERTY() TObjectPtr<UInputAction> PrevWeapon;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> Slots;
	/** Radio: hold to talk (V, d-pad down), and hold for the callout wheel (T, d-pad up). */
	UPROPERTY() TObjectPtr<UInputAction> Radio;
	UPROPERTY() TObjectPtr<UInputAction> RadioWheel;

	/**
	 * Laid over everything while the callout wheel is open (so the mouse and 1-4 don't also turn the camera or
	 * swap weapons): the mouse (RadioAim) nudges a cursor round the wheel and the right stick (RadioAimStick) points
	 * straight at a callout; Callouts[0..3] pick one outright.
	 */
	UPROPERTY() TObjectPtr<UInputMappingContext> WheelContext;
	UPROPERTY() TObjectPtr<UInputAction> RadioAim;
	UPROPERTY() TObjectPtr<UInputAction> RadioAimStick;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> Callouts;
	/** Talking to someone: 1-4 (or the d-pad) say one of the options, over the weapon keys. */
	UPROPERTY() TObjectPtr<UInputMappingContext> TalkContext;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> TalkOptions;
	/** End of shift: vote for overtime (Y, left bumper) or to clock off (N, view button). */
	UPROPERTY() TObjectPtr<UInputAction> VoteOvertime;
	UPROPERTY() TObjectPtr<UInputAction> VoteClockOff;

private:
	UInputAction* MakeAction(FName Name, int32 ValueType);
	void MapAxis2D(UInputAction* Action, FKey Up, FKey Down, FKey Left, FKey Right);
};
