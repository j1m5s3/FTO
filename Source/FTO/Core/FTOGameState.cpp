#include "Core/FTOGameState.h"
#include "Scoring/FTOScoring.h"
#include "GameFramework/PlayerState.h"
#include "Audio/FTOAudio.h"
#include "Crime/FTOIncident.h"
#include "GameFramework/Pawn.h"
#include "Weapons/FTOBallistics.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"

AFTOGameState::AFTOGameState()
{
	auto Load = [](const TCHAR* Name) -> USoundBase*
	{
		ConstructorHelpers::FObjectFinder<USoundBase> Finder(*FString::Printf(TEXT("/Game/FTO/Audio/%s.%s"), Name, Name));
		return Finder.Object;
	};
	SoundSet.SirenLoop = Load(TEXT("SW_SirenLoop"));
	SoundSet.EngineLoop = Load(TEXT("SW_EngineLoop"));
	SoundSet.Whistle = Load(TEXT("SW_Whistle"));
	SoundSet.Horn = Load(TEXT("SW_Horn"));
	SoundSet.Chime = Load(TEXT("SW_Chime"));
	SoundSet.Radio = Load(TEXT("SW_Radio"));
	SoundSet.Alarm = Load(TEXT("SW_Alarm"));
	SoundSet.Fanfare = Load(TEXT("SW_Fanfare"));
	SoundSet.Womp = Load(TEXT("SW_Womp"));
	SoundSet.Bugle = Load(TEXT("SW_Bugle"));
	SoundSet.Fail = Load(TEXT("SW_Fail"));
	SoundSet.Click = Load(TEXT("SW_Click"));
	SoundSet.Bonk = Load(TEXT("SW_Bonk"));
	SoundSet.ShotPistol = Load(TEXT("SW_ShotPistol"));
	SoundSet.ShotShotgun = Load(TEXT("SW_ShotShotgun"));
	SoundSet.ShotRifle = Load(TEXT("SW_ShotRifle"));
	SoundSet.Taser = Load(TEXT("SW_Taser"));
	SoundSet.Reload = Load(TEXT("SW_Reload"));
	SoundSet.Ricochet = Load(TEXT("SW_Ricochet"));
	SoundSet.DryFire = Load(TEXT("SW_DryFire"));
	SoundSet.Cuffs = Load(TEXT("SW_Cuffs"));
	SoundSet.Scuffle = Load(TEXT("SW_Scuffle"));
	SoundSet.Glass = Load(TEXT("SW_Glass"));
	SoundSet.Crash = Load(TEXT("SW_Crash"));
	SoundSet.Clang = Load(TEXT("SW_Clang"));
	SoundSet.GushLoop = Load(TEXT("SW_GushLoop"));
	SoundSet.FireLoop = Load(TEXT("SW_FireLoop"));
	SoundSet.TireSkidLoop = Load(TEXT("SW_TireSkidLoop"));
	SoundSet.CityAmbienceLoop = Load(TEXT("SW_CityAmbienceLoop"));
	SoundSet.ElevatorLoop = Load(TEXT("SW_ElevatorLoop"));
	SoundSet.ElevatorDing = Load(TEXT("SW_ElevatorDing"));
	SoundSet.DoorOpen = Load(TEXT("SW_DoorOpen"));
	SoundSet.DoorClose = Load(TEXT("SW_DoorClose"));
	SoundSet.Collapse = Load(TEXT("SW_Collapse_01"));

	// The numbered takes. A sound that had just the one (a gunshot, a crash) keeps it among its takes.
	for (const FTOAudio::FFamilySpec& Spec : FTOAudio::Families())
	{
		FFTOSoundFamily& Family = SoundSet.Families.FindOrAdd(Spec.Name);
		for (int32 Take = 1; Take <= Spec.Takes; ++Take)
		{
			if (USoundBase* Sound = Load(*FString::Printf(TEXT("SW_%s_%02d"), *Spec.Name.ToString(), Take)))
			{
				Family.Sounds.Add(Sound);
			}
		}
	}
	auto Takes = [this](USoundBase* Original, FName Family)
	{
		if (Original && SoundSet.Families.Contains(Family))
		{
			SoundSet.Families[Family].Sounds.AddUnique(Original);
			SoundSet.TakesOf.Add(Original, Family);
		}
	};
	Takes(SoundSet.ShotPistol, TEXT("ShotPistol"));
	Takes(SoundSet.ShotShotgun, TEXT("ShotShotgun"));
	Takes(SoundSet.ShotRifle, TEXT("ShotRifle"));
	Takes(SoundSet.Ricochet, TEXT("Ricochet"));
	Takes(SoundSet.Crash, TEXT("Crash"));
	Takes(SoundSet.Glass, TEXT("Glass"));
	Takes(SoundSet.Clang, TEXT("Clang"));

	static ConstructorHelpers::FObjectFinder<USoundAttenuation> WorldAttenuation(TEXT("/Game/FTO/Audio/SA_FTOWorld.SA_FTOWorld"));
	SoundSet.World = WorldAttenuation.Object;
}

void AFTOGameState::MulticastPlaySound_Implementation(USoundBase* Sound, FVector_NetQuantize Location, float Volume)
{
	if (Sound && GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FTOAudio::Vary(Sound), Location, Volume, FMath::FRandRange(0.96f, 1.04f), 0.f, SoundSet.World);
	}
}

void AFTOGameState::MulticastShot_Implementation(AActor* Shooter, EFTOWeapon Weapon, FVector_NetQuantize Origin, FVector_NetQuantizeNormal Aim, int32 Seed, bool bAimed)
{
	// The server flew (and drew) its own copy; the shooter's machine drew theirs the moment they pulled the trigger.
	const APawn* Pawn = Cast<APawn>(Shooter);
	if (HasAuthority() || (Pawn && Pawn->IsLocallyControlled()))
	{
		return;
	}
	if (UFTOBallistics* Ballistics = UFTOBallistics::Get(GetWorld()))
	{
		Ballistics->Fire(Shooter, Weapon, Origin, Aim, Seed, bAimed, false, true);
	}
}

void AFTOGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOGameState, Chaos);
	DOREPLIFETIME(AFTOGameState, ShiftPhase);
	DOREPLIFETIME(AFTOGameState, BriefingEndTime);
	DOREPLIFETIME(AFTOGameState, ShiftEndTime);
	DOREPLIFETIME(AFTOGameState, VoteEndTime);
	DOREPLIFETIME(AFTOGameState, VoteDuration);
	DOREPLIFETIME(AFTOGameState, OvertimeOffer);
	DOREPLIFETIME(AFTOGameState, SquadCombo);
	DOREPLIFETIME(AFTOGameState, SquadComboTime);
	DOREPLIFETIME(AFTOGameState, bTagTeam);
	DOREPLIFETIME(AFTOGameState, BestSquadCombo);
	DOREPLIFETIME(AFTOGameState, SetPiece);
	DOREPLIFETIME(AFTOGameState, SetPieceTime);
	DOREPLIFETIME(AFTOGameState, Overtimes);
	DOREPLIFETIME(AFTOGameState, Incidents);
	DOREPLIFETIME(AFTOGameState, IncidentsResolved);
	DOREPLIFETIME(AFTOGameState, IncidentsFailed);
	DOREPLIFETIME(AFTOGameState, IncidentsWitnessed);
	DOREPLIFETIME(AFTOGameState, TrafficStops);
	DOREPLIFETIME(AFTOGameState, SuspectsBooked);
	DOREPLIFETIME(AFTOGameState, CiviliansBowledOver);
	DOREPLIFETIME(AFTOGameState, PropertyBroken);
	DOREPLIFETIME(AFTOGameState, CarsWrecked);
	DOREPLIFETIME(AFTOGameState, PeakChaos);
	DOREPLIFETIME(AFTOGameState, ShiftSeed);
}

void AFTOGameState::AddChaos(float Delta)
{
	check(HasAuthority());
	Chaos = FMath::Clamp(Chaos + Delta, 0.f, MaxChaos);
	PeakChaos = FMath::Max(PeakChaos, Chaos);
}

float AFTOGameState::GetShiftTimeRemaining() const
{
	if (ShiftPhase == EFTOShiftPhase::Survived || ShiftPhase == EFTOShiftPhase::Overrun || ShiftPhase == EFTOShiftPhase::OvertimeVote)
	{
		return 0.f;
	}
	return FMath::Max(0.f, ShiftEndTime - GetServerWorldTimeSeconds());
}

float AFTOGameState::GetBriefingTimeRemaining() const
{
	return ShiftPhase == EFTOShiftPhase::Briefing ? FMath::Max(0.f, BriefingEndTime - GetServerWorldTimeSeconds()) : 0.f;
}

void AFTOGameState::SetShiftPhase(EFTOShiftPhase NewPhase)
{
	check(HasAuthority());
	if (ShiftPhase != NewPhase)
	{
		ShiftPhase = NewPhase;
		OnRep_ShiftPhase();
	}
}

void AFTOGameState::SetShiftTimes(float InBriefingEnd, float InShiftEnd)
{
	BriefingEndTime = InBriefingEnd;
	ShiftEndTime = InShiftEnd;
}

void AFTOGameState::RegisterIncident(AFTOIncident* Incident)
{
	Incidents.AddUnique(Incident);
}

void AFTOGameState::UnregisterIncident(AFTOIncident* Incident)
{
	Incidents.Remove(Incident);
}

void AFTOGameState::OnRep_ShiftPhase()
{
	OnShiftPhaseChanged.Broadcast(ShiftPhase);
}

float AFTOGameState::GetVoteTimeRemaining() const
{
	return ShiftPhase == EFTOShiftPhase::OvertimeVote ? FMath::Max(0.f, VoteEndTime - GetServerWorldTimeSeconds()) : 0.f;
}

void AFTOGameState::BeginOvertimeVote(float Seconds, float Offer)
{
	check(HasAuthority());
	VoteDuration = Seconds;
	OvertimeOffer = Offer;
	VoteEndTime = GetServerWorldTimeSeconds() + Seconds;
	SetShiftPhase(EFTOShiftPhase::OvertimeVote);
}

float AFTOGameState::BumpSquadCombo(const APlayerState* Officer)
{
	check(HasAuthority());
	const float Now = GetServerWorldTimeSeconds();
	if (Now - SquadComboTime <= FTOScoring::SquadComboWindow && SquadCombo > 0)
	{
		++SquadCombo;
		bTagTeam |= LastSquadContributor.IsValid() && LastSquadContributor.Get() != Officer;
	}
	else
	{
		SquadCombo = 1;
		bTagTeam = false;
	}
	SquadComboTime = Now;
	LastSquadContributor = Officer;
	BestSquadCombo = FMath::Max(BestSquadCombo, SquadCombo);
	return GetSquadMultiplier();
}

void AFTOGameState::BreakSquadCombo()
{
	check(HasAuthority());
	SquadCombo = FMath::Max(0, SquadCombo - FTOScoring::SquadPenaltySteps);
	if (SquadCombo == 0)
	{
		bTagTeam = false;
		SquadComboTime = -1000.f;
	}
}

int32 AFTOGameState::GetSquadCombo() const
{
	return GetServerWorldTimeSeconds() - SquadComboTime <= FTOScoring::SquadComboWindow ? SquadCombo : 0;
}

float AFTOGameState::GetSquadMultiplier() const
{
	return FTOScoring::SquadMultiplier(GetSquadCombo(), bTagTeam, FMath::Max(1, PlayerArray.Num()));
}

float AFTOGameState::GetSquadComboFuse() const
{
	return GetSquadCombo() > 0 ? FMath::Clamp(1.f - (GetServerWorldTimeSeconds() - SquadComboTime) / FTOScoring::SquadComboWindow, 0.f, 1.f) : 0.f;
}

bool AFTOGameState::IsRushHour() const
{
	return ShiftPhase == EFTOShiftPhase::OnDuty && GetShiftTimeRemaining() > 0.f && GetShiftTimeRemaining() <= RushHourSeconds;
}

float AFTOGameState::GetSetPieceAge() const
{
	return GetServerWorldTimeSeconds() - SetPieceTime;
}

void AFTOGameState::AnnounceSetPiece(FName Which)
{
	check(HasAuthority());
	SetPiece = Which;
	SetPieceTime = GetServerWorldTimeSeconds();
}

void AFTOGameState::StartOvertime(float Seconds)
{
	check(HasAuthority());
	++Overtimes;
	ShiftEndTime = GetServerWorldTimeSeconds() + Seconds;
	SetShiftPhase(EFTOShiftPhase::OnDuty);
}