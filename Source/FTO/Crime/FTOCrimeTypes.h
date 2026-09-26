#pragma once

#include "CoreMinimal.h"
#include "FTOCrimeTypes.generated.h"

/** How big a deal an incident is. Drives chaos, staffing and rarity. */
UENUM(BlueprintType)
enum class EFTOCrimeTier : uint8
{
	Petty,		// jaywalking, graffiti, cat in a tree
	Minor,		// domestic dispute, bar fight, traffic stop
	Major,		// armed robbery, car chase
	Critical	// bank heist, hostage situation, "terror" plot
};

UENUM(BlueprintType)
enum class EFTOIncidentState : uint8
{
	/** Happening, but nobody has called it in. Only visible to officers nearby. */
	Unreported,
	/** On the dispatch board, waiting for officers. */
	Reported,
	/** At least one officer is on scene. */
	Responding,
	Resolved,
	/** Nobody came; the incident went cold (or blew up into something worse). */
	Failed
};

/** A procedural building block: one kind of crime or call. */
USTRUCT(BlueprintType)
struct FTO_API FFTOCrimeTemplate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Title;
	/** Randomly chosen flavour text shown on the dispatch board. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FText> Flavor;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EFTOCrimeTier Tier = EFTOCrimeTier::Petty;

	/** Chaos added per second while the incident is unresolved. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ChaosPerSecond = 0.05f;
	/** Chaos removed when resolved. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ChaosRelief = 2.f;
	/** Seconds of on-scene work needed with a full crew. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ResolveSeconds = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 OfficersRequired = 1;

	/** Chance (0-1) a citizen calls it in. Otherwise officers must witness it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ReportChance = 0.7f;
	/** Seconds before a citizen's call reaches dispatch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector2D ReportDelay = FVector2D(3.f, 10.f);
	/** Seconds unattended before it escalates or goes cold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float TimeToEscalate = 60.f;
	/** Template this becomes if ignored. None = it goes cold instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName EscalatesTo;
	/** One-off chaos hit when it goes cold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FailPenalty = 3.f;

	/** Relative spawn weight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Weight = 1.f;
	/** Only spawns once city chaos reaches this level (0-100). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MinChaos = 0.f;
	/** Only one of these may be active at once (big set pieces). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bUnique = false;
};

/** Random twist layered onto a template to keep shifts fresh. */
USTRUCT(BlueprintType)
struct FTO_API FFTOCrimeModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Id;
	/** Prefix for the title, e.g. "Armed". Empty = use Suffix only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Prefix;
	/** Extra line for the dispatch board, e.g. "Suspect is dressed as a hot dog." */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Note;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ChaosMultiplier = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ResolveMultiplier = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 ExtraOfficers = 0;
	/** Lowest tier this modifier can apply to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EFTOCrimeTier MinTier = EFTOCrimeTier::Petty;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Weight = 1.f;
};

/** The rolled, concrete incident. Replicated so every client can show it. */
USTRUCT(BlueprintType)
struct FTO_API FFTOIncidentInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName TemplateId;
	UPROPERTY(BlueprintReadOnly) FText Title;
	UPROPERTY(BlueprintReadOnly) FText Description;
	UPROPERTY(BlueprintReadOnly) EFTOCrimeTier Tier = EFTOCrimeTier::Petty;
	UPROPERTY(BlueprintReadOnly) int32 OfficersRequired = 1;
	UPROPERTY(BlueprintReadOnly) float ResolveSeconds = 4.f;
	UPROPERTY(BlueprintReadOnly) float ChaosPerSecond = 0.05f;
	UPROPERTY(BlueprintReadOnly) float ChaosRelief = 2.f;
	UPROPERTY(BlueprintReadOnly) float FailPenalty = 3.f;
	UPROPERTY(BlueprintReadOnly) float TimeToEscalate = 60.f;
	UPROPERTY(BlueprintReadOnly) FName EscalatesTo;
};

namespace FTOCrime
{
	FTO_API FLinearColor TierColor(EFTOCrimeTier Tier);
	FTO_API FText TierName(EFTOCrimeTier Tier);
}
