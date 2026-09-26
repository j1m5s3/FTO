#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "FTOAnimatedActor.generated.h"

/** Full-body "action" layered over locomotion. */
UENUM(BlueprintType)
enum class EFTOAnimAction : uint8
{
	None,
	Interact,	// writing a ticket, handling a scene, chatting
	Cheer		// shift survived!
};

UINTERFACE(MinimalAPI)
class UFTOAnimatedActor : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything animated by UFTOCharacterAnimInstance: officers, and later citizens.
 * Works for non-pawn actors too, so the ambient crowd can share the same clips.
 */
class FTO_API IFTOAnimatedActor
{
	GENERATED_BODY()

public:
	virtual EFTOAnimAction GetAnimAction() const { return EFTOAnimAction::None; }
	virtual bool IsAnimAirborne() const { return false; }
	/** Ground speed in cm/s used to pick idle/walk/run. */
	virtual float GetAnimSpeed() const = 0;
};
