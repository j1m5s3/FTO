#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Crime/FTOCrimeTypes.h"
#include "FTOIncident.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class AController;
class AFTOIncident;
class AFTOCityGenerator;
class AFTOPerp;
class AFTOCharacter;

DECLARE_MULTICAST_DELEGATE_OneParam(FFTOIncidentEvent, AFTOIncident*);

/**
 * A live incident somewhere in the city, with its perp (AFTOPerp) standing at the heart of it.
 * Server-authoritative: the server ticks timers, counts officers on scene and resolves/escalates. Clients only
 * render the replicated state. Officers handle a call by being on scene long enough. A crime ends in an arrest:
 * the perp gives up once talked down (or run to ground, or put on the floor) and the call's handled when the
 * cuffs are on (AFTOPerp does the arresting, including suspects who fight or run).
 */
UCLASS()
class FTO_API AFTOIncident : public AActor
{
	GENERATED_BODY()

public:
	AFTOIncident();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: configure a freshly spawned incident (and put its perp in place). */
	void InitIncident(const FFTOIncidentInfo& InInfo, bool bWillBeReported, float InReportDelay);

	UFUNCTION(BlueprintPure, Category="Incident") FFTOIncidentInfo GetInfo() const { return Info; }
	UFUNCTION(BlueprintPure, Category="Incident") EFTOIncidentState GetState() const { return State; }
	/** 0-1 resolution progress. */
	UFUNCTION(BlueprintPure, Category="Incident") float GetProgress() const { return Progress; }
	UFUNCTION(BlueprintPure, Category="Incident") int32 GetOfficersOnScene() const { return OfficersOnScene; }
	UFUNCTION(BlueprintPure, Category="Incident") bool WasWitnessed() const { return bWitnessed; }
	/** Seconds since the incident started. */
	UFUNCTION(BlueprintPure, Category="Incident") float GetAge() const;
	/** 0-1, how close it is to escalating/failing. */
	UFUNCTION(BlueprintPure, Category="Incident") float GetUrgency() const;

	UFUNCTION(BlueprintPure, Category="Incident")
	bool IsActive() const { return State != EFTOIncidentState::Resolved && State != EFTOIncidentState::Failed; }

	/** Is this on the dispatch board? */
	UFUNCTION(BlueprintPure, Category="Incident")
	bool IsKnownToDispatch() const { return State == EFTOIncidentState::Reported || State == EFTOIncidentState::Responding; }

	/** Server: a citizen tip or radio call puts this on the board right now. */
	void ForceReport();

	/** Server: an officer spotted or called this in personally (counts as caught in the act). */
	void ReportByOfficer();

	/** Server: make this a moving incident that rides along with Target (car chases; the perp's in the car). */
	void FollowActor(AActor* Target);

	/**
	 * Server: the perp's given up (talked down, run to ground, or put on the floor by ByPolice): no more chaos from
	 * this one, it's handled once they're cuffed.
	 */
	void Subdue(AController* ByPolice);
	bool IsSubdued() const { return bSubdued; }
	/** Whoever subdued the perp, if anyone (server). */
	AController* GetSubduedBy() const { return SubduedBy.Get(); }

	/** Server: the cuffs are on (Officer did it): handled. */
	void CompleteArrest(AFTOCharacter* Officer);
	/** Whoever cuffed the perp (server). */
	AFTOCharacter* GetArrestingOfficer() const { return ArrestingOfficer.Get(); }

	/** Server: the perp's legged it on foot: the marker goes with them, and there's no talking anyone down meanwhile. */
	void StartFootChase();
	bool IsFootChase() const { return bFootChase; }

	/** Server: the perp got clean away: it goes cold. */
	void PerpGotAway();

	/** Server: fully talked down (or a chase run to ground): the perp gives up for the cuffs (a car chase's driver
	 *  climbs out first). What a full progress bar does for a crime. */
	void TalkedDown();

	/** Whoever's at the heart of it (null for car chases, the perp being in the car). */
	AFTOPerp* GetPerp() const { return Perp; }

	/**
	 * Server: the suspect's slipped away and is lying low in the crowd: the marker stays where they were last seen and
	 * the board carries their description. Citizens phone in sightings now and then (moving the marker), and it goes
	 * cold if nobody finds them in time. Talking to whoever matches (E) is how officers catch them.
	 */
	void StartSearch(const FVector& LastSeen);
	/** Server: someone saw the suspect near Where: the marker moves (roughly) there and the search clock's area resets. */
	void ReportSighting(const FVector& Where);
	/** Server: the perp's changed how they look: the board's description follows. */
	void RefreshSuspectDescription();
	bool IsSearching() const { return bSearch; }
	/** Seconds left to find them (every machine). */
	float GetSearchTimeLeft() const;
	/** How far from the marker they could be by now (every machine): the area to search. */
	float GetSearchRadius() const;

	// ---- Each kind of crime's twist ----
	/** A pickpocket working a crowd: pick them out by the description (nobody's talked down just by standing there). */
	bool IsCrowd() const { return bCrowd; }
	/** A burglar hiding somewhere in the building (upstairs, maybe): find them first. */
	bool IsHiddenInside() const { return bHiddenInside; }
	/** Server: set by the perp as they take up their place. */
	void SetCrowd(bool bInCrowd);
	void SetHiddenInside(bool bInHiding);
	/** Server: the hidden perp's been found at Where: the scene's there now (out of the building's room if upstairs). */
	void SuspectFound(const FVector& Where, bool bLeaveRoom);
	/** Server: progress made some other way than standing about (talking a drunk round). */
	void SetTalkProgress(float Value);
	/** Server: it's over, badly, right now (a bomb going off): the chaos hit, and it's gone. */
	void FailNow();
	/** Server: it's turned into something else (the heist crew have driven off: the getaway's the call now). */
	void Supersede();
	bool IsSuperseded() const { return bSuperseded; }
	/** Server: how many officers it takes (a set piece, scaled down for a small squad). */
	void SetOfficersRequired(int32 Count) { Info.OfficersRequired = FMath::Max(1, Count); }
	/** Server: handled without anyone cuffed (a drunk talked round and sent home): whoever's there gets the credit. */
	void HandledPeacefully();
	/** Is this one talked down just by officers being there (else it has a twist of its own)? */
	bool IsTalkedDownByPresence() const;
	/** A bar fight or street brawl: it takes two officers to pull them apart (or put them down by hand). */
	bool IsBrawl() const;
	/** Officers on scene before talking down even starts. */
	int32 GetMinCrew() const { return IsBrawl() ? 2 : 1; }
	/** One line for the officers about the twist (on the scene panel; every machine). */
	FString GetTwistHint() const;

	/** Everyone else caught up in it (the victim, the other brawlers); tidied away with the incident. */
	void AddExtra(AActor* Extra) { Extras.Add(Extra); }

	/** Server: this one's happening inside building Index (AFTOCityGenerator::GetBuildings). */
	void SetBuilding(int32 Index);
	int32 GetBuildingIndex() const { return BuildingIndex; }
	bool IsIndoors() const { return BuildingIndex != INDEX_NONE; }

	/** Officers within this distance count as on scene (chases use a wider radius). */
	UFUNCTION(BlueprintPure, Category="Incident")
	float GetSceneRadius() const { return bMobile ? ChaseRadius : SceneRadius; }

	UFUNCTION(BlueprintPure, Category="Incident")
	bool IsMobile() const { return bMobile; }

	/** Current chaos per second this incident is pushing into the city (server). */
	float GetChaosRate() const;

	/** Server events consumed by the crime director. */
	FFTOIncidentEvent OnResolved;
	FFTOIncidentEvent OnFailed;
	FFTOIncidentEvent OnReported;

	/** Officers inside this radius count as on scene. */
	UPROPERTY(EditDefaultsOnly, Category="Incident")
	float SceneRadius = 450.f;

	/** The bomb set piece: how long the squad has to defuse it. */
	UPROPERTY(EditDefaultsOnly, Category="Incident")
	float BombFuseSeconds = 150.f;

	/** On-scene radius while following a fleeing car; cruisers count too. */
	UPROPERTY(EditDefaultsOnly, Category="Incident")
	float ChaseRadius = 1100.f;

	/** How long the squad has to find a suspect who's slipped away before the trail goes cold. */
	UPROPERTY(EditDefaultsOnly, Category="Incident|Search")
	float SearchSeconds = 240.f;

	/** Seconds between citizens phoning in a sighting (a range). */
	UPROPERTY(EditDefaultsOnly, Category="Incident|Search")
	FVector2D SightingEvery = FVector2D(25.f, 40.f);

	/** A sighting's only roughly where they are: the marker lands within this of them. */
	UPROPERTY(EditDefaultsOnly, Category="Incident|Search")
	float SightingSpread = 600.f;

	/** Officers within this radius with line of sight witness an unreported incident. */
	UPROPERTY(EditDefaultsOnly, Category="Incident")
	float WitnessRadius = 2200.f;

	/** How long a finished incident lingers before it is destroyed. */
	UPROPERTY(EditDefaultsOnly, Category="Incident")
	float CleanupDelay = 4.f;

	/** Officers within this distance of a perp on the run are after them. */
	UPROPERTY(EditDefaultsOnly, Category="Incident")
	float FootChaseRadius = 2500.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void ServerTick(float DeltaSeconds);
	int32 CountOfficersOnScene() const;
	bool IsWitnessedByAnyOfficer() const;
	void SetState(EFTOIncidentState NewState);
	void Resolve();
	/** Server: the end of a car chase: the driver gets out beside the car, and they're the perp now. */
	void BringOutTheDriver();

	UFUNCTION() void OnRep_State();
	UFUNCTION() void OnRep_Info();
	void RefreshVisuals();

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USceneComponent> Root;
	/** Placeholder "something is happening here" beacon. */
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Beacon;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BeaconMaterial;

	UPROPERTY(ReplicatedUsing=OnRep_Info) FFTOIncidentInfo Info;
	UPROPERTY(ReplicatedUsing=OnRep_State) EFTOIncidentState State = EFTOIncidentState::Unreported;
	UPROPERTY(Replicated) float Progress = 0.f;
	UPROPERTY(Replicated) int32 OfficersOnScene = 0;
	UPROPERTY(Replicated) bool bWitnessed = false;
	/** Server world time when the incident started (replicated for UI timers). */
	UPROPERTY(Replicated) float StartTime = 0.f;
	/** Time without an officer on scene, drives escalation. */
	UPROPERTY(Replicated) float NeglectTime = 0.f;
	/** Riding along with a moving target; the suspect is inside it, not standing here. */
	UPROPERTY(Replicated) bool bMobile = false;
	/** The building it's in, if it's indoors (the beacon hangs under the ceiling). */
	UPROPERTY(ReplicatedUsing=OnRep_Info) int32 BuildingIndex = INDEX_NONE;
	UPROPERTY(Replicated) TObjectPtr<AFTOPerp> Perp;
	UPROPERTY(ReplicatedUsing=OnRep_Info) bool bSubdued = false;
	/** The perp's running and we're riding along with them. */
	UPROPERTY(ReplicatedUsing=OnRep_Info) bool bFootChase = false;
	/** The perp's slipped away into the crowd and we're where they were last seen. */
	UPROPERTY(ReplicatedUsing=OnRep_Info) bool bSearch = false;
	/** Server world times the search started and the last sighting came in. */
	UPROPERTY(Replicated) float SearchStartTime = 0.f;
	/** The twists: a pickpocket in a crowd, a burglar hiding in the building. */
	UPROPERTY(ReplicatedUsing=OnRep_Info) bool bCrowd = false;
	UPROPERTY(ReplicatedUsing=OnRep_Info) bool bHiddenInside = false;
	/** Turned into something else (no penalty, no sting). */
	UPROPERTY(ReplicatedUsing=OnRep_Info) bool bSuperseded = false;
	/** Server: the twist's been explained to the officers who turned up. */
	bool bTwistAnnounced = false;
	/** Server: the scene's been left alone a while: escalate, or go cold. */
	void TickNeglect(float DeltaSeconds);
	UPROPERTY(Replicated) float LastSightingTime = 0.f;

	/** Server: back off the search (they've been found and are running again, or they're caught). */
	void EndSearch();
	/** Server: the victim, the brawlers... (Setup). */
	void SpawnExtras();
	/** Server: the crowd a pickpocket hides in (the perp joins it). */
	void SpawnCrowd();
	/** Somewhere to stand Distance from Center along Dir (or closer, if there's a wall or a kerb in the way). */
	FVector CrowdSpot(const FVector& Center, const FVector& Dir, float Distance) const;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> Extras;
	float NextSightingTime = 0.f;

	// Server-only
	mutable TWeakObjectPtr<AFTOCityGenerator> City;
	TWeakObjectPtr<AController> SubduedBy;
	TWeakObjectPtr<AFTOCharacter> ArrestingOfficer;
	bool bWillBeReported = false;
	float ReportAt = 0.f;
	float WitnessCheckAccumulator = 0.f;
};
