#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "FTOCareer.generated.h"

/** The precinct's career, as the game state shares it with everyone (the host's save, on the host). */
USTRUCT(BlueprintType)
struct FFTOCareerState
{
	GENERATED_BODY()

	/** Career points ever earned (unlocks outfits and liveries), and what's left to spend on upgrades. */
	UPROPERTY(BlueprintReadOnly) int32 Earned = 0;
	UPROPERTY(BlueprintReadOnly) int32 Bank = 0;
	UPROPERTY(BlueprintReadOnly) int32 ShiftsPlayed = 0;
	UPROPERTY(BlueprintReadOnly) int32 ShiftsSurvived = 0;
	/** Precinct upgrades bought. */
	UPROPERTY(BlueprintReadOnly) TArray<FName> Upgrades;
	/** The fleet's livery. */
	UPROPERTY(BlueprintReadOnly) FName Livery = TEXT("Standard");
	/** What the last shift earned (for the scoreboard). */
	UPROPERTY(BlueprintReadOnly) int32 LastEarned = 0;

	bool Has(FName Upgrade) const { return Upgrades.Contains(Upgrade); }
};

/** The career on disk (SaveGameToSlot, the host's machine). */
UCLASS()
class FTO_API UFTOCareerSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() FFTOCareerState State;
};

/**
 * Progression. Every shift banks career points (a tenth of the squad's score, and a bonus for surviving it). Points ever
 * earned raise the precinct's rank, unlocking outfits for the officers and liveries for the fleet; points in the bank
 * buy precinct upgrades. Every shift survived raises the precinct's level: crimes come a little faster and do a little
 * more damage (up to level 10). Saved on the host; everyone in the squad shares it.
 */
namespace FTOCareer
{
	/** An outfit, a livery or an upgrade: what it's called, what it takes (points earned, or the price), and a line. */
	struct FItem
	{
		FName Id;
		const TCHAR* Name;
		int32 Points;
		const TCHAR* Blurb;
	};
	FTO_API TConstArrayView<FItem> Outfits();
	FTO_API TConstArrayView<FItem> Liveries();
	FTO_API TConstArrayView<FItem> Upgrades();
	FTO_API const FItem* Find(TConstArrayView<FItem> Items, FName Id);

	/** The precinct's rank for points earned ("Rookie" ... "Commissioner"). */
	FTO_API FString RankFor(int32 Earned);
	/** The difficulty level (0-10) for shifts survived. */
	FTO_API int32 LevelFor(const FFTOCareerState& State);

	/** Load the career from Slot (a fresh one if there isn't a save), and save it. */
	FTO_API FFTOCareerState Load(const FString& Slot);
	FTO_API void Save(const FString& Slot, const FFTOCareerState& State);
	/** The save slot: -FTOCareerSlot=Name, else "FTOCareer". */
	FTO_API FString SlotName();

	/** An outfit's shirt colour (Badge: the officer's badge colour). */
	FTO_API bool OutfitColor(FName Outfit, FLinearColor& OutColor);
	/** A livery's paint and how it handles (top speed and toughness multipliers). */
	FTO_API void LiveryStats(FName Livery, FLinearColor& OutPaint, bool& bOutRepaint, float& OutSpeed, float& OutToughness);
}
