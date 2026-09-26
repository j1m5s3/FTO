#pragma once

#include "CoreMinimal.h"
#include "Weapons/FTOWeapons.h"

class AActor;
class AController;

/**
 * The rules for bowling people over and shooting them (server only): who can be knocked down, how far, and what it
 * costs. The police pay in chaos for hurting citizens (and it goes on the report card); a getaway car mowing people
 * down just makes the city angrier. Crooks and perps go down for free, and a perp who goes down is subdued (their
 * crime is handled). Officers hit by gunfire are downed until a partner helps them up.
 */
namespace FTOImpact
{
	/** Slower than this (cm/s), a car only nudges people. */
	constexpr float MinRunOverSpeed = 250.f;

	/** Can this actor be bowled over right now (it can be knocked down, and it's on its feet)? */
	FTO_API bool CanBowlOver(const AActor* Victim);

	/**
	 * Server: a car at CarLocation moving at Velocity hits Victim. Driver is whoever's at the wheel (null for
	 * NPC cars). Returns true if they went flying.
	 */
	FTO_API bool RunOver(AActor* Victim, const FVector& CarLocation, const FVector& Velocity, AController* Driver);

	/** Server: an officer's flying tackle, heading along Direction, lands on Victim. */
	FTO_API bool Tackle(AActor* Victim, const FVector& Direction, AController* Officer);

	/** Server: a round from Weapon, fired by Shooter (an officer, a perp), hits Victim at Velocity. */
	FTO_API bool Shot(AActor* Victim, const FVector& Velocity, EFTOWeapon Weapon, AActor* Shooter);
}
