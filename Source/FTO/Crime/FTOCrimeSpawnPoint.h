#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FTOCrimeSpawnPoint.generated.h"

/**
 * A place where crimes can happen. Placed by hand or by the city generator.
 * AllowedTemplates narrows what can spawn here (e.g. only "BankHeist" at the bank).
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

	/** Free-form district tag, e.g. "Downtown". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crime")
	FName District;

	bool Allows(FName TemplateId) const { return AllowedTemplates.Num() == 0 || AllowedTemplates.Contains(TemplateId); }
};
