#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Crime/FTOCrimeTypes.h"
#include "Animation/FTOAnimatedActor.h"
#include "FTOIncident.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class AFTOIncident;

DECLARE_MULTICAST_DELEGATE_OneParam(FFTOIncidentEvent, AFTOIncident*);

/**
 * A live incident somewhere in the city.
 * Server-authoritative: the server ticks timers, counts officers on scene and
 * resolves/escalates. Clients only render the replicated state.
 */
UCLASS()
class FTO_API AFTOIncident : public AActor, public IFTOAnimatedActor
{
	GENERATED_BODY()

public:
	AFTOIncident();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: configure a freshly spawned incident. */
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

	/** Server: make this a moving incident that rides along with Target (car chases). */
	void FollowActor(AActor* Target);

	/** Officers within this distance count as on scene (chases use a wider radius). */
	UFUNCTION(BlueprintPure, Category="Incident")
	float GetSceneRadius() const { return bMobile ? ChaseRadius : SceneRadius; }

	UFUNCTION(BlueprintPure, Category="Incident")
	bool IsMobile() const { return bMobile; }

	// IFTOAnimatedActor: the suspect acts shifty, then puts their hands up when officers arrive.
	virtual EFTOAnimAction GetAnimAction() const override;
	virtual float GetAnimSpeed() const override { return 0.f; }

	/** Current chaos per second this incident is pushing into the city (server). */
	float GetChaosRate() const;

	/** Server events consumed by the crime director. */
	FFTOIncidentEvent OnResolved;
	FFTOIncidentEvent OnFailed;
	FFTOIncidentEvent OnReported;

	/** Officers inside this radius count as on scene. */
	UPROPERTY(EditDefaultsOnly, Category="Incident")
	float SceneRadius = 450.f;

	/** On-scene radius while following a fleeing car; cruisers count too. */
	UPROPERTY(EditDefaultsOnly, Category="Incident")
	float ChaseRadius = 1100.f;

	/** Officers within this radius with line of sight witness an unreported incident. */
	UPROPERTY(EditDefaultsOnly, Category="Incident")
	float WitnessRadius = 2200.f;

	/** How long a finished incident lingers before it is destroyed. */
	UPROPERTY(EditDefaultsOnly, Category="Incident")
	float CleanupDelay = 4.f;

protected:
	virtual void BeginPlay() override;

	void ServerTick(float DeltaSeconds);
	int32 CountOfficersOnScene() const;
	bool IsWitnessedByAnyOfficer() const;
	void SetState(EFTOIncidentState NewState);

	UFUNCTION() void OnRep_State();
	UFUNCTION() void OnRep_Info();
	void RefreshVisuals();

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USceneComponent> Root;
	/** Placeholder "something is happening here" beacon. */
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Beacon;
	/** Placeholder suspect. */
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USkeletalMeshComponent> Suspect;
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
	UPROPERTY(ReplicatedUsing=OnRep_Mobile) bool bMobile = false;

	UFUNCTION() void OnRep_Mobile();

	// Server-only
	bool bWillBeReported = false;
	float ReportAt = 0.f;
	float WitnessCheckAccumulator = 0.f;
};
