#pragma once

#include "CoreMinimal.h"
#include "Crime/FTOCrimeTypes.h"
#include "Engine/DataAsset.h"
#include "FTOCrimeCatalog.generated.h"

/**
 * Every crime template and modifier the director can roll.
 * Ships with a built-in default set; designers can make a data asset
 * of this class to override it without code changes.
 */
UCLASS(BlueprintType)
class FTO_API UFTOCrimeCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Crime")
	TArray<FFTOCrimeTemplate> Templates;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Crime")
	TArray<FFTOCrimeModifier> Modifiers;

	/** Chance (0-1) that a rolled incident gets a modifier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Crime")
	float ModifierChance = 0.5f;

	const FFTOCrimeTemplate* FindTemplate(FName Id) const;

	/** Fills in the built-in crime list. */
	void PopulateDefaults();

	/** Builds a concrete incident from a template, optionally adding a modifier. */
	FFTOIncidentInfo RollIncident(const FFTOCrimeTemplate& Template, FRandomStream& Rng) const;
};
