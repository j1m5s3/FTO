#include "Crime/FTOCrimeSpawnPoint.h"
#include "Components/SceneComponent.h"

AFTOCrimeSpawnPoint::AFTOCrimeSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}
