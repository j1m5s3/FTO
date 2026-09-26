#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "FTOAnimatedActor.generated.h"

/**
 * Full-body "action" layered over locomotion. Each value plays the clip A_Officer_<Name>
 * (every character shares the officer rig), so adding a clip is: key it in
 * Tools/Blender/build_officer.py, import it, add a value here.
 */
UENUM(BlueprintType)
enum class EFTOAnimAction : uint8
{
	None,
	Interact,	// writing a ticket, handling a scene, chatting
	Cheer,		// shift survived!
	Sit,
	Drive,
	Talk,
	Work,		// cashier, typing, wiping the counter
	HandsUp,
	Kneel,		// on the knees, hands on head
	Cuffed,		// kneeling, hands cuffed behind
	Cuffing,	// officer applying the cuffs
	Struggle,	// resisting arrest
	Tackle,
	Punch,
	Cower,
	Dance,
	Slump,
	Dazed		// sat on the ground seeing stars
};

/** Upper-body weapon pose layered over whatever the legs are doing. */
UENUM(BlueprintType)
enum class EFTOAimPose : uint8
{
	None,
	Pistol,
	Rifle
};

UINTERFACE(MinimalAPI)
class UFTOAnimatedActor : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything animated by UFTOCharacterAnimInstance: officers, citizens, suspects.
 * Works for non-pawn actors too, so the ambient crowd shares the same clips.
 */
class FTO_API IFTOAnimatedActor
{
	GENERATED_BODY()

public:
	virtual EFTOAnimAction GetAnimAction() const { return EFTOAnimAction::None; }
	virtual bool IsAnimAirborne() const { return false; }
	/** Ground speed in cm/s used to pick idle/walk/run. */
	virtual float GetAnimSpeed() const = 0;
	virtual EFTOAimPose GetAimPose() const { return EFTOAimPose::None; }
	/** Degrees; positive looks up. */
	virtual float GetAimPitch() const { return 0.f; }
};
