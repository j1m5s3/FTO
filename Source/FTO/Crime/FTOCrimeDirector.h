#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FTOCrimeDirector.generated.h"

class AFTOGameState;
class AFTOIncident;
class AFTOCrimeSpawnPoint;
class UFTOCrimeCatalog;
struct FFTOCrimeTemplate;

/**
 * Server-only brain of the shift. Lives on the game mode.
 * Rolls procedural incidents, pumps their chaos into the city, handles
 * escalation, and decides when the shift is won or lost.
 *
 * The feedback loop: more chaos -> crimes arrive faster and nastier -> more chaos.
 * Players have to break it.
 */
UCLASS(ClassGroup=(FTO), meta=(BlueprintSpawnableComponent))
class FTO_API UFTOCrimeDirector : public UActorComponent
{
	GENERATED_BODY()

public:
	UFTOCrimeDirector();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Starts a new shift with the given seed. */
	void BeginShift(int32 Seed);

	/** Re-scan the world for crime spawn points (call after the city is generated). */
	void RefreshSpawnPoints();

	/** Spawns a specific template right now (debug / scripted events). */
	AFTOIncident* SpawnIncident(FName TemplateId, bool bForceReported = false);

	/** Spawns a specific template at a given spot (e.g. a traffic stop gone wrong). */
	AFTOIncident* SpawnIncidentAt(FName TemplateId, const FVector& Location, bool bForceReported = true);

	/** Optional designer override; defaults are built in. */
	UPROPERTY(EditAnywhere, Category="Director")
	TObjectPtr<UFTOCrimeCatalog> CatalogOverride;

	UPROPERTY(EditAnywhere, Category="Director|Shift")
	float BriefingSeconds = 10.f;

	UPROPERTY(EditAnywhere, Category="Director|Shift")
	float ShiftLengthSeconds = 20.f * 60.f;

	/** Seconds between new incidents at 0 chaos and at 100 chaos. */
	UPROPERTY(EditAnywhere, Category="Director|Pacing")
	FVector2D SpawnIntervalRange = FVector2D(22.f, 7.f);

	/** Spawn interval multiplier per officer count (index = officers - 1). */
	UPROPERTY(EditAnywhere, Category="Director|Pacing")
	TArray<float> OfficerPacing = { 1.7f, 1.3f, 1.1f, 1.0f };

	UPROPERTY(EditAnywhere, Category="Director|Pacing")
	int32 BaseMaxActiveIncidents = 4;

	UPROPERTY(EditAnywhere, Category="Director|Pacing")
	int32 MaxActiveIncidentsPerOfficer = 2;

	/** City calms down by itself this much per second. */
	UPROPERTY(EditAnywhere, Category="Director|Chaos")
	float PassiveDecayPerSecond = 0.05f;

	/** Extra relief multiplier for catching a crime in the act. */
	UPROPERTY(EditAnywhere, Category="Director|Chaos")
	float WitnessBonus = 1.5f;

	/** New incidents won't spawn closer than this to an active one. */
	UPROPERTY(EditAnywhere, Category="Director|Placement")
	float MinIncidentSpacing = 1500.f;

	/** Fallback random placement radius when no spawn points exist. */
	UPROPERTY(EditAnywhere, Category="Director|Placement")
	float FallbackRadius = 6000.f;

	UPROPERTY(EditAnywhere, Category="Director|Placement")
	TSubclassOf<AFTOIncident> IncidentClass;

protected:
	virtual void BeginPlay() override;

	void EnsureCatalog();
	AFTOGameState* GetFTOGameState() const;
	int32 CountActiveIncidents() const;
	int32 GetOfficerCount() const;

	const FFTOCrimeTemplate* PickTemplate(float Chaos);
	bool PickLocation(FName TemplateId, FVector& OutLocation);
	bool IsTooCloseToActiveIncident(const FVector& Location) const;
	AFTOIncident* SpawnFromTemplate(const FFTOCrimeTemplate& Template, const FVector& Location, bool bForceReported);

	void HandleResolved(AFTOIncident* Incident);
	void HandleFailed(AFTOIncident* Incident);
	void HandleReported(AFTOIncident* Incident);

	UFUNCTION()
	void HandleIncidentDestroyed(AActor* DestroyedActor);

	void TickOnDuty(float DeltaTime);

	UPROPERTY(Transient)
	TObjectPtr<UFTOCrimeCatalog> Catalog;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AFTOCrimeSpawnPoint>> SpawnPoints;

	FRandomStream Rng;
	float NextSpawnTime = 0.f;
	bool bShiftStarted = false;
};
