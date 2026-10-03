#include "Core/FTOGameMode.h"
#include "Core/FTOCareer.h"
#include "Core/FTOPrecinctBoard.h"
#include "City/FTOAmbientPopulation.h"
#include "City/FTOCityGenerator.h"
#include "City/FTOInteriorLife.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Core/FTOPlayerState.h"
#include "Crime/FTOCrimeDirector.h"
#include "UI/FTOHUD.h"
#include "Vehicles/FTOCruiser.h"
#include "Physics/FTODestruction.h"
#include "Physics/FTOKnockdownComponent.h"
#include "Dev/FTOAnimDummy.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Scoring/FTOScoring.h"
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
	CrimeDirector->ShiftNumber = UGameplayStatics::GetIntOption(Options, TEXT("Shift"), 0);
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

	if (AFTOGameState* GS = GetGameState<AFTOGameState>())
	{
		GS->ShiftSeed = ShiftSeed;
		GS->OnShiftPhaseChanged.AddDynamic(this, &AFTOGameMode::HandleShiftPhase);
		// The precinct's career (the host's save).
		GS->SetCareer(FTOCareer::Load(FTOCareer::SlotName()), false);
	}

	if (CityGenerator)
	{
		if (AFTOAmbientPopulation* Population = GetWorld()->SpawnActor<AFTOAmbientPopulation>(AFTOAmbientPopulation::StaticClass(), FTransform::Identity))
		{
			Population->Populate(CityGenerator, ShiftSeed);
		}
		// Everyone indoors: staff, customers, residents and the odd crook, wherever officers are.
		if (AFTOInteriorLife* Interiors = GetWorld()->SpawnActor<AFTOInteriorLife>(AFTOInteriorLife::StaticClass(), FTransform::Identity))
		{
			Interiors->Init(CityGenerator, ShiftSeed);
		}
		// Keeps track of what gets broken (windows, street furniture), for everyone.
		GetWorld()->SpawnActor<AFTODestruction>(AFTODestruction::StaticClass(), FTransform::Identity);
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

	// The locker-room and upgrades boards on the precinct's wall.
	if (const FFTOBuilding* Precinct = CityGenerator ? CityGenerator->FindBuilding(EFTOBuildingType::Precinct) : nullptr)
	{
		for (int32 i = 0; i < 2; ++i)
		{
			const FVector At = Precinct->Room.TransformPosition(FVector(220.f + i * 220.f, Precinct->YMin + 6.f, 0.f));
			const FRotator Facing = (Precinct->Room.GetRotation().GetRightVector()).Rotation();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AFTOPrecinctBoard* Board = GetWorld()->SpawnActor<AFTOPrecinctBoard>(AFTOPrecinctBoard::StaticClass(), At, Facing, Params))
			{
				Board->SetKind(i == 0 ? EFTOBoardKind::Locker : EFTOBoardKind::Upgrades);
			}
		}
	}

	// Officers gather in the lobby until the host starts the shift (-FTOQuickStart skips it).
	if (FParse::Param(FCommandLine::Get(), TEXT("FTOQuickStart")))
	{
		StartShift();
	}
}

void AFTOGameMode::StartShift()
{
	const AFTOGameState* GS = GetGameState<AFTOGameState>();
	if (GS && GS->GetShiftPhase() == EFTOShiftPhase::Lobby)
	{
		CrimeDirector->BeginShift(ShiftSeed);
	}
}

void AFTOGameMode::NewShift()
{
	const FString Map = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	const bool bListen = GetNetMode() == NM_ListenServer;
	const int32 NextSeed = FMath::RandRange(1, MAX_int32 - 1);
	GetWorld()->ServerTravel(FString::Printf(TEXT("%s?Seed=%d?Shift=%d%s"), *Map, NextSeed, CrimeDirector->ShiftNumber + 1, bListen ? TEXT("?listen") : TEXT("")), true);
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

void AFTOGameMode::FTOSetPiece(FName Which)
{
	FTOSetPieceNow(Which);
}

void AFTOGameMode::FTOCareerPoints(int32 Points)
{
	if (AFTOGameState* GS = GetGameState<AFTOGameState>())
	{
		FFTOCareerState Career = GS->GetCareer();
		Career.Earned += Points;
		Career.Bank += Points;
		GS->SetCareer(Career, true);
	}
}

void AFTOGameMode::FTOCareerReset()
{
	if (AFTOGameState* GS = GetGameState<AFTOGameState>())
	{
		GS->SetCareer(FFTOCareerState(), true);
	}
}

void AFTOGameMode::FTOMutator(FName Which)
{
	if (AFTOGameState* GS = GetGameState<AFTOGameState>())
	{
		GS->SetMutator(Which == TEXT("None") ? NAME_None : Which);
	}
}

AFTOIncident* AFTOGameMode::FTOSetPieceNow(FName Which)
{
	return CrimeDirector->StartSetPiece(Which);
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
		StartShift();
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

void AFTOGameMode::FTOKnockdown(float Radius)
{
	// Debug: bowl over everyone near the first player.
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Center = PC ? PC->GetPawn() : nullptr;
	if (!Center)
	{
		return;
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		UFTOKnockdownComponent* Target = It->FindComponentByClass<UFTOKnockdownComponent>();
		if (Target && FVector::DistSquared(It->GetActorLocation(), Center->GetActorLocation()) < FMath::Square(Radius))
		{
			const FVector Away = (It->GetActorLocation() - Center->GetActorLocation()).GetSafeNormal2D();
			Target->Knockdown(Away * 450.f + FVector(FMath::FRandRange(-150.f, 150.f), FMath::FRandRange(-150.f, 150.f), 550.f), 3.5f);
		}
	}
}

void AFTOGameMode::FTOAnimGallery()
{
	// Debug: two rows of mannequins in front of the first player, one per action/aim clip.
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Center = PC ? PC->GetPawn() : nullptr;
	if (!Center)
	{
		return;
	}
	for (TActorIterator<AFTOAnimDummy> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}

	struct FEntry { EFTOAnimAction Action; EFTOAimPose Aim; };
	TArray<FEntry> Entries;
	const UEnum* Actions = StaticEnum<EFTOAnimAction>();
	for (int32 i = 0; i < Actions->NumEnums() - 1; ++i)
	{
		const EFTOAnimAction Action = static_cast<EFTOAnimAction>(Actions->GetValueByIndex(i));
		if (Action != EFTOAnimAction::None)
		{
			Entries.Add({ Action, EFTOAimPose::None });
		}
	}
	Entries.Add({ EFTOAnimAction::None, EFTOAimPose::Pistol });
	Entries.Add({ EFTOAnimAction::None, EFTOAimPose::Rifle });
	Entries.Add({ EFTOAnimAction::None, EFTOAimPose::Cuffed });

	const FVector Fwd = Center->GetActorForwardVector();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);
	const FVector Feet = Center->GetActorLocation() - FVector(0.f, 0.f, 96.f);
	const int32 PerRow = (Entries.Num() + 1) / 2;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const int32 Row = i / PerRow;
		const int32 Col = i % PerRow;
		const FVector Location = Feet + Fwd * (900.f + Row * 320.f) + Right * ((Col - (PerRow - 1) * 0.5f) * 160.f);
		const FRotator FacePlayer(0.f, (-Fwd).Rotation().Yaw, 0.f);
		if (AFTOAnimDummy* Dummy = GetWorld()->SpawnActor<AFTOAnimDummy>(AFTOAnimDummy::StaticClass(), Location, FacePlayer))
		{
			Dummy->Setup(Entries[i].Action, Entries[i].Aim, (i % 2) == 0);
		}
	}
}

void AFTOGameMode::HandleShiftPhase(EFTOShiftPhase NewPhase)
{
	if (NewPhase == EFTOShiftPhase::Survived || NewPhase == EFTOShiftPhase::Overrun)
	{
		GatherForDebrief();
		// Pay the precinct: a tenth of the squad's score, and a bonus for getting through it. Surviving raises the level.
		if (AFTOGameState* GS = GetGameState<AFTOGameState>())
		{
			int32 TeamScore = 0;
			for (const APlayerState* PS : GS->PlayerArray)
			{
				if (const AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS))
				{
					TeamScore += Officer->GetShiftScore();
				}
			}
			FFTOCareerState Career = GS->GetCareer();
			const bool bSurvived = NewPhase == EFTOShiftPhase::Survived;
			Career.LastEarned = FMath::Max(0, TeamScore / 10) + (bSurvived ? 150 : 0);
			Career.Earned += Career.LastEarned;
			Career.Bank += Career.LastEarned;
			++Career.ShiftsPlayed;
			Career.ShiftsSurvived += bSurvived ? 1 : 0;
			GS->SetCareer(Career, true);
			UE_LOG(LogFTO, Log, TEXT("Career: +%d points (%d earned, %d banked), %d shifts survived of %d."), Career.LastEarned, Career.Earned, Career.Bank,
				Career.ShiftsSurvived, Career.ShiftsPlayed);
		}
	}
}

void AFTOGameMode::GatherForDebrief()
{
	// Everyone out of the cars and up off the floor, lined up by badge outside the precinct for the scoreboard (the
	// squad dances if the city survived, slumps if it didn't; see AFTOCharacter::GetAnimAction).
	TArray<AFTOCharacter*> Squad;
	for (TActorIterator<AFTOCharacter> It(GetWorld()); It; ++It)
	{
		const AFTOPlayerState* PS = It->GetPlayerState<AFTOPlayerState>();
		const AFTOCruiser* Cruiser = Cast<AFTOCruiser>(It->GetCurrentVehicle());
		if (!PS && Cruiser && Cruiser->GetDriver() == *It)
		{
			PS = Cruiser->GetPlayerState<AFTOPlayerState>();
		}
		if (PS)
		{
			Squad.Add(*It);
		}
	}
	auto BadgeOf = [](const AFTOCharacter& Officer)
	{
		const AFTOPlayerState* PS = Officer.GetPlayerState<AFTOPlayerState>();
		const AFTOCruiser* Cruiser = Cast<AFTOCruiser>(Officer.GetCurrentVehicle());
		PS = PS ? PS : (Cruiser ? Cruiser->GetPlayerState<AFTOPlayerState>() : nullptr);
		return PS ? PS->GetBadgeIndex() : 0;
	};
	Squad.Sort([&BadgeOf](const AFTOCharacter& A, const AFTOCharacter& B) { return BadgeOf(A) < BadgeOf(B); });

	TArray<FTransform> Spots;
	FTransform Camera;
	if (!FTOScoring::DebriefSpots(GetWorld(), Squad.Num(), Spots, Camera))
	{
		return;
	}
	for (int32 i = 0; i < Squad.Num(); ++i)
	{
		AFTOCharacter* Officer = Squad[i];
		if (AFTOCruiser* Cruiser = Cast<AFTOCruiser>(Officer->GetCurrentVehicle()))
		{
			Cruiser->LetOut(Officer);
		}
		// Whatever arrest they were in the middle of is off (the suspect goes back to kneeling or runs for it).
		Officer->EndSyncedAction();
		if (UFTOKnockdownComponent* Knockdown = Officer->GetKnockdown(); Knockdown && Knockdown->IsDown())
		{
			Knockdown->Recover();
		}
		const float HalfHeight = Officer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Officer->TeleportTo(Spots[i].GetLocation() + FVector(0.f, 0.f, HalfHeight + 5.f), Spots[i].Rotator(), false, true);
		if (AController* Controller = Officer->GetController())
		{
			Controller->SetControlRotation(Spots[i].Rotator());
		}
	}
	// Then point everyone at the line-up: facing on their own machine (control rotation doesn't replicate) and the
	// debrief camera (reliable, after any "out of the car" restart, so that can't snap the camera back to the pawn).
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(It->Get()))
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				PC->ClientSetRotation(Pawn->GetActorRotation());
			}
			PC->ClientShowDebrief();
		}
	}
}
void AFTOGameMode::FTOShiftTimeLeft(float Seconds)
{
	if (AFTOGameState* GS = GetGameState<AFTOGameState>(); GS && GS->GetShiftPhase() == EFTOShiftPhase::OnDuty)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		GS->SetShiftTimes(Now, Now + FMath::Max(0.f, Seconds));
	}
}