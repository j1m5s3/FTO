#pragma once

#include "CoreMinimal.h"

class AActor;
class AController;

/**
 * The rules for bowling people over (server only): who can be knocked flying, how far, and what it costs.
 * The police pay in chaos for flattening citizens (and it goes on the report card); a getaway car mowing
 * people down just makes the city angrier. Crooks and fellow officers go flying for free.
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
}
