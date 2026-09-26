#pragma once

#include "CoreMinimal.h"
#include "City/FTOPedestrian.h"
#include "FTOPerp.generated.h"

class AFTOIncident;
class UStaticMeshComponent;

/** Where an arrest stands, from the perp's side. */
UENUM()
enum class EFTOPerpArrest : uint8
{
	None,			// up to no good (or hands up, or shooting)
	Surrendered,	// on their knees, hands on head, waiting for the cuffs
	Struggling,		// fighting off an officer, who mashes Interact to win
	Fleeing,		// legging it on foot
	Cuffing			// kneeling while an officer puts the cuffs on
};

/** How a suspect takes an officer's attempt to arrest them. */
UENUM()
enum class EFTOArrestResponse : uint8
{
	Roll,		// decide on the spot (the tests can force the others)
	Comply,
	Struggle,
	Bolt
};

/**
 * Whoever is at the heart of an incident: the robber at the counter, the brawler, the burglar in the racks, the
 * neighbour with the bagpipes (or, on calls without a crook, the citizen who needs a hand). Stands at the incident
 * doing the deed, and puts their hands up once the police arrive.
 *
 * Armed perps (hold-ups, stand-offs, the "Armed" twist) carry a pistol and shoot at officers who come close, until
 * they're outnumbered or talked down.
 *
 * Arrests: an officer walks up and presses Interact. A suspect who's given up (talked down, run to ground, or put on
 * the floor by a shot, the taser, a tackle or a bumper) kneels for the cuffs: the officer steps in behind them and
 * cuffs them, a synced two-person move, and they become an AFTOArrestee. One who hasn't may come quietly, or fight
 * back (a struggle: mash Interact, partners can pile in, lose and you're shoved over) or bolt on foot (sprint after
 * them and tackle). Server-driven like the crowd.
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

	/** Server: the police put us on the floor: we'll kneel for the cuffs once we're up. */
	void Subdued(AController* ByPolice);

	/** Server: done fighting (talked down, run to ground, floored): on our knees, hands on head, waiting for the cuffs. */
	void GiveUp(AController* ByPolice);

	/** Server: an officer (or a partner piling in) heaves in a struggle. */
	void Mash(AFTOCharacter* Officer);

	AFTOIncident* GetIncident() const { return Incident; }
	bool IsCriminal() const { return bCriminal; }
	EFTOPerpArrest GetArrestState() const { return ArrestState; }
	bool IsFleeing() const { return ArrestState == EFTOPerpArrest::Fleeing; }
	/** Wrestling with an officer or being cuffed: the scene's on hold meanwhile. */
	bool IsInArrest() const { return ArrestState == EFTOPerpArrest::Struggling || ArrestState == EFTOPerpArrest::Cuffing; }
	/** 0-1: how close the officers are to winning a struggle. */
	float GetStruggleMeter() const { return StruggleMeter; }
	/** 0-1 through the cuffing (every machine). */
	float GetCuffProgress() const;
	/** The officer we're struggling with or being cuffed by. */
	AFTOCharacter* GetArrester() const { return Arrester; }

	/** Tests: how the next arrest attempt goes (and no bolting at the mere sight of an officer). */
	void SetForcedResponse(EFTOArrestResponse Response) { ForcedResponse = Response; }

	/** Server: tell every officer within Radius of us (bar Except) what's going on. */
	void ToastOfficersNear(const FText& Message, const FLinearColor& Color, float Radius, const AActor* Except = nullptr) const;

	// IFTOInteractable: arrest a crook, cuff one who's given up, or pile in to help a partner wrestle one. (Citizens
	// at calls are handled by being there.)
	virtual bool CanInteract(const AFTOCharacter* Officer) const override;
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual void Interact(AFTOCharacter* Officer) override;
	virtual float GetInteractRange() const override { return 300.f; }

	// IFTOAnimatedActor
	virtual EFTOAnimAction GetAnimAction() const override;
	virtual EFTOAimPose GetAimPose() const override;
	virtual float GetAimPitch() const override { return AimPitch; }

	/** Server: back to the spot (after a tumble or a whistle), unless we're mid-arrest. */
	virtual void Resume() override;

	/** How close an officer has to get before an armed perp opens fire (cm). */
	UPROPERTY(EditDefaultsOnly, Category="Perp") float FiringRange = 2000.f;
	/** Seconds from spotting an officer to the first shot (time to get the drop on them). */
	UPROPERTY(EditDefaultsOnly, Category="Perp") float DrawSeconds = 1.5f;
	/** How far (degrees) a perp's aim wanders off the officer. */
	UPROPERTY(EditDefaultsOnly, Category="Perp") float WildAim = 7.f;

	/** How long the cuffs take to go on. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float CuffSeconds = 2.6f;
	/** How far behind a kneeling suspect the officer stands to cuff them, so their hands meet the suspect's wrists (the
	 *  officer's cuffing clip in Tools/Blender/build_officer.py reaches about this far ahead of their feet). */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float CuffDistance = 76.f;
	/** Face to face in a struggle. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float StruggleDistance = 80.f;
	/** A struggle starts with the meter here; it drains every second and every heave fills it (a partner's a bit
	 *  less). Full and the officers win; empty (or too long) and the suspect breaks free. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float StruggleStart = 0.4f;
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float StruggleDrain = 0.2f;
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float StruggleHeave = 0.08f;
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float StruggleMaxSeconds = 7.f;
	/** On foot: a sprinting officer (850) closes in, a jogging one (500) doesn't. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float FleeSpeed = 620.f;
	/** Out of puff after this long, they give up. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float FleeSeconds = 18.f;
	/** No officer within GetAwayDistance for GetAwaySeconds and they're gone. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float GetAwayDistance = 4000.f;
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float GetAwaySeconds = 5.f;
	/** Left kneeling with no officer about for this long, they slip away. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float WaitForCuffsSeconds = 45.f;
	/** A suspect "on the move" (the Fleeing twist) bolts as soon as an officer gets this close. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float SpookDistance = 900.f;

protected:
	virtual void ApplyLook() override;
	virtual void OnArrived() override;
	virtual void FaceOfficer(const AActor* Officer) override;

	/** Server: armed and cornered: pick an officer, turn to them, and fire now and then. */
	void TickShooting(float DeltaSeconds);
	bool IsStillFighting() const;
	AActor* FindTarget() const;
	void Shoot();

	/** Server: the arrest side of things (struggles, chases, waiting for the cuffs). */
	void TickArrest(float DeltaSeconds);
	void TryArrest(AFTOCharacter* Officer);
	EFTOArrestResponse RollResponse(const AFTOCharacter* Officer);
	void BeginCuffing(AFTOCharacter* Officer);
	void FinishCuffing();
	void BeginStruggle(AFTOCharacter* Officer);
	void EndStruggle(bool bOfficersWon);
	void BeginFleeing(const AActor* From);
	void RunToNextWaypoint();
	void GetAway();
	/** Let go of the officer we're locked into a move with (if they still are). */
	void ReleaseArrester();
	/** Is Arrester still locked into a move with us? */
	bool IsArresterWithUs() const;
	/** The nearest officer (on foot or at the wheel) and how far away. */
	AActor* FindNearestOfficer(float& OutDistance) const;
	AFTOCityGenerator* FindCity();
	/** Cosmetic, every machine: a cartoon dust cloud around a struggle. */
	void UpdateScuffleCloud(float DeltaSeconds);

	UFUNCTION() void OnRep_Armed();

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Gun;
	UPROPERTY() TObjectPtr<USkeletalMesh> SuspectLook;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> ScuffleCloud;

	UPROPERTY(Replicated) TObjectPtr<AFTOIncident> Incident;
	UPROPERTY(ReplicatedUsing=OnRep_Look) bool bCriminal = true;
	UPROPERTY(ReplicatedUsing=OnRep_Armed) bool bArmed = false;
	/** Gun up at an officer right now. */
	UPROPERTY(Replicated) bool bShooting = false;
	UPROPERTY(Replicated) float AimPitch = 0.f;

	UPROPERTY(Replicated) EFTOPerpArrest ArrestState = EFTOPerpArrest::None;
	UPROPERTY(Replicated) TObjectPtr<AFTOCharacter> Arrester;
	UPROPERTY(Replicated) float StruggleMeter = 0.f;
	/** Server world time the cuffing started (a moment with hands on head, then the cuffs). */
	UPROPERTY(Replicated) float CuffStartTime = 0.f;

	FVector Home = FVector::ZeroVector;
	float HomeYaw = 0.f;
	TWeakObjectPtr<AActor> Target;
	float NextShotTime = 0.f;
	float ShootCheckAccumulator = 0.f;

	EFTOArrestResponse ForcedResponse = EFTOArrestResponse::Roll;
	/** Put on the floor by the police: give up once back up. */
	bool bGiveUpWhenUp = false;
	TWeakObjectPtr<AController> SubduedBy;
	FTimerHandle CuffTimer;
	float ArrestCheckAccumulator = 0.f;
	float StruggleEndTime = 0.f;
	float FleeStartTime = 0.f;
	float FarFromOfficersTime = 0.f;
	float AloneTime = 0.f;
	float ScuffleSpin = 0.f;
	/** Fleeing: the way out of the building first, then round the sidewalk corners (block X, Y and corner 0-3). */
	TArray<FVector> FleeExit;
	/** Where we're running to right now (a whistle stops us short of it). */
	FVector FleeTarget = FVector::ZeroVector;
	bool bOnSidewalks = false;
	FIntVector FleeCorner = FIntVector::ZeroValue;
	FIntVector PrevFleeCorner = FIntVector(-1, -1, -1);
};
