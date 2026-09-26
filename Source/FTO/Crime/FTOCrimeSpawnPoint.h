#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FTOCrimeSpawnPoint.generated.h"

/**
 * A place where crimes can happen. Placed by hand or by the city generator.
 * AllowedTemplates narrows what can spawn here (e.g. only "BankHeist" in the bank's vault).
 * Indoor points belong to a building; the perp stands on the point facing the way it faces.
 */
UCLASS()
class FTO_API AFTOCrimeSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AFTOCrimeSpawnPoint();

	/** Empty = any template. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crime")
	TArray<FName> AllowedTemplates;

	/** Free-form district tag, e.g. "Downtown", or the kind of building for indoor points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crime")
	FName District;

	/** Index into the city's buildings for points inside one (AFTOCityGenerator::GetBuildings), else INDEX_NONE. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crime")
	int32 BuildingIndex = INDEX_NONE;

	bool Allows(FName TemplateId) const { return AllowedTemplates.Num() == 0 || AllowedTemplates.Contains(TemplateId); }
	bool IsIndoors() const { return BuildingIndex != INDEX_NONE; }
};
