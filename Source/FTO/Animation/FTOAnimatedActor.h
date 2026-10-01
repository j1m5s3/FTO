#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "FTOAnimatedActor.generated.h"

class USkeletalMeshComponent;

/**
 * Full-body "action" layered over locomotion. Each value plays our clip A_FTO_<Name> (Tools/Blender/character_clips.py,
 * on Epic's mannequin skeleton, which every character shares), so adding one is: key it there, export and import it,
 * add a value here (UFTOCharacterAnimInstance maps the odd one whose name differs).
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
	Dazed,		// sat on the ground seeing stars
	Ride,		// passenger seat, hands in the lap
	SitCuffed,	// back of the cruiser, cuffed and sulking
	SitHandsUp,	// busted at the wheel
	IdleBored,	// shifting from foot to foot, checking the time
	Phone,		// on the phone
	Wave,
	Point,
	Clipboard,	// writing a ticket
	Search,		// patting someone down
	SearchedPose,	// hands on the wall, being patted down
	Spray,		// spraying graffiti
	Smash,		// kicking and battering street furniture
	Grab,		// rummaging (a shoplifter filling a sack)
	Dance2,
	// Hand to hand (Combat/FTOFighting.h)
	Jab,
	Cross,
	Hook,
	Uppercut,
	KickFront,
	KickSide,
	KickRoundhouse,
	Shove,
	FightGrab,	// grabbing hold of someone (the clinch)
	Throw,		// and throwing them over
	Block,
	HitLightFront,	// rocked by a blow from in front (and so on round)
	HitLightBack,
	HitLightLeft,
	HitLightRight,
	HitHeavy,	// staggered back by a big one
	Taunt,
	FightIdle,	// fists up
	FightStepFwd,
	FightStepBack
};

/** Upper-body pose layered over whatever the legs are doing: a weapon up, or hands cuffed behind the back. */
UENUM(BlueprintType)
enum class EFTOAimPose : uint8
{
	None,
	Pistol,
	Rifle,
	Cuffed	// A_FTO_HandsBehind: walked to the cells in cuffs
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
	/** Per-mesh action, for actors that animate several people (a car's driver and passengers). */
	virtual EFTOAnimAction GetAnimActionFor(const USkeletalMeshComponent* Mesh) const { return GetAnimAction(); }
	virtual bool IsAnimAirborne() const { return false; }
	/** Ground speed in cm/s used to pick idle/walk/run. */
	virtual float GetAnimSpeed() const = 0;
	virtual EFTOAimPose GetAimPose() const { return EFTOAimPose::None; }
	/** Degrees; positive looks up. */
	virtual float GetAimPitch() const { return 0.f; }
	/** Bumps whenever the same action starts over (a second jab straight after the first). */
	virtual uint8 GetAnimActionSerial() const { return 0; }
};
