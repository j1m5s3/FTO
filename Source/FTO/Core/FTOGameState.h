#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FTOGameState.generated.h"

class AFTOIncident;

UENUM(BlueprintType)
enum class EFTOShiftPhase : uint8
{
	Briefing,	// short countdown before crimes start
	OnDuty,
	Survived,	// made it to the end of the shift
	Overrun		// chaos hit 100
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFTOShiftPhaseChanged, EFTOShiftPhase, NewPhase);

/** Replicated, city-wide state every officer can see. */
UCLASS()
class FTO_API AFTOGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AFTOGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	static constexpr float MaxChaos = 100.f;

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
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO|Stats") float PeakChaos = 0.f;

	/** Seed for this shift's procedural generation (city layout + crimes). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="FTO") int32 ShiftSeed = 0;

protected:
	UPROPERTY(Replicated) float Chaos = 0.f;

	UPROPERTY(ReplicatedUsing=OnRep_ShiftPhase) EFTOShiftPhase ShiftPhase = EFTOShiftPhase::Briefing;
	UPROPERTY(Replicated) float BriefingEndTime = 0.f;
	UPROPERTY(Replicated) float ShiftEndTime = 0.f;

	UPROPERTY(Replicated) TArray<TObjectPtr<AFTOIncident>> Incidents;

	UFUNCTION() void OnRep_ShiftPhase();
};
