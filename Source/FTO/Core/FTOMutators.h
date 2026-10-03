#pragma once

#include "CoreMinimal.h"

/**
 * The shift's mutator: one silly rule for the whole shift, rolled from the seed when the shift starts (the game state
 * replicates it; -FTOMutator=Name or the FTOMutator console command picks one).
 *  - LowGravity: everyone (and everything knocked flying) floats down slowly; jumps go a long way up.
 *  - BouncyCars: every car bounces along on hydraulics, and cruisers bounce off walls like rubber balls.
 *  - HotDogs: every suspect (and the crowd a pickpocket hides in) is dressed as a hot dog.
 *  - BigHeads: everyone's head is twice the size.
 */
namespace FTOMutators
{
	FTO_API const TArray<FName>& All();
	/** This shift's (None before it's rolled). */
	FTO_API FName Active(const UObject* WorldContext);
	FTO_API bool Is(const UObject* WorldContext, FName Which);
	/** The briefing's line for it ("LOW GRAVITY: ..."). */
	FTO_API FString Describe(FName Which);
	/** Gravity in low gravity, as a fraction of the usual. */
	constexpr float LowGravityScale = 0.35f;
}
