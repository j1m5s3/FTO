#include "Core/FTOGameState.h"
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

	static ConstructorHelpers::FObjectFinder<USoundAttenuation> WorldAttenuation(TEXT("/Game/FTO/Audio/SA_FTOWorld.SA_FTOWorld"));
	SoundSet.World = WorldAttenuation.Object;
}

void AFTOGameState::MulticastPlaySound_Implementation(USoundBase* Sound, FVector_NetQuantize Location, float Volume)
{
	if (Sound && GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Location, Volume, 1.f, 0.f, SoundSet.World);
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
	DOREPLIFETIME(AFTOGameState, Incidents);
	DOREPLIFETIME(AFTOGameState, IncidentsResolved);
	DOREPLIFETIME(AFTOGameState, IncidentsFailed);
	DOREPLIFETIME(AFTOGameState, IncidentsWitnessed);
	DOREPLIFETIME(AFTOGameState, TrafficStops);
	DOREPLIFETIME(AFTOGameState, SuspectsBooked);
	DOREPLIFETIME(AFTOGameState, CiviliansBowledOver);
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
	if (ShiftPhase == EFTOShiftPhase::Survived || ShiftPhase == EFTOShiftPhase::Overrun)
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
