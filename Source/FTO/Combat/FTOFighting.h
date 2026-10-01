#pragma once

#include "CoreMinimal.h"
#include "Animation/FTOAnimatedActor.h"
#include "FTOFighting.generated.h"

class AActor;
class AController;

/** A hand-to-hand move. */
UENUM()
enum class EFTOMove : uint8
{
	None,
	Jab,
	Cross,
	Hook,
	Uppercut,
	KickFront,
	KickSide,
	KickRoundhouse,
	Shove,
	Grab,		// take hold (the clinch)...
	Throw		// ...and over they go
};

/** How a move plays and what it does (timing from the clip, Art/Source/Characters/Anims/fight_timing.json). */
struct FFTOMoveSpec
{
	EFTOAnimAction Action = EFTOAnimAction::None;
	/** The clip's length, and when in it the blow lands (seconds). */
	float Length = 0.5f;
	float Contact = 0.25f;
	/** How far from the root the blow reaches (cm). */
	float Reach = 80.f;
	/** How groggy it leaves them (out of 100: at 100 they're down). */
	float Daze = 10.f;
	/** How hard it shoves them (cm/s). */
	float Push = 250.f;
	/** A big one: puts them down once they're half gone, and staggers them back otherwise. */
	bool bHeavy = false;
	/** Where it lands on them. */
	const TCHAR* Bone = TEXT("head");
	/** The sound family it lands with (FTOAudio). */
	const TCHAR* Sound = TEXT("Punch");
};

/**
 * Hand-to-hand fighting, for everyone: officers punching, kicking, grabbing and throwing, suspects who'd rather fight
 * than come quietly, and brawlers going at it in a bar.
 *
 * A move plays over whatever the fighter's doing (UFTOKnockdownComponent::PlayMove, which every machine shows) and
 * lands at the clip's moment of contact on whoever's in reach in front. The one it lands on reels the way it came
 * from, their upper body knocked loose under physics for a moment (TakeBlow), and grows groggier; enough blows (or a
 * big one when they're already groggy, or a throw) and they go down for real (FTOImpact::Strike: a suspect put on
 * the floor by the police is caught). The police roughing up citizens costs chaos. Server only.
 */
namespace FTOFighting
{
	FTO_API const FFTOMoveSpec& Spec(EFTOMove Move);

	/**
	 * Attacker throws Move: plays it, and lands it at its moment of contact on whoever's then in reach in front (only
	 * Target, if given). ByPolice: the officer's controller, if an officer threw it. bStaged: a scrap for show (NPCs
	 * brawling with each other): it rocks them, but never floors them. False if they can't swing right now.
	 */
	FTO_API bool Swing(AActor* Attacker, EFTOMove Move, AController* ByPolice, AActor* Target = nullptr, bool bStaged = false);

	/** Whoever Attacker would hit right now with a blow reaching Reach cm (in front, on their feet), or only Only. */
	FTO_API AActor* FindTarget(const AActor* Attacker, float Reach, AActor* Only = nullptr);

	/** Victim takes Attacker's Move. */
	FTO_API void TakeHit(AActor* Victim, AActor* Attacker, EFTOMove Move, AController* ByPolice, bool bStaged);

	/** A move for a brawler to throw (mostly punches, the odd kick). */
	FTO_API EFTOMove PickBrawlerMove(FRandomStream& Rng);

	/** Can this actor throw a punch right now (on their feet, not mid-move or reeling)? */
	FTO_API bool CanSwing(const AActor* Fighter);
}
