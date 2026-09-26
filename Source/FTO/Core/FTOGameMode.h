#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FTOGameMode.generated.h"

class UFTOCrimeDirector;
class AFTOCityGenerator;

/**
 * Server-only rules for a shift at the precinct.
 * Caps the session at four officers, wires up the default FTO classes,
 * and owns the crime director that runs the shift.
 */
UCLASS()
class FTO_API AFTOGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFTOGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	UFTOCrimeDirector* GetCrimeDirector() const { return CrimeDirector; }

	/** Hard cap on officers in one precinct. */
	UPROPERTY(EditDefaultsOnly, Category="FTO")
	int32 MaxOfficers = 4;

	// ---- Debug console commands (host only) ----
	UFUNCTION(Exec) void FTOSpawnCrime(FName TemplateId);
	UFUNCTION(Exec) void FTOAddChaos(float Amount);
	UFUNCTION(Exec) void FTOSkipBriefing();
	UFUNCTION(Exec) void FTOEndShift(bool bSurvived = true);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="FTO")
	TObjectPtr<UFTOCrimeDirector> CrimeDirector;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="FTO")
	TObjectPtr<AFTOCityGenerator> CityGenerator;

	/** From the "?Seed=" URL option; 0 = random. */
	int32 ShiftSeed = 0;

	int32 PickFreeBadge(const APlayerController* ForPlayer) const;
};
