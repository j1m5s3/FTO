#include "Dev/FTOBotPilot.h"
#include "City/FTOCityGenerator.h"
#include "City/FTOLift.h"
#include "City/FTOPedestrian.h"
#include "City/FTOTrafficCar.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameMode.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Core/FTOPlayerState.h"
#include "Crime/FTOArrestee.h"
#include "Crime/FTOBomb.h"
#include "Crime/FTOCrimeExtra.h"
#include "Crime/FTOIncident.h"
#include "Crime/FTOPerp.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Interaction/FTOTalkable.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Physics/FTOKnockdownComponent.h"
#include "Scoring/FTOScoring.h"
#include "UnrealClient.h"
#include "Vehicles/FTOCruiser.h"
#include "Physics/FTOVehicleDamage.h"
#include "Weapons/FTOArmoryRack.h"
#include "FTO.h"
#include "Components/BoxComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"

namespace
{
	/** Shifts played by this process (bots carry on across New shift's map change). */
	int32 GShiftsPlayed = 0;

	/** Does one description fit another ("red top, bald..." vs "red top, bald..., carrying a sack")? */
	bool Matches(const FString& Look, const FString& Wanted)
	{
		return !Look.IsEmpty() && (Wanted.StartsWith(Look) || Look.StartsWith(Wanted));
	}

	/** Which wire a riddle means, the way a player would work it out. */
	int32 WireForClue(const FString& Clue)
	{
		const FString C = Clue.ToLower();
		for (const TCHAR* Word : { TEXT("fire engine"), TEXT("tomato"), TEXT("stop sign") }) { if (C.Contains(Word)) return 0; }
		for (const TCHAR* Word : { TEXT("sky"), TEXT("sea"), TEXT("blueberry") }) { if (C.Contains(Word)) return 1; }
		for (const TCHAR* Word : { TEXT("frog"), TEXT("grass"), TEXT("envy") }) { if (C.Contains(Word)) return 2; }
		for (const TCHAR* Word : { TEXT("duck"), TEXT("banana"), TEXT("smiley") }) { if (C.Contains(Word)) return 3; }
		return FMath::RandRange(0, 3);
	}

	/** Which of a drunk's answers sounds kind, the way a player would read them. */
	int32 KindAnswer(const TArray<FText>& Options)
	{
		static const TCHAR* Kind[] = { TEXT("water"), TEXT("home"), TEXT("leave the"), TEXT("taxi"), TEXT("listening"), TEXT("sit down"), TEXT("long night"), TEXT("mate") };
		for (int32 i = 0; i < FMath::Min(3, Options.Num()); ++i)
		{
			const FString Line = Options[i].ToString().ToLower();
			for (const TCHAR* Word : Kind)
			{
				if (Line.Contains(Word))
				{
					return i;
				}
			}
		}
		return FMath::RandRange(0, 2);
	}
}

AFTOBotPilot::AFTOBotPilot()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

bool AFTOBotPilot::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("FTOBotPlay"));
}

void AFTOBotPilot::BeginPlay()
{
	Super::BeginPlay();
	FParse::Value(FCommandLine::Get(), TEXT("FTOBotStyle="), Style);
	FParse::Value(FCommandLine::Get(), TEXT("FTOBotTag="), Tag);
	FParse::Value(FCommandLine::Get(), TEXT("FTOBotShifts="), ShiftsWanted);
	FParse::Value(FCommandLine::Get(), TEXT("FTOBotShots="), ShotEvery);
	FParse::Value(FCommandLine::Get(), TEXT("FTOBotWaitFor="), WaitFor);
	StartedAt = GetWorld()->GetRealTimeSeconds();
	NextShot = StartedAt + 8.f;
	Say(FString::Printf(TEXT("starting: style %s, %d shift(s), a screenshot every %.0f s (shift %d of this run)."), *Style, ShiftsWanted, ShotEvery, GShiftsPlayed + 1));
}

AFTOCharacter* AFTOBotPilot::Me() const
{
	const APlayerController* Controller = PC();
	if (AFTOCharacter* OnFoot = Controller ? Cast<AFTOCharacter>(Controller->GetPawn()) : nullptr)
	{
		return OnFoot;
	}
	const AFTOCruiser* Car = MyCar();
	return Car ? Car->GetDriver() : nullptr;
}

AFTOCruiser* AFTOBotPilot::MyCar() const
{
	const APlayerController* Controller = PC();
	return Controller ? Cast<AFTOCruiser>(Controller->GetPawn()) : nullptr;
}

APlayerController* AFTOBotPilot::PC() const
{
	return GetWorld()->GetFirstPlayerController();
}

AFTOGameState* AFTOBotPilot::GS() const
{
	return GetWorld()->GetGameState<AFTOGameState>();
}

FVector AFTOBotPilot::Here() const
{
	const APlayerController* Controller = PC();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	return Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
}

bool AFTOBotPilot::Close(const FVector& Where, float Distance) const
{
	return FVector::Dist2D(Here(), Where) < Distance && FMath::Abs(Here().Z - Where.Z) < 250.f;
}

void AFTOBotPilot::Say(const FString& Line)
{
	UE_LOG(LogFTO, Display, TEXT("BOT:%s [%.0fs] %s"), Tag.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" %s"), *Tag), GetWorld()->GetRealTimeSeconds() - StartedAt, *Line);
}

void AFTOBotPilot::Shot(const TCHAR* Why)
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Bot"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Name = FString::Printf(TEXT("%s%s%02d_%03d_%s.png"), *Tag, Tag.IsEmpty() ? TEXT("") : TEXT("_"), GShiftsPlayed + 1, ++Shots, Why);
	FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, Name), true, false);
	Say(FString::Printf(TEXT("screenshot %s"), *Name));
}

void AFTOBotPilot::Face(const FVector& Where)
{
	APlayerController* Controller = PC();
	AFTOCharacter* Officer = Me();
	const FVector To = (Where - Here()).GetSafeNormal2D();
	if (!Controller || To.IsNearlyZero())
	{
		return;
	}
	Controller->SetControlRotation(FRotator(-8.f, To.Rotation().Yaw, 0.f));
	if (Officer && !MyCar())
	{
		Officer->SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
	}
}

bool AFTOBotPilot::Aim(const FVector& Where)
{
	// The crosshair onto Where: from the camera, which is where a round's aimed from. The camera follows a frame or
	// so behind: on target once it's caught up.
	APlayerController* Controller = PC();
	if (!Controller || !Controller->PlayerCameraManager)
	{
		return false;
	}
	const FVector Eye = Controller->PlayerCameraManager->GetCameraLocation();
	const FRotator Look = (Where - Eye).Rotation();
	Controller->SetControlRotation(FRotator(Look.Pitch, Look.Yaw, 0.f));
	if (AFTOCharacter* Officer = Me(); Officer && !MyCar())
	{
		Officer->SetActorRotation(FRotator(0.f, Look.Yaw, 0.f));
	}
	const FVector Seeing = Controller->PlayerCameraManager->GetCameraRotation().Vector();
	return FVector::DotProduct(Seeing, (Where - Eye).GetSafeNormal()) > FMath::Cos(FMath::DegreesToRadians(1.5f));
}

bool AFTOBotPilot::InSight(const AActor* Who) const
{
	const APawn* Pawn = PC() ? PC()->GetPawn() : nullptr;
	if (!Pawn || !Who)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOBotSight), false, Pawn);
	Params.AddIgnoredActor(Who);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Pawn->GetPawnViewLocation(), Who->GetActorLocation() + FVector(0.f, 0.f, 30.f), ECC_Visibility, Params);
}

void AFTOBotPilot::Press()
{
	const float Now = GetWorld()->GetRealTimeSeconds();
	if (Now < NextPress)
	{
		return;
	}
	NextPress = Now + 0.5f;
	if (AFTOCharacter* Officer = Me())
	{
		Officer->PressInteract();
	}
}

void AFTOBotPilot::GoTo(const FVector& Where, bool bInRun, bool bAllowDrive)
{
	// (A new destination: the drive's progress counts from here.)
	if (!bGoal || FVector::Dist2D(Where, Goal) > 2000.f)
	{
		BestDriveDistance = TNumericLimits<float>::Max();
		DriveProgressAt = GetWorld()->GetRealTimeSeconds();
	}
	Goal = Where;
	bGoal = true;
	bRun = bInRun;
	bMayDrive = bAllowDrive;
}

void AFTOBotPilot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorld()->GetRealTimeSeconds();
	if (!GS() || !PC())
	{
		return;
	}
	EnsureNavMesh();
	TickPhase();
	if (ShotEvery > 0.f && Now >= NextShot)
	{
		NextShot = Now + ShotEvery;
		Shot(TEXT("play"));
	}
	if (GS()->GetShiftPhase() == EFTOShiftPhase::OnDuty)
	{
		if (Now >= NextThink)
		{
			NextThink = Now + 0.25f;
			Think();
		}
		Steer(DeltaSeconds);
	}
}

void AFTOBotPilot::TickPhase()
{
	AFTOGameState* State = GS();
	const float Now = GetWorld()->GetRealTimeSeconds();
	const EFTOShiftPhase Phase = State->GetShiftPhase();
	if (uint8(Phase) != LastPhase)
	{
		LastPhase = uint8(Phase);
		PhaseSince = Now;
		bVoted = false;
		Say(FString::Printf(TEXT("phase: %s."), *StaticEnum<EFTOShiftPhase>()->GetNameStringByValue(int64(Phase))));
		if (Phase == EFTOShiftPhase::Survived || Phase == EFTOShiftPhase::Overrun || Phase == EFTOShiftPhase::OvertimeVote)
		{
			Shot(Phase == EFTOShiftPhase::OvertimeVote ? TEXT("vote") : TEXT("end"));
		}
	}
	AFTOGameMode* Mode = GetWorld()->GetAuthGameMode<AFTOGameMode>();
	switch (Phase)
	{
	case EFTOShiftPhase::Lobby:
		// The host starts the shift once everyone's in (or a minute's gone by).
		if (Mode && Now - PhaseSince > 5.f && (State->PlayerArray.Num() >= WaitFor || Now - PhaseSince > 60.f))
		{
			Say(FString::Printf(TEXT("starting the shift with %d officer(s)."), State->PlayerArray.Num()));
			Mode->StartShift();
			PhaseSince = Now + 1000.f; // (once)
		}
		break;
	case EFTOShiftPhase::OvertimeVote:
		if (!bVoted && Now - PhaseSince > 2.f)
		{
			bVoted = true;
			const bool bMore = Style == TEXT("Reckless") || (Style == TEXT("Explorer") && State->GetChaos() < 60.f);
			Say(FString::Printf(TEXT("voting %s (chaos %.0f%%)."), bMore ? TEXT("for overtime") : TEXT("to clock off"), State->GetChaos()));
			if (AFTOPlayerController* Controller = Cast<AFTOPlayerController>(PC()))
			{
				Controller->FTOVote(bMore ? TEXT("Overtime") : TEXT("ClockOff"));
			}
		}
		break;
	case EFTOShiftPhase::Survived:
	case EFTOShiftPhase::Overrun:
		if (!bReported && Now - PhaseSince > 6.f)
		{
			bReported = true;
			int32 Team = 0;
			for (const APlayerState* PS : State->PlayerArray)
			{
				if (const AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS))
				{
					const FFTOOfficerStats& S = Officer->GetStats();
					Team += Officer->GetShiftScore();
					Say(FString::Printf(TEXT("result: %s scored %d (arrests %d, busts %d, calls %d, caught in the act %d, booked %d, tickets %d, revives %d, collateral %d, friendly fire %d, teamwork %d, best combo x%.1f)."),
						*Officer->GetPlayerName(), Officer->GetShiftScore(), S.Arrests, S.Busts, S.CallsHandled, S.CaughtInAct, S.Booked, S.Tickets, S.Revives, S.Collateral,
						S.FriendlyFire, S.Teamwork, FTOScoring::ComboMultiplier(S.BestCombo)));
				}
			}
			Say(FString::Printf(TEXT("shift over: %s, grade %s, squad %d; handled %d, went cold %d, witnessed %d, booked %d, peak chaos %.0f%%, overtimes %d, set piece %s, mutator %s, front pages %d, career +%d (rank %s, level %d)."),
				Phase == EFTOShiftPhase::Survived ? TEXT("SURVIVED") : TEXT("OVERRUN"),
				*FTOScoring::Grade(Team, State->PlayerArray.Num(), Phase == EFTOShiftPhase::Survived, State->PeakChaos, 10.f + 5.f * State->GetOvertimes()), Team,
				State->IncidentsResolved, State->IncidentsFailed, State->IncidentsWitnessed, State->SuspectsBooked, State->PeakChaos, State->GetOvertimes(),
				*State->GetSetPiece().ToString(), *State->GetMutator().ToString(), State->GetFrontPages().Num(), State->GetCareer().LastEarned,
				*FTOCareer::RankFor(State->GetCareer().Earned), State->GetCareerLevel()));
			Shot(TEXT("scoreboard"));
		}
		if (Now - PhaseSince > 14.f)
		{
			PhaseSince = Now + 1000.f; // (once)
			++GShiftsPlayed;
			if (GShiftsPlayed < ShiftsWanted)
			{
				if (Mode)
				{
					Say(TEXT("another shift."));
					Mode->NewShift();
				}
			}
			else
			{
				Say(TEXT("done playing."));
				FPlatformMisc::RequestExit(false, TEXT("FTOBotPlay"));
			}
		}
		break;
	default:
		break;
	}
}

AFTOIncident* AFTOBotPilot::PickTarget() const
{
	// Something already in hand comes first: a suspect on their knees, one we're chasing.
	AFTOIncident* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (AFTOIncident* Incident : GS()->GetIncidents())
	{
		if (!Incident || !Incident->IsActive() || !(Incident->IsKnownToDispatch() || Incident->WasWitnessed()))
		{
			continue;
		}
		if (const float* Until = GivenUp.Find(Incident); Until && GetWorld()->GetRealTimeSeconds() < *Until)
		{
			continue;
		}
		float Score = FVector::Dist2D(Incident->GetActorLocation(), Here());
		const AFTOPerp* Perp = Incident->GetPerp();
		if (Incident->IsSubdued() || Incident->IsFootChase())
		{
			Score -= 6000.f;
		}
		if (Incident == Target.Get())
		{
			Score -= 2500.f; // (stick with it)
		}
		if (Style == TEXT("Careful"))
		{
			Score -= 1500.f * int32(Incident->GetInfo().Tier); // the big ones first
		}
		// One about to escalate or go cold, before it does.
		Score -= 3000.f * Incident->GetUrgency();
		if (Incident->IsMobile() && !Perp && Style == TEXT("Reckless"))
		{
			Score -= 1500.f; // a car chase: Reckless loves one
		}
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Incident;
		}
	}
	return Best;
}

bool AFTOBotPilot::TakeThemIn()
{
	// Suspects trailing after us: walk (or drive) them to the cells when there are a few, or nothing's close.
	AFTOCharacter* Officer = Me();
	int32 Following = 0;
	for (TActorIterator<AFTOArrestee> It(GetWorld()); It && Officer; ++It)
	{
		Following += It->GetEscort() == Officer && It->GetArrestState() == EFTOArresteeState::Escorted ? 1 : 0;
	}
	// (Straight to the cells, as a player would: a suspect left trailing about the city is a suspect who slips off. Only
	// a suspect already down on the ground right here comes first.)
	const AFTOIncident* Next = PickTarget();
	const bool bTime = Following > 0 && !(Next && (Next->IsSubdued() || Next->IsFootChase()) && FVector::Dist2D(Next->GetActorLocation(), Here()) < 2500.f)
		&& !(Next && Next->GetInfo().Tier >= EFTOCrimeTier::Major && Following < 3); // (a big one first: they can tag along)
	const float Now = GetWorld()->GetRealTimeSeconds();
	if (bTime && bBooking && Now - LeftProgressAt > 60.f && Now >= NoBookingUntil)
	{
		Say(TEXT("can't find a way to the cells: on with the calls for a minute (they'll tag along)."));
		NoBookingUntil = Now + 60.f;
	}
	if (!bTime || Now < NoBookingUntil)
	{
		bBooking = false;
		return false;
	}
	for (TActorIterator<AFTOCityGenerator> It(GetWorld()); It; ++It)
	{
		if (!bBooking)
		{
			bBooking = true;
			Say(FString::Printf(TEXT("taking %d suspect(s) to the cells (%.0f m)."), Following, FVector::Dist2D(It->GetHoldingCellsLocation(), Here()) / 100.f));
		}
		GoTo(It->GetHoldingCellsLocation(), false, true);
		break;
	}
	TargetProgressAt = GetWorld()->GetRealTimeSeconds(); // (the call waits)
	return true;
}

void AFTOBotPilot::Think()
{
	AFTOCharacter* Officer = Me();
	const float Now = GetWorld()->GetRealTimeSeconds();
	if (Now >= NextStatus)
	{
		NextStatus = Now + 15.f;
		const AFTOPlayerState* PS = PC()->GetPlayerState<AFTOPlayerState>();
		int32 Open = 0;
		for (const AFTOIncident* Incident : GS()->GetIncidents())
		{
			Open += Incident && Incident->IsActive() && Incident->IsKnownToDispatch() ? 1 : 0;
		}
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		const FVector At = Here();
		Say(FString::Printf(TEXT("status: %.0f s left, chaos %.0f%%, %d calls open, score %d, squad combo %d, target %s%s; at %.0f,%.0f,%.0f%s, goal %.0f m; paths %d full, %d partial, %d none; navmesh %s."),
			GS()->GetShiftTimeRemaining(), GS()->GetChaos(), Open, PS ? PS->GetShiftScore() : -1, GS()->GetSquadCombo(), Target.IsValid() ? *Target->GetInfo().Title.ToString() : TEXT("none"),
			MyCar() ? TEXT(" (driving)") : bBooking ? TEXT(" (booking)") : !Officer ? TEXT(" (no officer)") : Officer->IsInSyncedAction() ? *FString::Printf(TEXT(" (in a synced %s with %s)"), *StaticEnum<EFTOAnimAction>()->GetNameStringByValue(int64(Officer->GetSyncedAction())), *GetNameSafe(Officer->GetSyncedPartner())) : Officer->GetTalkingTo() ? *FString::Printf(TEXT(" (talking to %s)"), *GetNameSafe(Officer->GetTalkingTo())) : Officer->IsDowned() ? TEXT(" (downed)") : TEXT(""), At.X / 100.f, At.Y / 100.f, At.Z / 100.f,
			MyCar() ? *FString::Printf(TEXT(" at %.0f km/h"), FMath::Abs(MyCar()->GetSpeed()) * 0.036f) : TEXT(""), bGoal ? FVector::Dist2D(Goal, At) / 100.f : 0.f,
			PathsFull, PathsPartial, PathsNone, !Nav ? TEXT("none") : Nav->IsNavigationBuildInProgress() ? *FString::Printf(TEXT("building (%d tiles to go)"), Nav->GetNumRemainingBuildTasks()) : TEXT("built")));
		PathsFull = PathsPartial = PathsNone = 0;
	}
	if (!Officer || Officer->IsDowned() || (Officer->GetKnockdown() && Officer->GetKnockdown()->IsDown()))
	{
		bGoal = false;
		return;
	}
	// Wrestling: heave away.
	if (Officer->IsInSyncedAction())
	{
		bGoal = false;
		if (Officer->GetSyncedAction() == EFTOAnimAction::Struggle)
		{
			Officer->PressInteract();
		}
		return;
	}
	// A conversation's open: answer it.
	if (Officer->GetTalkingTo())
	{
		bGoal = false;
		HandleTalk();
		return;
	}
	// Explorers get kitted out first.
	if (Style == TEXT("Explorer") && !bKitted && !MyCar())
	{
		AFTOArmoryRack* Rack = nullptr;
		for (TActorIterator<AFTOArmoryRack> It(GetWorld()); It; ++It)
		{
			Rack = *It;
			break;
		}
		if (!Rack || Officer->HasWeapon(EFTOWeapon::Shotgun) || Now - StartedAt > 240.f)
		{
			bKitted = true;
		}
		else
		{
			GoTo(Rack->GetActorLocation(), true);
			if (Close(Rack->GetActorLocation(), 180.f))
			{
				Face(Rack->GetActorLocation());
				Press();
			}
			return;
		}
	}
	if (TakeThemIn())
	{
		return;
	}
	// A call that's getting nowhere (no closer, no further on, the suspect doing nothing new) for a minute and a
	// quarter: let it go for a minute (as a player would), and take another.
	if (AFTOIncident* Current = Target.Get())
	{
		const float Distance = FVector::Dist2D(Current->GetActorLocation(), Here());
		const uint8 PerpState = Current->GetPerp() ? uint8(Current->GetPerp()->GetArrestState()) : 254;
		if (Distance < TargetBestDistance - 300.f || Current->GetProgress() > TargetBestProgress + 0.02f || PerpState != TargetPerpState)
		{
			TargetBestDistance = FMath::Min(Distance, TargetBestDistance);
			TargetBestProgress = FMath::Max(Current->GetProgress(), TargetBestProgress);
			TargetPerpState = PerpState;
			TargetProgressAt = Now;
		}
		if (Now - TargetProgressAt > 75.f)
		{
			Say(FString::Printf(TEXT("giving up on %s for now (%.0f s on it, nothing doing for %.0f s; %.0f m away, progress %.0f%%)."), *Current->GetInfo().Title.ToString(),
				Now - TargetSince, Now - TargetProgressAt, Distance / 100.f, 100.f * Current->GetProgress()));
			Shot(TEXT("giveup"));
			GivenUp.Add(Target, Now + 60.f);
			Target = nullptr;
		}
	}
	AFTOIncident* Incident = PickTarget();
	if (Incident != Target.Get())
	{
		if (AFTOIncident* Old = Target.Get(); Old && Old->IsActive())
		{
			Say(FString::Printf(TEXT("leaving %s for now."), *Old->GetInfo().Title.ToString()));
		}
		Target = Incident;
		TargetSince = Now;
		TargetProgressAt = Now;
		TargetBestDistance = TNumericLimits<float>::Max();
		TargetBestProgress = 0.f;
		TargetPerpState = 255;
		Questioned.Reset();
		if (Incident)
		{
			Say(FString::Printf(TEXT("on my way to: %s (%s, %.0f m)."), *Incident->GetInfo().Title.ToString(), *FTOCrime::TierName(Incident->GetInfo().Tier).ToString(),
				FVector::Dist2D(Incident->GetActorLocation(), Here()) / 100.f));
		}
	}
	if (!Incident)
	{
		// Nothing on: patrol somewhere new now and then.
		if (!bGoal || Close(Goal, 300.f))
		{
			GoTo(Here() + FVector(FMath::RandPointInCircle(4000.f), 0.f), false, false);
		}
		return;
	}
	Work(Incident);
}

void AFTOBotPilot::Work(AFTOIncident* Incident)
{
	AFTOCharacter* Officer = Me();
	AFTOPerp* Perp = Incident->GetPerp();
	const FFTOIncidentInfo Info = Incident->GetInfo();
	const float Now = GetWorld()->GetRealTimeSeconds();
	const bool bReckless = Style == TEXT("Reckless");

	// A car chase: after it in a cruiser.
	if (Incident->IsMobile() && !Perp)
	{
		GoTo(Incident->GetActorLocation(), true, true);
		if (!MyCar())
		{
			// (Into the nearest free cruiser, if there's one to be had: nobody catches a getaway car on foot.)
			const AFTOCruiser* Nearest = nullptr;
			for (TActorIterator<AFTOCruiser> It(GetWorld()); It; ++It)
			{
				if (!It->HasDriver() && FVector::Dist2D(It->GetActorLocation(), Here()) < 9000.f &&
					(!Nearest || FVector::Dist2D(It->GetActorLocation(), Here()) < FVector::Dist2D(Nearest->GetActorLocation(), Here())))
				{
					Nearest = *It;
				}
			}
			if (Nearest)
			{
				GoTo(Nearest->GetActorLocation(), true, false);
				if (Close(Nearest->GetActorLocation(), 500.f))
				{
					if (AFTOPlayerController* Controller = Cast<AFTOPlayerController>(PC()); Controller && Now >= NextPress)
					{
						NextPress = Now + 1.5f;
						Controller->FTODrive();
						Route.Reset();
						RouteGoal = FVector::ZeroVector;
						BestDriveDistance = TNumericLimits<float>::Max();
						DriveProgressAt = Now;
						Say(TEXT("into a cruiser, after them."));
					}
				}
			}
		}
		return;
	}
	if (!Perp)
	{
		GoTo(Incident->GetActorLocation(), false);
		return;
	}
	const EFTOPerpArrest State = Perp->GetArrestState();
	const FVector At = Perp->GetActorLocation();

	// On the run: sprint after them and dive.
	if (State == EFTOPerpArrest::Fleeing)
	{
		GoTo(At + Perp->GetVelocity() * 0.3f, true, false);
		if (Close(At, 320.f))
		{
			Face(At);
			Officer->DiveTackle();
		}
		return;
	}
	// On their knees: cuff them.
	if (State == EFTOPerpArrest::Surrendered)
	{
		GoTo(At, false, false);
		if (Close(At, 200.f))
		{
			Face(At);
			Press();
		}
		return;
	}
	// Squared up: fists.
	if (State == EFTOPerpArrest::Fighting)
	{
		GoTo(At, false, false);
		if (Close(At, 150.f) && Now >= NextSwing)
		{
			Face(At);
			NextSwing = Now + 0.45f;
			if (FMath::FRand() < 0.25f)
			{
				Officer->KickPressed();
			}
			else
			{
				Officer->PunchPressed();
			}
		}
		return;
	}
	// A crowd or a search: find whoever matches the description and have a word.
	if (Incident->IsCrowd() || Incident->IsSearching())
	{
		const FString Wanted = Info.SuspectDescription.ToString();
		AFTOPedestrian* Pick = nullptr;
		float PickDist = TNumericLimits<float>::Max();
		const float Radius = Incident->IsSearching() ? Incident->GetSearchRadius() + 800.f : 900.f;
		for (TActorIterator<AFTOPedestrian> It(GetWorld()); It; ++It)
		{
			if (Questioned.Contains(*It) || FVector::Dist2D(It->GetActorLocation(), Incident->GetActorLocation()) > Radius)
			{
				continue;
			}
			const float Dist = FVector::Dist2D(It->GetActorLocation(), Here());
			// The one who fits the description; else (a careless officer) whoever's nearest.
			const bool bFits = Matches(It->DescribeLook(), Wanted);
			const float Score = Dist - (bFits ? 100000.f : 0.f);
			if ((bFits || bReckless) && Score < PickDist)
			{
				PickDist = Score;
				Pick = *It;
			}
		}
		if (Pick)
		{
			GoTo(Pick->GetActorLocation(), false, false);
			if (Close(Pick->GetActorLocation(), 220.f))
			{
				Face(Pick->GetActorLocation());
				Press();
				Questioned.Add(Pick);
			}
		}
		else
		{
			GoTo(Incident->GetActorLocation(), Incident->IsSearching(), true);
		}
		return;
	}
	// Hiding in the building: in we go, up the lift if they're upstairs.
	if (Incident->IsHiddenInside())
	{
		const bool bUpstairs = At.Z > Here().Z + 200.f;
		const bool bDownstairs = At.Z < Here().Z - 200.f;
		if (bUpstairs || bDownstairs)
		{
			const AFTOLift* Lift = nullptr;
			for (TActorIterator<AFTOLift> It(GetWorld()); It; ++It)
			{
				if (FMath::Abs(It->GetActorLocation().Z - Here().Z) < 200.f && FVector::Dist2D(It->GetActorLocation(), At) < 3000.f &&
					(!Lift || FVector::Dist2D(It->GetActorLocation(), Here()) < FVector::Dist2D(Lift->GetActorLocation(), Here())))
				{
					Lift = *It;
				}
			}
			if (Lift)
			{
				GoTo(Lift->GetInteractLocation(), false, true);
				if (Close(Lift->GetInteractLocation(), 180.f))
				{
					Face(Lift->GetInteractLocation());
					Press();
				}
				return;
			}
		}
		// Look round where they might be (wandering the floor near them).
		if (!bGoal || Close(Goal, 150.f) || Now - TargetSince > 20.f)
		{
			GoTo(At + FVector(FMath::RandPointInCircle(350.f), 0.f), false, true);
		}
		return;
	}
	// The bomb: to the wires.
	if (Info.TemplateId == TEXT("Bomb"))
	{
		for (TActorIterator<AFTOBomb> It(GetWorld()); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), Incident->GetActorLocation()) < 600.f && !It->IsDefused() && !It->HasExploded())
			{
				GoTo(It->GetActorLocation(), true, true);
				if (Close(It->GetActorLocation(), 220.f))
				{
					Face(It->GetActorLocation());
					Press();
				}
				return;
			}
		}
	}
	// A drunk: talk them round.
	if (Perp->IsDrunkCall() && State == EFTOPerpArrest::None)
	{
		GoTo(At, false, true);
		if (Close(At, 230.f))
		{
			Face(At);
			Press();
		}
		return;
	}
	// Armed (or a gun out): in quickly with the taser up, and zap them from inside its nine metres before they get a
	// bead on us. Tasered, they're on the floor and as good as caught.
	if ((Info.bArmed || Perp->GetAimPose() != EFTOAimPose::None) && Perp->IsCriminal() && State == EFTOPerpArrest::None && !Incident->IsSubdued())
	{
		const float Distance = FVector::Dist2D(At, Here());
		GoTo(At, true, true);
		if (Distance < 2500.f && FMath::Abs(At.Z - Here().Z) < 250.f)
		{
			if (Officer->GetDrawnWeapon() != EFTOWeapon::Taser && Now >= NextSwing)
			{
				NextSwing = Now + 0.5f;
				Officer->SelectSlot(0);
			}
			const bool bSeen = InSight(Perp);
			if (Distance < 550.f && bSeen)
			{
				bGoal = false; // (close enough: stand and shoot)
			}
			if (Distance < 800.f && bSeen && Officer->GetDrawnWeapon() == EFTOWeapon::Taser && Now >= NextSwing && Aim(At + FVector(0.f, 0.f, 20.f)))
			{
				NextSwing = Now + 0.8f;
				Officer->FirePressed();
				if (Now >= NextZapLog)
				{
					NextZapLog = Now + 5.f;
					Say(FString::Printf(TEXT("tasering the armed suspect (%.0f m)."), Distance / 100.f));
				}
			}
		}
		return;
	}
	// A brawl with nobody to help: wade in.
	if (Incident->IsBrawl() && Incident->GetOfficersOnScene() < Incident->GetMinCrew() && State == EFTOPerpArrest::None && Close(At, 400.f))
	{
		GoTo(At, false, false);
		if (Close(At, 150.f) && Now >= NextSwing)
		{
			Face(At);
			NextSwing = Now + 0.5f;
			Officer->PunchPressed();
		}
		return;
	}
	// Anything else: onto the scene, and talk them down (Careful) or arrest them on the spot (Reckless).
	if (Officer->GetDrawnWeapon() != EFTOWeapon::None)
	{
		Officer->SelectSlot(Officer->GetDrawnSlot()); // (put it away)
	}
	GoTo(Incident->GetActorLocation(), Info.Tier >= EFTOCrimeTier::Major, true);
	if (Info.bArrest && Perp->IsCriminal() && Close(At, 250.f) && (bReckless || Now - TargetSince > 25.f))
	{
		Face(At);
		Press();
	}
}

void AFTOBotPilot::HandleTalk()
{
	AFTOCharacter* Officer = Me();
	AActor* Who = Officer ? Officer->GetTalkingTo() : nullptr;
	IFTOTalkable* Talkable = Cast<IFTOTalkable>(Who);
	const float Now = GetWorld()->GetRealTimeSeconds();
	if (!Talkable || Now < NextPress)
	{
		return;
	}
	NextPress = Now + 0.9f; // (reading the panel)
	TArray<FText> Options;
	Talkable->GetTalkOptions(Officer, Options);
	const FString Title = Talkable->GetTalkTitle().ToString();
	auto Choose = [&](int32 Index, const TCHAR* Why)
	{
		Say(FString::Printf(TEXT("talking to \"%s\": %s (\"%s\")."), *Title, Why, Options.IsValidIndex(Index) ? *Options[Index].ToString() : TEXT("?")));
		Officer->TalkPressed(Index);
	};
	if (const AFTOBomb* Bomb = Cast<AFTOBomb>(Who))
	{
		const int32 Wire = WireForClue(Title);
		Choose(Wire, TEXT("cutting the wire the label means"));
		return;
	}
	if (const AFTOLift* Lift = Cast<AFTOLift>(Who))
	{
		const AFTOIncident* Incident = Target.Get();
		const AFTOPerp* Perp = Incident ? Incident->GetPerp() : nullptr;
		const bool bUp = !Perp || Perp->GetActorLocation().Z > Here().Z;
		for (int32 i = 0; i < Options.Num(); ++i)
		{
			const FString Line = Options[i].ToString().ToLower();
			if ((bUp && Line.Contains(TEXT("up"))) || (!bUp && (Line.Contains(TEXT("down")) || Line.Contains(TEXT("street")))))
			{
				Choose(i, bUp ? TEXT("going up") : TEXT("going down"));
				return;
			}
		}
		Choose(Options.Num() - 1, TEXT("never mind"));
		return;
	}
	if (const AFTOPerp* Perp = Cast<AFTOPerp>(Who); Perp && Perp->IsDrunkCall() && Perp->GetArrestState() == EFTOPerpArrest::None)
	{
		Choose(KindAnswer(Options), TEXT("the kind answer"));
		return;
	}
	if (const AFTOPedestrian* Person = Cast<AFTOPedestrian>(Who))
	{
		// Someone who fits the description we're after: search them, and arrest them if the search turns something up.
		const AFTOIncident* Incident = Target.Get();
		const FString Wanted = Incident ? Incident->GetInfo().SuspectDescription.ToString() : FString();
		const bool bSuspect = Matches(Person->DescribeLook(), Wanted) || (Style == TEXT("Reckless") && Incident && (Incident->IsCrowd() || Incident->IsSearching()));
		const AFTOCrimeExtra* Extra = Cast<AFTOCrimeExtra>(Person);
		if (Extra && Extra->GetRole() == EFTOExtraRole::Victim && !Questioned.Contains(Who))
		{
			Questioned.Add(Who);
			Choose(0, TEXT("taking a statement"));
			return;
		}
		if (bSuspect && !Person->WasSearched())
		{
			Choose(2, TEXT("they fit the description: a search"));
			return;
		}
		if (bSuspect && !Person->GetFound().IsEmpty())
		{
			Choose(2, TEXT("found something: an arrest"));
			return;
		}
		Choose(Options.Num() - 1, TEXT("not them: goodbye"));
		return;
	}
	Choose(Options.Num() - 1, TEXT("done here"));
}

void AFTOBotPilot::PlanRoute(const FVector& From)
{
	Route.Reset();
	RouteGoal = Goal;
	const AFTOCityGenerator* City = TActorIterator<AFTOCityGenerator>(GetWorld()) ? *TActorIterator<AFTOCityGenerator>(GetWorld()) : nullptr;
	if (!City)
	{
		return;
	}
	auto Nearest = [City](const FVector& Where, int32& OutI, int32& OutJ)
	{
		float Best = TNumericLimits<float>::Max();
		for (int32 I = 0; I < City->NumIntersectionsX(); ++I)
		{
			for (int32 J = 0; J < City->NumIntersectionsY(); ++J)
			{
				const float D = FVector::DistSquared2D(City->GetIntersection(I, J), Where);
				if (D < Best)
				{
					Best = D;
					OutI = I;
					OutJ = J;
				}
			}
		}
	};
	int32 I0 = 0, J0 = 0, I1 = 0, J1 = 0;
	Nearest(From, I0, J0);
	Nearest(Goal, I1, J1);
	// Out to the nearest junction, along one street and then the other, to the junction nearest the goal.
	const int32 StepI = I1 > I0 ? 1 : -1;
	const int32 StepJ = J1 > J0 ? 1 : -1;
	Route.Add(City->GetIntersection(I0, J0));
	for (int32 I = I0; I != I1; I += StepI)
	{
		Route.Add(City->GetIntersection(I + StepI, J0));
	}
	for (int32 J = J0; J != J1; J += StepJ)
	{
		Route.Add(City->GetIntersection(I1, J + StepJ));
	}
	// And along the street to the kerb nearest the goal (pulling up there: never across the pavement).
	const FVector Last = City->GetIntersection(I1, J1);
	const FVector AlongX(Goal.X, Last.Y, Last.Z);
	const FVector AlongY(Last.X, Goal.Y, Last.Z);
	const FVector Kerb = FVector::DistSquared2D(AlongX, Goal) < FVector::DistSquared2D(AlongY, Goal) ? AlongX : AlongY;
	if (FVector::Dist2D(Kerb, Last) > 900.f)
	{
		Route.Add(Kerb);
	}
	LaneOffset = City->GetRoadWidth() * 0.25f;
}

void AFTOBotPilot::EnsureNavMesh()
{
	// No navmesh in a city that's generated at play time: once it's built here, a bounds volume over the whole of it,
	// and the navigation system builds one (at runtime, in the background; RecastNavMesh RuntimeGeneration=Dynamic).
	if (bNavRequested)
	{
		return;
	}
	AFTOCityGenerator* City = TActorIterator<AFTOCityGenerator>(GetWorld()) ? *TActorIterator<AFTOCityGenerator>(GetWorld()) : nullptr;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!City || !City->IsGeometryBuilt() || !Nav)
	{
		return;
	}
	bNavRequested = true;
	// The city's walls, floors, stairs and furniture are what the navmesh is built from (they're kept out of navigation
	// in normal play, there being no navmesh); everything that moves stays out of it.
	int32 Solid = 0;
	TInlineComponentArray<UInstancedStaticMeshComponent*> Pieces(City);
	for (UInstancedStaticMeshComponent* Piece : Pieces)
	{
		if (Piece->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			Piece->SetCanEverAffectNavigation(true);
			++Solid;
		}
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		QuietNav(*It);
	}
	SpawnedHandle = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &AFTOBotPilot::OnSpawned));
	Say(FString::Printf(TEXT("the navmesh is built from %d pieces of the city."), Solid));
	const FVector Extent = City->GetCityExtent();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ANavMeshBoundsVolume* Volume = GetWorld()->SpawnActor<ANavMeshBoundsVolume>(City->GetActorLocation(), FRotator::ZeroRotator, Params);
	if (!Volume)
	{
		return;
	}
	UBoxComponent* Box = NewObject<UBoxComponent>(Volume, TEXT("NavBounds"));
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetCanEverAffectNavigation(false);
	Box->SetBoxExtent(FVector(Extent.X + 3000.f, Extent.Y + 3000.f, 6000.f));
	Box->SetupAttachment(Volume->GetRootComponent());
	Box->RegisterComponent();
	Nav->OnNavigationBoundsUpdated(Volume);
	Nav->GetDefaultNavDataInstance(FNavigationSystem::Create);
	Say(FString::Printf(TEXT("building a navmesh over the city (%.0f x %.0f m)."), Box->GetScaledBoxExtent().X / 50.f, Box->GetScaledBoxExtent().Y / 50.f));
}

void AFTOBotPilot::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SpawnedHandle.IsValid())
	{
		GetWorld()->RemoveOnActorSpawnedHandler(SpawnedHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AFTOBotPilot::QuietNav(AActor* Actor)
{
	if (!Actor || Actor->IsA<AFTOCityGenerator>())
	{
		return;
	}
	TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
	for (UPrimitiveComponent* Part : Parts)
	{
		if (Part->Mobility != EComponentMobility::Static && Part->CanEverAffectNavigation())
		{
			Part->SetCanEverAffectNavigation(false);
		}
	}
}

void AFTOBotPilot::OnSpawned(AActor* Actor)
{
	QuietNav(Actor);
}

FVector AFTOBotPilot::NextStep(const FVector& Where)
{
	// The navmesh's path to Where (asked again every couple of seconds, or when Where moves), a corner at a time. No
	// path all the way (it's upstairs, or the navmesh isn't built there yet): by the building's door or the outside
	// stairs, as far as that gets us.
	const float Now = GetWorld()->GetRealTimeSeconds();
	const FVector From = Here();
	if (Now >= NextPathTime || FVector::Dist(PathGoal, Where) > 300.f)
	{
		NextPathTime = Now + 1.5f;
		PathGoal = Where;
		Path.Reset();
		PathIndex = 1;
		bPathFull = false;
		APawn* Pawn = PC() ? PC()->GetPawn() : nullptr;
		auto Find = [&](const FVector& To, bool& bOutFull) -> bool
		{
			UNavigationPath* Found = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), From, To, Pawn);
			if (!Found || !Found->IsValid() || Found->PathPoints.Num() < 2)
			{
				return false;
			}
			Path = Found->PathPoints;
			bOutFull = !Found->IsPartial();
			return true;
		};
		bool bFull = false;
		const bool bAny = Find(Where, bFull);
		if (!bFull)
		{
			const FVector Via = Waypoint(Where);
			bool bViaFull = false;
			const TArray<FVector> Partial = Path;
			if (FVector::Dist(Via, Where) > 10.f && Find(Via, bViaFull))
			{
				bFull = false; // (the way in, not all the way)
			}
			else
			{
				Path = Partial;
			}
		}
		bPathFull = bFull;
		(bFull ? PathsFull : (Path.Num() > 1 ? PathsPartial : PathsNone))++;
		if (bAny && bFull && !bPathsWork)
		{
			bPathsWork = true;
			Say(FString::Printf(TEXT("the navmesh is up: finding my way by it (a path of %d corners, %.0f m)."), Path.Num(), PathLeft() / 100.f));
		}
	}
	while (Path.IsValidIndex(PathIndex) && FVector::Dist2D(Path[PathIndex], From) < 80.f && FMath::Abs(Path[PathIndex].Z - (From.Z - 96.f)) < 150.f)
	{
		++PathIndex;
	}
	return Path.IsValidIndex(PathIndex) ? Path[PathIndex] : Where;
}

void AFTOBotPilot::Watch(float Left)
{
	const float Now = GetWorld()->GetRealTimeSeconds();
	if (FVector::Dist2D(Goal, WatchGoal) > 400.f)
	{
		WatchGoal = Goal;
		BestLeft = Left;
		LeftProgressAt = Now;
		return;
	}
	if (Left < BestLeft - 150.f)
	{
		BestLeft = Left;
		LeftProgressAt = Now;
	}
	if (Now - LeftProgressAt > 15.f && Now >= NextStuckReport)
	{
		NextStuckReport = Now + 30.f;
		const FVector At = Here();
		Say(FString::Printf(TEXT("stuck: %.0f s without getting closer (%.0f m to go, %s; at %.0f,%.0f,%.0f, going to %.0f,%.0f,%.0f)."), Now - LeftProgressAt, Left / 100.f,
			MyCar() ? TEXT("driving") : bPathFull ? TEXT("on a path") : Path.Num() > 1 ? TEXT("a partial path") : TEXT("no path"),
			At.X / 100.f, At.Y / 100.f, At.Z / 100.f, Goal.X / 100.f, Goal.Y / 100.f, Goal.Z / 100.f));
		if (!MyCar() && !bPathFull)
		{
			// (Why there's no way there: the path's end, and whether the goal's building can be got into from its door.)
			const AFTOCityGenerator* City = TActorIterator<AFTOCityGenerator>(GetWorld()) ? *TActorIterator<AFTOCityGenerator>(GetWorld()) : nullptr;
			FString Why = Path.Num() > 1 ? FString::Printf(TEXT("the path ends at %.0f,%.0f,%.0f"), Path.Last().X / 100.f, Path.Last().Y / 100.f, Path.Last().Z / 100.f) : TEXT("no path");
			for (int32 i = 0; City && i < City->GetBuildings().Num(); ++i)
			{
				const FFTOBuilding& B = City->GetBuildings()[i];
				if (B.Contains(Goal, 30.f))
				{
					const UNavigationPath* In = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), B.DoorOutside + FVector(0.f, 0.f, 96.f), Goal, PC() ? PC()->GetPawn() : nullptr);
					const UNavigationPath* Centre = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), B.DoorOutside + FVector(0.f, 0.f, 96.f), B.GetCenter() + FVector(0.f, 0.f, 50.f), PC() ? PC()->GetPawn() : nullptr);
					Why += FString::Printf(TEXT("; the goal's in building %d (%s, type %d): door to goal %s, door to the middle %s"), i, *B.Name, int32(B.Type),
						!In || !In->IsValid() ? TEXT("none") : In->IsPartial() ? *FString::Printf(TEXT("partial to %.0f,%.0f,%.0f"), In->PathPoints.Last().X / 100.f, In->PathPoints.Last().Y / 100.f, In->PathPoints.Last().Z / 100.f) : TEXT("ok"),
						!Centre || !Centre->IsValid() ? TEXT("none") : Centre->IsPartial() ? TEXT("partial") : TEXT("ok"));
					break;
				}
			}
			Say(Why + TEXT("."));
		}
		Shot(TEXT("stuck"));
		NextPathTime = 0.f; // (a fresh path)
	}
}

float AFTOBotPilot::PathLeft() const
{
	if (!Path.IsValidIndex(PathIndex))
	{
		return FVector::Dist2D(Here(), Goal);
	}
	float Left = FVector::Dist2D(Here(), Path[PathIndex]);
	for (int32 i = PathIndex; i + 1 < Path.Num(); ++i)
	{
		Left += FVector::Dist2D(Path[i], Path[i + 1]);
	}
	// (A partial path: the rest of the way, as the crow flies.)
	return Left + (bPathFull ? 0.f : FVector::Dist2D(Path.Last(), Goal));
}

bool AFTOBotPilot::Blocked(const FVector& Dir) const
{
	// A wall or a fence at chest height, a stride ahead (kerbs and steps are below it: those we walk up).
	const FVector From = Here() + FVector(0.f, 0.f, 20.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOBotAhead), false, PC() ? PC()->GetPawn() : nullptr);
	FHitResult Hit;
	return GetWorld()->SweepSingleByObjectType(Hit, From, From + Dir * 140.f, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
		FCollisionShape::MakeSphere(30.f), Params);
}

FVector AFTOBotPilot::Waypoint(const FVector& Where) const
{
	// No navmesh in a generated city: buildings are walked in and out of by their front doors, as a player would.
	const AFTOCityGenerator* City = TActorIterator<AFTOCityGenerator>(GetWorld()) ? *TActorIterator<AFTOCityGenerator>(GetWorld()) : nullptr;
	if (!City)
	{
		return Where;
	}
	const FVector From = Here();
	// Upstairs (a flat over a house): up its outside stairs, from the foot to the doorway at the top.
	if (Where.Z - From.Z > 200.f)
	{
		const TArray<FTransform>& Feet = City->GetOutsideStairs();
		const TArray<FTransform>& Tops = City->GetOutsideStairTops();
		int32 Best = INDEX_NONE;
		float BestDistance = 2500.f;
		for (int32 i = 0; i < FMath::Min(Feet.Num(), Tops.Num()); ++i)
		{
			const float D = FVector::Dist2D(Tops[i].GetLocation(), Where);
			if (D < BestDistance)
			{
				BestDistance = D;
				Best = i;
			}
		}
		if (Best != INDEX_NONE)
		{
			const FVector Foot = Feet[Best].GetLocation();
			const FVector Top = Tops[Best].GetLocation();
			// On the stairs (or at their foot): climb to the top; anywhere else below: to the foot first.
			const bool bClimbing = FVector::Dist2D(From, Foot) < 200.f || From.Z > Foot.Z + 60.f;
			return bClimbing ? Top + Tops[Best].GetRotation().GetForwardVector() * 150.f : Foot;
		}
	}
	for (const FFTOBuilding& Building : City->GetBuildings())
	{
		const bool bGoalIn = Building.Contains(Where, 30.f);
		const bool bMeIn = Building.Contains(From, 30.f);
		if (bGoalIn == bMeIn || Building.DoorOutside.IsZero())
		{
			continue;
		}
		// Through the door: once we're at it, straight on to the goal.
		if (FVector::Dist2D(From, Building.DoorOutside) > 220.f)
		{
			return Building.DoorOutside;
		}
	}
	return Where;
}

void AFTOBotPilot::Steer(float DeltaSeconds)
{
	AFTOCharacter* Officer = Me();
	APlayerController* Controller = PC();
	const float Now = GetWorld()->GetRealTimeSeconds();
	if (AFTOCruiser* Car = MyCar())
	{
		// Driving: steer for the goal; out once we're there (unless it's a chase).
		const AFTOIncident* Incident = Target.Get();
		const bool bChase = Incident && Incident->IsMobile() && !Incident->GetPerp();
		// (The car moves itself, kinematically: its speed is its own, not the root's velocity.)
		const float Speed = FMath::Abs(Car->GetSpeed());
		const float ToGoal = FVector::Dist2D(Goal, Car->GetActorLocation());
		// By the streets, a junction at a time (never across the pavement and through a shop).
		if (RouteGoal.IsZero() || FVector::Dist2D(RouteGoal, Goal) > 2000.f)
		{
			PlanRoute(Car->GetActorLocation());
		}
		while (Route.Num() > 0 && FVector::Dist2D(Route[0], Car->GetActorLocation()) < 700.f)
		{
			Route.RemoveAt(0);
		}
		// A chase, close: straight at them (and into them: a getaway car that's been rammed enough is going nowhere).
		const bool bRam = bChase && ToGoal < 3500.f;
		if (bRam)
		{
			Route.Reset();
			RouteGoal = FVector::ZeroVector;
		}
		// The way still to go: along the route, then on to the goal.
		float Left = 0.f;
		FVector From = Car->GetActorLocation();
		for (const FVector& Point : Route)
		{
			Left += FVector::Dist2D(From, Point);
			From = Point;
		}
		Left += FVector::Dist2D(From, Goal);
		// Not getting any closer (wedged, wrecked, going round in circles): out, and on foot for a while.
		if (bGoal && (Left < BestDriveDistance - 500.f || (bChase && Speed > 400.f)))
		{
			BestDriveDistance = FMath::Min(Left, BestDriveDistance);
			DriveProgressAt = Now;
		}
		const bool bWrecked = Car->GetDamage() && Car->GetDamage()->IsWrecked();
		const bool bNoProgress = bGoal && Now - DriveProgressAt > (bChase ? 20.f : 12.f);
		if (bNoProgress || bWrecked)
		{
			NoDriveUntil = Now + 60.f;
		}
		if (!bGoal || bNoProgress || bWrecked || (!bChase && (ToGoal < 1500.f || Route.IsEmpty())))
		{
			Car->SetAutopilot(true, Speed > 100.f ? -1.f : 0.f, 0.f);
			if (Speed < 150.f && Now >= NextPress)
			{
				NextPress = Now + 1.5f;
				Car->RequestExit();
				Say(bWrecked ? TEXT("the car's a write-off: out, on foot.") : bNoProgress ? TEXT("getting nowhere in the car: out, on foot.") : TEXT("out of the car."));
			}
			return;
		}
		// Keep to the right of the road (the oncoming lane's for overtaking), and slow down for the corners.
		const FVector CarAt = Car->GetActorLocation();
		FVector Next = Route.Num() > 0 ? Route[0] : Goal;
		const FVector Leg = (Next - CarAt).GetSafeNormal2D();
		if (Route.Num() > 0 && !bRam)
		{
			Next += FVector(-Leg.Y, Leg.X, 0.f) * LaneOffset;
		}
		if (bRam)
		{
			// (Where they'll be in a moment.)
			if (const AFTOTrafficCar* Quarry = Incident ? Cast<AFTOTrafficCar>(Incident->GetAttachParentActor()) : nullptr)
			{
				Next += Quarry->GetMoveDirection() * Quarry->GetCurrentSpeed() * 0.4f;
			}
		}
		const FVector To = Next - CarAt;
		const float Angle = FMath::FindDeltaAngleDegrees(Car->GetActorRotation().Yaw, To.Rotation().Yaw);
		float Want = bRam ? 2600.f : 2000.f;
		if (Route.Num() >= 2)
		{
			const FVector After = (Route[1] - Route[0]).GetSafeNormal2D();
			if (FVector::DotProduct(After, Leg) < 0.7f)
			{
				Want = FMath::Min(Want, FMath::GetMappedRangeValueClamped(FVector2D(800.f, 3500.f), FVector2D(800.f, 2000.f), To.Size2D()));
			}
		}
		else if (!bRam)
		{
			Want = FMath::Min(Want, FMath::Max(500.f, ToGoal - 1200.f)); // (pulling up)
		}
		if (FMath::Abs(Angle) > 25.f)
		{
			Want = FMath::Min(Want, FMath::GetMappedRangeValueClamped(FVector2D(25.f, 90.f), FVector2D(1200.f, 500.f), FMath::Abs(Angle)));
		}
		float Throttle = FMath::Clamp((Want - Car->GetSpeed()) / 400.f, -1.f, 1.f);
		float Wheel = FMath::Clamp(Angle / 30.f, -1.f, 1.f);
		// Stuck against something: back off with the wheel the other way.
		StuckFor = Speed < 80.f && Now >= EscapeUntil ? StuckFor + DeltaSeconds : 0.f;
		if (StuckFor > 1.2f)
		{
			EscapeUntil = Now + 1.4f;
			StuckFor = 0.f;
		}
		if (Now < EscapeUntil)
		{
			Throttle = -0.8f;
			Wheel = -Wheel;
		}
		Car->SetAutopilot(true, Throttle, Wheel);
		return;
	}
	if (!Officer || !bGoal || Officer->IsInSyncedAction() || Officer->GetTalkingTo())
	{
		if (Officer && Officer->IsSprinting())
		{
			Officer->SetSprinting(false);
		}
		return;
	}
	const float Distance = FVector::Dist2D(Goal, Here());
	// Far: a cruiser's quicker, if there's a free one near.
	if (bMayDrive && Distance > 8000.f && Style != TEXT("Explorer") && Now >= NoDriveUntil)
	{
		for (TActorIterator<AFTOCruiser> It(GetWorld()); It; ++It)
		{
			if (!It->HasDriver() && Close(It->GetActorLocation(), 2500.f))
			{
				if (Close(It->GetActorLocation(), 500.f))
				{
					if (AFTOPlayerController* FTOPC = Cast<AFTOPlayerController>(Controller); FTOPC && Now >= NextPress)
					{
						NextPress = Now + 1.5f;
						FTOPC->FTODrive();
						Route.Reset();
						RouteGoal = FVector::ZeroVector; // (planned afresh from wherever this car is)
						BestDriveDistance = TNumericLimits<float>::Max();
						DriveProgressAt = Now;
						Say(TEXT("into a cruiser."));
					}
					return;
				}
				const FVector ToCar = (NextStep(It->GetActorLocation()) - Here()).GetSafeNormal2D();
				Officer->AddMovementInput(ToCar, 1.f);
				Controller->SetControlRotation(FRotator(-10.f, ToCar.Rotation().Yaw, 0.f));
				return;
			}
		}
	}
	if (Distance < 90.f)
	{
		return;
	}
	FVector Dir = (NextStep(Goal) - Here()).GetSafeNormal2D();
	Watch(Distance > 250.f ? PathLeft() : 0.f);
	// Getting nowhere with no path all the way: a few seconds feeling our way along the walls toward the door (or the
	// goal) instead, as someone lost would.
	if (!bPathFull && Now - LeftProgressAt > 10.f && Now > FeelUntil + 8.f)
	{
		FeelUntil = Now + 5.f;
	}
	if (Now < FeelUntil)
	{
		Dir = (Waypoint(Goal) - Here()).GetSafeNormal2D();
	}
	// No path all the way, and a wall ahead (a house between us and a back garden): follow it round, always keeping it
	// on the same side, till the way to the goal is clear again.
	if (!bPathFull && Blocked(Dir))
	{
		if (FollowSide == 0.f || Now > FollowUntil)
		{
			FollowSide = FMath::RandBool() ? 1.f : -1.f;
		}
		FollowUntil = Now + 5.f;
		for (float Angle = 20.f; Angle <= 180.f; Angle += 20.f)
		{
			const FVector Turned = Dir.RotateAngleAxis(FollowSide * Angle, FVector::UpVector);
			if (!Blocked(Turned))
			{
				Dir = Turned;
				break;
			}
		}
	}
	// Stuck (a wall, a bench, a car): jump and sidestep for a moment.
	const float Moved = FVector::Dist2D(Here(), LastSpot);
	LastSpot = Here();
	StuckFor = Moved < 60.f * DeltaSeconds ? StuckFor + DeltaSeconds : FMath::Max(0.f, StuckFor - DeltaSeconds);
	if (StuckFor > 1.f)
	{
		StuckFor = 0.f;
		EscapeUntil = Now + 0.9f;
		EscapeDir = FVector::CrossProduct(FVector::UpVector, Dir) * (FMath::RandBool() ? 1.f : -1.f);
		Officer->Jump();
	}
	if (Now < EscapeUntil)
	{
		Dir = (EscapeDir + Dir * 0.3f).GetSafeNormal2D();
	}
	const bool bSprint = bRun || Distance > 1500.f;
	if (Officer->IsSprinting() != bSprint)
	{
		Officer->SetSprinting(bSprint);
	}
	Officer->AddMovementInput(Dir, 1.f);
	Controller->SetControlRotation(FRotator(-10.f, FMath::FixedTurn(Controller->GetControlRotation().Yaw, Dir.Rotation().Yaw, 360.f * DeltaSeconds), 0.f));
}
