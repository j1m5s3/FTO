#include "Core/FTOGameMode.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Core/FTOPlayerState.h"
#include "Crime/FTOCrimeDirector.h"
#include "Kismet/GameplayStatics.h"
#include "FTO.h"

AFTOGameMode::AFTOGameMode()
{
	DefaultPawnClass = AFTOCharacter::StaticClass();
	PlayerControllerClass = AFTOPlayerController::StaticClass();
	PlayerStateClass = AFTOPlayerState::StaticClass();
	GameStateClass = AFTOGameState::StaticClass();

	CrimeDirector = CreateDefaultSubobject<UFTOCrimeDirector>(TEXT("CrimeDirector"));
}

void AFTOGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	RequestedSeed = UGameplayStatics::GetIntOption(Options, TEXT("Seed"), 0);
}

void AFTOGameMode::StartPlay()
{
	Super::StartPlay();

	const int32 Seed = RequestedSeed != 0 ? RequestedSeed : FMath::Rand();
	CrimeDirector->BeginShift(Seed);
}

void AFTOGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);

	if (ErrorMessage.IsEmpty() && GetNumPlayers() >= MaxOfficers)
	{
		ErrorMessage = TEXT("The precinct is fully staffed (4/4 officers).");
	}
}

void AFTOGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (AFTOPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AFTOPlayerState>() : nullptr)
	{
		// Badge numbers double as the officer's colour slot.
		PS->SetBadgeIndex(FMath::Clamp(GetNumPlayers() - 1, 0, MaxOfficers - 1));
	}

	UE_LOG(LogFTO, Log, TEXT("Officer joined: %s (%d/%d)"), *GetNameSafe(NewPlayer), GetNumPlayers(), MaxOfficers);
}

void AFTOGameMode::FTOSpawnCrime(FName TemplateId)
{
	if (!CrimeDirector->SpawnIncident(TemplateId, true))
	{
		UE_LOG(LogFTO, Warning, TEXT("Couldn't spawn crime '%s'."), *TemplateId.ToString());
	}
}

void AFTOGameMode::FTOAddChaos(float Amount)
{
	if (AFTOGameState* GS = GetGameState<AFTOGameState>())
	{
		GS->AddChaos(Amount);
	}
}
