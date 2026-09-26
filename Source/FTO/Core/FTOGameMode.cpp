#include "Core/FTOGameMode.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Core/FTOPlayerState.h"
#include "FTO.h"

AFTOGameMode::AFTOGameMode()
{
	DefaultPawnClass = AFTOCharacter::StaticClass();
	PlayerControllerClass = AFTOPlayerController::StaticClass();
	PlayerStateClass = AFTOPlayerState::StaticClass();
	GameStateClass = AFTOGameState::StaticClass();
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
