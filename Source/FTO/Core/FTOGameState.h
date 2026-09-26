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
	Overrun		// chaos hit 100
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFTOShiftPhaseChanged, EFTOShiftPhase, NewPhase);

/** Every in-house synthesised sound (Tools/Unreal/make_audio.py). */
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
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO|Stats") float PeakChaos = 0.f;

	/** Seed for this shift's procedural generation (city layout + crimes). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO") int32 ShiftSeed = 0;

protected:
	UPROPERTY(Replicated) float Chaos = 0.f;

	UPROPERTY(ReplicatedUsing=OnRep_ShiftPhase) EFTOShiftPhase ShiftPhase = EFTOShiftPhase::Lobby;
	UPROPERTY(Replicated) float BriefingEndTime = 0.f;
	UPROPERTY(Replicated) float ShiftEndTime = 0.f;

	UPROPERTY(Replicated) TArray<TObjectPtr<AFTOIncident>> Incidents;

	UFUNCTION() void OnRep_ShiftPhase();
};
