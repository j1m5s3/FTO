#include "Dev/FTOBotPilot.h"
#include "City/FTOCityGenerator.h"
#include "City/FTOLift.h"
#include "City/FTOPedestrian.h"
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
#include "Weapons/FTOArmoryRack.h"
#include "FTO.h"

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
		if (Incident->IsMobile() && !Perp)
		{
			Score += Style == TEXT("Reckless") ? -1500.f : 2000.f; // a car chase: Reckless loves one
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
	const AFTOIncident* Next = PickTarget();
	const bool bTime = Following >= 3 || (Following > 0 && (!Next || FVector::Dist2D(Next->GetActorLocation(), Here()) > 5000.f));
	if (!bTime)
	{
		return false;
	}
	for (TActorIterator<AFTOCityGenerator> It(GetWorld()); It; ++It)
	{
		GoTo(It->GetHoldingCellsLocation(), false, true);
		break;
	}
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
		Say(FString::Printf(TEXT("status: %.0f s left, chaos %.0f%%, %d calls open, score %d, squad combo %d, target %s%s."), GS()->GetShiftTimeRemaining(),
			GS()->GetChaos(), Open, PS ? PS->GetShiftScore() : -1, GS()->GetSquadCombo(), Target.IsValid() ? *Target->GetInfo().Title.ToString() : TEXT("none"),
			MyCar() ? TEXT(" (driving)") : TEXT("")));
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
	AFTOIncident* Incident = PickTarget();
	if (Incident != Target.Get())
	{
		Target = Incident;
		TargetSince = Now;
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
			// (Into the nearest cruiser, if one's close; else on foot.)
			for (TActorIterator<AFTOCruiser> It(GetWorld()); It; ++It)
			{
				if (!It->HasDriver() && Close(It->GetActorLocation(), 700.f))
				{
					if (AFTOPlayerController* Controller = Cast<AFTOPlayerController>(PC()); Controller && Now >= NextPress)
					{
						NextPress = Now + 1.f;
						Controller->FTODrive();
					}
					break;
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
	// A gun out: the taser (from a little way off).
	if (Perp->GetAimPose() != EFTOAimPose::None && State == EFTOPerpArrest::None)
	{
		GoTo(At, false, true);
		if (Close(At, 1300.f))
		{
			bGoal = !Close(At, 900.f);
			Face(At + FVector(0.f, 0.f, 40.f));
			if (Officer->GetDrawnWeapon() == EFTOWeapon::None)
			{
				Officer->SelectSlot(0);
			}
			else if (Now >= NextSwing)
			{
				NextSwing = Now + 0.9f;
				Officer->FirePressed();
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
		if (!bGoal || (!bChase && Close(Goal, 900.f)))
		{
			Car->SetAutopilot(true, -1.f, 0.f);
			if (Car->GetVelocity().Size() < 150.f && Now >= NextPress)
			{
				NextPress = Now + 1.5f;
				Car->RequestExit();
				Say(TEXT("out of the car."));
			}
			return;
		}
		const FVector To = Goal - Car->GetActorLocation();
		const float Angle = FMath::FindDeltaAngleDegrees(Car->GetActorRotation().Yaw, To.Rotation().Yaw);
		float Throttle = FMath::Clamp(To.Size2D() / 2500.f, 0.35f, 1.f) * (FMath::Abs(Angle) > 100.f ? 0.45f : 1.f);
		float Wheel = FMath::Clamp(Angle / 35.f, -1.f, 1.f);
		// Stuck against something: back off with the wheel the other way.
		StuckFor = Car->GetVelocity().Size() < 80.f ? StuckFor + DeltaSeconds : 0.f;
		if (StuckFor > 1.5f)
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
	if (bMayDrive && Distance > 5000.f && Style != TEXT("Explorer"))
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
						Say(TEXT("into a cruiser."));
					}
					return;
				}
				const FVector ToCar = (It->GetActorLocation() - Here()).GetSafeNormal2D();
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
	FVector Dir = (Goal - Here()).GetSafeNormal2D();
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
