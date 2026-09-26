#include "City/FTOAmbientPopulation.h"
#include "City/FTOCityGenerator.h"
#include "City/FTOPedestrian.h"
#include "City/FTOTrafficCar.h"
#include "Engine/World.h"
#include "FTO.h"

AFTOAmbientPopulation::AFTOAmbientPopulation()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.f;
	bReplicates = false;
}

void AFTOAmbientPopulation::Populate(AFTOCityGenerator* InCity, int32 Seed)
{
	check(HasAuthority());
	City = InCity;
	Rng.Initialize(Seed ^ 0xC17F);

	if (!City)
	{
		return;
	}

	for (int32 i = 0; i < PedestrianCount; ++i)
	{
		SpawnPedestrian();
	}
	for (int32 i = 0; i < CarCount; ++i)
	{
		SpawnCar();
	}

	UE_LOG(LogFTO, Log, TEXT("City populated: %d pedestrians, %d cars."), PedestrianCount, Cars.Num());
}

void AFTOAmbientPopulation::SpawnPedestrian()
{
	const int32 BX = Rng.RandRange(0, City->NumIntersectionsX() - 2);
	const int32 BY = Rng.RandRange(0, City->NumIntersectionsY() - 2);
	const int32 Corner = Rng.RandRange(0, 3);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AFTOPedestrian* Ped = GetWorld()->SpawnActor<AFTOPedestrian>(AFTOPedestrian::StaticClass(), City->GetSidewalkCorner(BX, BY, Corner), FRotator::ZeroRotator, Params))
	{
		Ped->StartWandering(City, BX, BY, Corner, Rng.RandRange(1, MAX_int32 - 1));
	}
}

void AFTOAmbientPopulation::SpawnCar()
{
	const int32 I = Rng.RandRange(0, City->NumIntersectionsX() - 1);
	const int32 J = Rng.RandRange(0, City->NumIntersectionsY() - 1);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AFTOTrafficCar* Car = GetWorld()->SpawnActor<AFTOTrafficCar>(AFTOTrafficCar::StaticClass(), City->GetIntersection(I, J), FRotator::ZeroRotator, Params))
	{
		Car->StartDriving(City, I, J, Rng.RandRange(1, MAX_int32 - 1));
		Cars.Add(Car);
	}
}

void AFTOAmbientPopulation::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!City)
	{
		return;
	}

	// Replace cars that drove off into the sunset.
	Cars.RemoveAll([](const TObjectPtr<AFTOTrafficCar>& Car) { return !IsValid(Car); });
	if (Cars.Num() < CarCount)
	{
		SpawnCar();
	}
}
