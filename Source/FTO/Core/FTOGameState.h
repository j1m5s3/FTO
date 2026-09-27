#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Weapons/FTOWeapons.h"
#include "FTOGameState.generated.h"

class AFTOIncident;
class USoundBase;
class USoundAttenuation;

UENUM(BlueprintType)
enum class EFTOShiftPhase : uint8
{
	Lobby,		// officers gather at the precinct until the host starts the shift
	Briefing,	// short countdown before crimes start
	OnDuty,
	Survived,	// made it to the end of the shift
	Overrun,	// chaos hit 100
	OvertimeVote	// the clock ran out: the squad votes to keep going (overtime) or clock off
};

/** An officer's say when the shift clock runs out. */
UENUM(BlueprintType)
enum class EFTOShiftVote : uint8
{
	None,
	Overtime,
	ClockOff
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFTOShiftPhaseChanged, EFTOShiftPhase, NewPhase);

/** Several takes of one sound (SW_Punch_01..04): FTOAudio::Pick plays one at random so repeats don't sound canned. */
USTRUCT()
struct FFTOSoundFamily
{
	GENERATED_BODY()

	UPROPERTY() TArray<TObjectPtr<USoundBase>> Sounds;
};

/** Every in-house synthesised sound (Tools/Audio/fto_synth.py, imported by Tools/Unreal/make_audio.py). */
USTRUCT(BlueprintType)
struct FFTOSoundSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> SirenLoop;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> EngineLoop;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Whistle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Horn;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Chime;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Radio;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Alarm;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Fanfare;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Womp;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Bugle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Fail;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Click;
	/** Cartoon thump-and-boing when somebody gets knocked flying. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Bonk;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> ShotPistol;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> ShotShotgun;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> ShotRifle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Taser;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Reload;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Ricochet;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> DryFire;
	/** Handcuffs ratcheting shut. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Cuffs;
	/** A suspect fighting back: a flurry of cartoon thumps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Scuffle;
	/** Things breaking: a window, a car crash, street furniture knocked flying; a burst hydrant and a burning car (loops). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Glass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Crash;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Clang;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> GushLoop;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> FireLoop;
	/** Loops: tyres squealing in a slide, the city's distant hum, a lift's motor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> TireSkidLoop;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> CityAmbienceLoop;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> ElevatorLoop;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> ElevatorDing;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> DoorOpen;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> DoorClose;
	/** A building coming down. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Collapse;
	/** The numbered takes, by family name ("Punch", "Step_Wood", "CarImpactHeavy"...; see FTOAudio). */
	UPROPERTY() TMap<FName, FFTOSoundFamily> Families;
	/** Sounds with takes of their own (the gunshots, crashes, glass...): which family to pick from instead. */
	UPROPERTY() TMap<TObjectPtr<USoundBase>, FName> TakesOf;
	/** Shared 3D falloff for sounds in the world. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundAttenuation> World;
};

/** Replicated, city-wide state every officer can see. */
UCLASS()
class FTO_API AFTOGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AFTOGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	static constexpr float MaxChaos = 100.f;

	/** The sound set (available even before a game state exists). */
	static const FFTOSoundSet& Sounds() { return GetDefault<AFTOGameState>()->SoundSet; }

	/** Server: play a sound in the world for everyone. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlaySound(USoundBase* Sound, FVector_NetQuantize Location, float Volume = 1.f);

	/**
	 * Server: someone fired. Every other machine flies its own copy of the rounds to draw them (the server's copy
	 * decides the hits, and the shooter's own machine already drew theirs).
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShot(AActor* Shooter, EFTOWeapon Weapon, FVector_NetQuantize Origin, FVector_NetQuantizeNormal Aim, int32 Seed, bool bAimed);

	UPROPERTY(EditDefaultsOnly, Category="FTO|Audio")
	FFTOSoundSet SoundSet;

	// ---- Chaos meter ----
	UFUNCTION(BlueprintPure, Category="FTO|Chaos") float GetChaos() const { return Chaos; }
	UFUNCTION(BlueprintPure, Category="FTO|Chaos") float GetChaosAlpha() const { return Chaos / MaxChaos; }
	/** Server only. Positive adds chaos, negative relieves it. */
	void AddChaos(float Delta);

	// ---- Shift ----
	UFUNCTION(BlueprintPure, Category="FTO|Shift") EFTOShiftPhase GetShiftPhase() const { return ShiftPhase; }
	UFUNCTION(BlueprintPure, Category="FTO|Shift") float GetShiftTimeRemaining() const;
	UFUNCTION(BlueprintPure, Category="FTO|Shift") float GetBriefingTimeRemaining() const;
	/** Server only. */
	void SetShiftPhase(EFTOShiftPhase NewPhase);
	/** Server only. */
	void SetShiftTimes(float InBriefingEnd, float InShiftEnd);

	// ---- End of shift: overtime or clock off ----
	/** Seconds left to vote (OvertimeVote only). */
	float GetVoteTimeRemaining() const;
	/** How many times the squad has gone into overtime this shift. */
	int32 GetOvertimes() const { return Overtimes; }
	/** How long the vote runs, and how much overtime is on offer (seconds). */
	float GetVoteDuration() const { return VoteDuration; }
	float GetOvertimeOffer() const { return OvertimeOffer; }
	/** Server: the clock's run out: everyone has Seconds to vote for Offer seconds more, or to clock off. */
	void BeginOvertimeVote(float Seconds, float Offer);
	/** Server: the squad voted to keep going: Seconds more on the clock, chaos as it was. */
	void StartOvertime(float Seconds);

	UPROPERTY(BlueprintAssignable, Category="FTO|Shift")
	FFTOShiftPhaseChanged OnShiftPhaseChanged;

	// ---- Incidents ----
	const TArray<TObjectPtr<AFTOIncident>>& GetIncidents() const { return Incidents; }
	/** Server only. */
	void RegisterIncident(AFTOIncident* Incident);
	void UnregisterIncident(AFTOIncident* Incident);

	// ---- Shift stats (end-of-shift report card) ----
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO|Stats") int32 IncidentsResolved = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO|Stats") int32 IncidentsFailed = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO|Stats") int32 IncidentsWitnessed = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO|Stats") int32 TrafficStops = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO|Stats") int32 SuspectsBooked = 0;
	/** Citizens the police bowled over (cruisers, tackles). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO|Stats") int32 CiviliansBowledOver = 0;
	/** Things the police broke (windows, street furniture), and citizens' cars they wrote off. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO|Stats") int32 PropertyBroken = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO|Stats") int32 CarsWrecked = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO|Stats") float PeakChaos = 0.f;

	/** Seed for this shift's procedural generation (city layout + crimes). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO") int32 ShiftSeed = 0;

protected:
	UPROPERTY(Replicated) float Chaos = 0.f;

	UPROPERTY(ReplicatedUsing=OnRep_ShiftPhase) EFTOShiftPhase ShiftPhase = EFTOShiftPhase::Lobby;
	UPROPERTY(Replicated) float BriefingEndTime = 0.f;
	UPROPERTY(Replicated) float ShiftEndTime = 0.f;
	UPROPERTY(Replicated) float VoteEndTime = 0.f;
	UPROPERTY(Replicated) float VoteDuration = 20.f;
	UPROPERTY(Replicated) float OvertimeOffer = 600.f;
	UPROPERTY(Replicated) int32 Overtimes = 0;

	UPROPERTY(Replicated) TArray<TObjectPtr<AFTOIncident>> Incidents;

	UFUNCTION() void OnRep_ShiftPhase();
};
