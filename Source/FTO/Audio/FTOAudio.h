#pragma once

#include "CoreMinimal.h"
#include "FTOAudio.generated.h"

class USoundBase;
class UWorld;
struct FHitResult;

/** What's underfoot, for footsteps. */
UENUM()
enum class EFTOSurface : uint8
{
	Concrete,	// streets, pavements, warehouses
	Wood,		// homes and bars
	Tile,		// shops, diners, the precinct, the bank
	Carpet,		// offices
	Metal,		// car roofs and bonnets
	Grass		// lawns and parks
};

/**
 * Picking sounds: several takes of anything heard often (a gunshot, a punch, a footstep) so repeats don't sound
 * canned, and which footstep goes with which floor. The takes are SW_<Family>_01, _02... (Tools/Audio/fto_synth.py),
 * loaded into AFTOGameState's sound set.
 */
namespace FTOAudio
{
	struct FFamilySpec
	{
		FName Name;
		int32 Takes;
	};

	/** Every family and how many takes it has. */
	FTO_API const TArray<FFamilySpec>& Families();

	/** A random take of the same sound (Sound itself if it has no others). */
	FTO_API USoundBase* Vary(USoundBase* Sound);

	/** A random take from a family ("Punch", "Kick", "BodyFall", "Whoosh", "CarImpactHeavy", "Rubble"...). */
	FTO_API USoundBase* Pick(FName Family);

	/** One of the dispatcher's lines (Tools/Audio/dispatch_lines.json, voiced as SW_Dispatch_<Category>_<Take>). */
	struct FDispatchLine
	{
		const TCHAR* Category;
		int32 Take;
		const TCHAR* Text;
	};
	FTO_API TConstArrayView<FDispatchLine> DispatchLines();
	/** The voice for a line (null if it hasn't been imported). */
	FTO_API USoundBase* DispatchSound(const FDispatchLine& Line);

	/** A footstep on Surface (a running one hits harder). */
	FTO_API USoundBase* Step(EFTOSurface Surface, bool bRunning);

	/** What the ground under a downward trace is made of: rooms by what the building is, lawns by their paint. */
	FTO_API EFTOSurface SurfaceOf(const FHitResult& Ground);
}
