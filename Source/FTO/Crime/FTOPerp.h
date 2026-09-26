#pragma once

#include "CoreMinimal.h"
#include "City/FTOPedestrian.h"
#include "FTOPerp.generated.h"

class AFTOIncident;
class UStaticMeshComponent;

/**
 * Whoever is at the heart of an incident: the robber at the counter, the brawler, the burglar in the racks, the
 * neighbour with the bagpipes (or, on calls without a crook, the citizen who needs a hand). Stands at the incident
 * doing the deed, and puts their hands up once the police arrive.
 *
 * Armed perps (hold-ups, stand-offs, the "Armed" twist) carry a pistol and shoot at officers who come close, until
 * they're outnumbered or talked down. Put a perp on the floor (a shot, the taser, a tackle, a bumper) and the
 * incident is handled on the spot. Server-driven like the crowd.
 */
UCLASS()
class FTO_API AFTOPerp : public AFTOPedestrian
{
	GENERATED_BODY()

public:
	AFTOPerp();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: take up the incident's spot. Criminals wear the striped jumper; everyone else is a citizen. */
	void Setup(AFTOIncident* InIncident, bool bInCriminal, bool bInArmed, int32 Seed);

	/** Server: the police put us on the floor: our incident is handled. */
	void Subdued(AController* ByPolice);

	AFTOIncident* GetIncident() const { return Incident; }
	bool IsCriminal() const { return bCriminal; }

	// IFTOInteractable: officers handle a scene by being there (questioning is for crooks lying low).
	virtual bool CanInteract(const AFTOCharacter* Officer) const override { return false; }

	// IFTOAnimatedActor
	virtual EFTOAnimAction GetAnimAction() const override;
	virtual EFTOAimPose GetAimPose() const override;
	virtual float GetAimPitch() const override { return AimPitch; }

	/** Server: back to the spot (after a tumble or a whistle). */
	virtual void Resume() override;

	/** How close an officer has to get before an armed perp opens fire (cm). */
	UPROPERTY(EditDefaultsOnly, Category="Perp") float FiringRange = 2000.f;
	/** Seconds from spotting an officer to the first shot (time to get the drop on them). */
	UPROPERTY(EditDefaultsOnly, Category="Perp") float DrawSeconds = 1.5f;
	/** How far (degrees) a perp's aim wanders off the officer. */
	UPROPERTY(EditDefaultsOnly, Category="Perp") float WildAim = 7.f;

protected:
	virtual void ApplyLook() override;
	virtual void OnArrived() override;

	/** Server: armed and cornered: pick an officer, turn to them, and fire now and then. */
	void TickShooting(float DeltaSeconds);
	bool IsStillFighting() const;
	AActor* FindTarget() const;
	void Shoot();

	UFUNCTION() void OnRep_Armed();

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Gun;
	UPROPERTY() TObjectPtr<USkeletalMesh> SuspectLook;

	UPROPERTY(Replicated) TObjectPtr<AFTOIncident> Incident;
	UPROPERTY(ReplicatedUsing=OnRep_Look) bool bCriminal = true;
	UPROPERTY(ReplicatedUsing=OnRep_Armed) bool bArmed = false;
	/** Gun up at an officer right now. */
	UPROPERTY(Replicated) bool bShooting = false;
	UPROPERTY(Replicated) float AimPitch = 0.f;

	FVector Home = FVector::ZeroVector;
	float HomeYaw = 0.f;
	TWeakObjectPtr<AActor> Target;
	float NextShotTime = 0.f;
	float ShootCheckAccumulator = 0.f;
};
