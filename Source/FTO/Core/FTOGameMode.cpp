#include "Core/FTOGameMode.h"
#include "City/FTOAmbientPopulation.h"
#include "City/FTOCityGenerator.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Core/FTOPlayerState.h"
#include "Crime/FTOCrimeDirector.h"
#include "UI/FTOHUD.h"
#include "Vehicles/FTOCruiser.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "FTO.h"

AFTOGameMode::AFTOGameMode()
{
	DefaultPawnClass = AFTOCharacter::StaticClass();
	PlayerControllerClass = AFTOPlayerController::StaticClass();
	PlayerStateClass = AFTOPlayerState::StaticClass();
	GameStateClass = AFTOGameState::StaticClass();
	HUDClass = AFTOHUD::StaticClass();

	CrimeDirector = CreateDefaultSubobject<UFTOCrimeDirector>(TEXT("CrimeDirector"));
}

void AFTOGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	ShiftSeed = UGameplayStatics::GetIntOption(Options, TEXT("Seed"), 0);
	if (ShiftSeed == 0)
	{
		ShiftSeed = FMath::RandRange(1, MAX_int32 - 1);
	}

	// Use a hand-built city if the level has one, otherwise generate one now so the
	// precinct player starts exist before anyone spawns.
	for (TActorIterator<AFTOCityGenerator> It(GetWorld()); It; ++It)
	{
		CityGenerator = *It;
		break;
	}
	if (!CityGenerator)
	{
		CityGenerator = GetWorld()->SpawnActor<AFTOCityGenerator>(AFTOCityGenerator::StaticClass(), FTransform::Identity);
	}
	if (CityGenerator)
	{
		CityGenerator->ServerGenerate(ShiftSeed);
	}
}

void AFTOGameMode::StartPlay()
{
	Super::StartPlay();

	if (CityGenerator)
	{
		if (AFTOAmbientPopulation* Population = GetWorld()->SpawnActor<AFTOAmbientPopulation>(AFTOAmbientPopulation::StaticClass(), FTransform::Identity))
		{
			Population->Populate(CityGenerator, ShiftSeed);
		}
	}

	// A cruiser per badge colour, parked in the precinct lot.
	if (CityGenerator)
	{
		const TArray<FTransform> Spots = CityGenerator->GetPrecinctParkingSpots();
		for (int32 i = 0; i < Spots.Num() && i < MaxOfficers; ++i)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			FTransform Spawn = Spots[i];
			Spawn.AddToTranslation(FVector(0.f, 0.f, AFTOCruiser::RideHeight));
			if (AFTOCruiser* Cruiser = GetWorld()->SpawnActor<AFTOCruiser>(AFTOCruiser::StaticClass(), Spawn, Params))
			{
				Cruiser->SetStripeColor(AFTOPlayerState::ColorForBadge(i));
			}
		}
	}

	CrimeDirector->BeginShift(ShiftSeed);
}

void AFTOGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);

	if (ErrorMessage.IsEmpty() && GetNumPlayers() >= MaxOfficers)
	{
		ErrorMessage = TEXT("The precinct is fully staffed (4/4 officers).");
	}
}

int32 AFTOGameMode::PickFreeBadge(const APlayerController* ForPlayer) const
{
	TSet<int32> Taken;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC != ForPlayer)
		{
			if (const AFTOPlayerState* PS = PC->GetPlayerState<AFTOPlayerState>())
			{
				Taken.Add(PS->GetBadgeIndex());
			}
		}
	}
	for (int32 Badge = 0; Badge < MaxOfficers; ++Badge)
	{
		if (!Taken.Contains(Badge))
		{
			return Badge;
		}
	}
	return 0;
}

void AFTOGameMode::PostLogin(APlayerController* NewPlayer)
{
	// Assign the badge before Super spawns the pawn, so the officer picks the right start and colour.
	if (AFTOPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AFTOPlayerState>() : nullptr)
	{
		PS->SetBadgeIndex(PickFreeBadge(NewPlayer));
	}

	Super::PostLogin(NewPlayer);

	UE_LOG(LogFTO, Log, TEXT("Officer joined: %s (%d/%d)"), *GetNameSafe(NewPlayer), GetNumPlayers(), MaxOfficers);
}

AActor* AFTOGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	TArray<APlayerStart*> PrecinctStarts;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		if (It->PlayerStartTag == TEXT("Precinct"))
		{
			PrecinctStarts.Add(*It);
		}
	}

	if (PrecinctStarts.Num() > 0)
	{
		const AFTOPlayerState* PS = Player ? Player->GetPlayerState<AFTOPlayerState>() : nullptr;
		const int32 Badge = PS ? PS->GetBadgeIndex() : 0;
		return PrecinctStarts[Badge % PrecinctStarts.Num()];
	}

	return Super::ChoosePlayerStart_Implementation(Player);
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

void AFTOGameMode::FTOSkipBriefing()
{
	if (AFTOGameState* GS = GetGameState<AFTOGameState>())
	{
		if (GS->GetShiftPhase() == EFTOShiftPhase::Briefing)
		{
			const float Now = GetWorld()->GetTimeSeconds();
			GS->SetShiftTimes(Now, Now + CrimeDirector->ShiftLengthSeconds);
			GS->SetShiftPhase(EFTOShiftPhase::OnDuty);
		}
	}
}

void AFTOGameMode::FTOEndShift(bool bSurvived)
{
	if (AFTOGameState* GS = GetGameState<AFTOGameState>())
	{
		GS->SetShiftPhase(bSurvived ? EFTOShiftPhase::Survived : EFTOShiftPhase::Overrun);
	}
}
