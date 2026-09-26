#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FTOGameMode.generated.h"

/**
 * Server-only rules for a shift at the precinct.
 * Caps the session at four officers and wires up the default FTO classes.
 */
UCLASS()
class FTO_API AFTOGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFTOGameMode();

	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;

	/** Hard cap on officers in one precinct. */
	UPROPERTY(EditDefaultsOnly, Category="FTO")
	int32 MaxOfficers = 4;
};
