#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FTOAmbientPopulation.generated.h"

class AFTOCityGenerator;
class AFTOPedestrian;
class AFTOTrafficCar;

/**
 * Server-only: fills the generated city with pedestrians and traffic,
 * and tops traffic back up when cars leave (e.g. a wanted driver escapes).
 */
UCLASS()
class FTO_API AFTOAmbientPopulation : public AActor
{
	GENERATED_BODY()

public:
	AFTOAmbientPopulation();

	void Populate(AFTOCityGenerator* InCity, int32 Seed);

	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category="Population") int32 PedestrianCount = 70;
	UPROPERTY(EditAnywhere, Category="Population") int32 CarCount = 26;

protected:
	void SpawnPedestrian();
	void SpawnCar();

	UPROPERTY(Transient) TObjectPtr<AFTOCityGenerator> City;
	UPROPERTY(Transient) TArray<TObjectPtr<AFTOTrafficCar>> Cars;

	FRandomStream Rng;
	float RefillAccumulator = 0.f;
};
