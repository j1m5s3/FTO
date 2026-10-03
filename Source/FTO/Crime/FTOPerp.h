#pragma once

#include "CoreMinimal.h"
#include "City/FTOPedestrian.h"
#include "FTOPerp.generated.h"

class AFTOIncident;
class AFTOGraffitiTag;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;

/** Where an arrest stands, from the perp's side. */
UENUM()
enum class EFTOPerpArrest : uint8
{
	None,			// up to no good (or hands up, or shooting)
	Surrendered,	// on their knees, hands on head, waiting for the cuffs
	Struggling,		// fighting off an officer, who mashes Interact to win
	Fleeing,		// legging it on foot
	Cuffing,		// kneeling while an officer puts the cuffs on
	Hiding,			// got away: strolling about like anyone else, till someone recognises them
	Fighting		// fists up, trading blows with an officer till one of them goes down
};

/** How a suspect takes an officer's attempt to arrest them. */
UENUM()
enum class EFTOArrestResponse : uint8
{
	Roll,		// decide on the spot (the tests can force the others)
	Comply,
	Struggle,
	Bolt,
	Fight
};

/**
 * Whoever is at the heart of an incident: the robber at the counter, the brawler, the burglar in the racks, the
 * neighbour with the bagpipes (or, on calls without a crook, the citizen who needs a hand). Stands at the incident
 * doing the deed, and puts their hands up once the police arrive.
 *
 * Armed perps (hold-ups, stand-offs, the "Armed" twist) carry a pistol and shoot at officers who come close, until
 * they're outnumbered or talked down.
 *
 * Crooks don't just stand there: they get on with it (a tagger sprays the wall, a vandal goes from bin to bin, a
 * shoplifter works along the shelves filling a sack), and some are done before the police arrive and walk off with
 * the goods. Others make a run for it when the police turn up. One who gets away blends into the crowd (Hiding):
 * the incident becomes a search by description, and an officer who talks to the right person has them.
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
	/** Server: an officer laid a hand on us (a punch, a kick): fight back, run, or give in. */
	void Provoked(AFTOCharacter* Officer);
	bool IsFighting() const { return ArrestState == EFTOPerpArrest::Fighting; }
	EFTOPerpArrest GetArrestState() const { return ArrestState; }
	bool IsFleeing() const { return ArrestState == EFTOPerpArrest::Fleeing; }
	bool IsHiding() const { return ArrestState == EFTOPerpArrest::Hiding; }
	/** Walking off with a sack of somebody else's things. */
	bool HasLoot() const { return bHasLoot; }
	/** What a witness would tell dispatch about us ("striped jumper, carrying a sack"). */
	FString DescribeSuspect() const;
	/** Tests: stay right here (no wandering off to the next shelf or bin, no leaving with the goods). */
	void StayPut() { DeedStops.Reset(); DeedEndTime = 0.f; }
	/** Tests: finish the crime now (a crook with somewhere to be leaves with the goods). */
	void FinishDeedNow();
	/** Server: dressed as the citizen they were a moment ago (found with contraband on a stop and search). */
	void WearLookOf(int32 Seed);

	// ---- The twists ----
	/** Server: a pickpocket blends into the crowd at At (lying low from the start, idling like everyone else). */
	void JoinCrowd(const FVector& At, float Yaw);
	/** A burglar still hiding somewhere in the building. */
	bool IsHidingInBuilding() const;
	/** Hiding upstairs rather than on the ground floor (server). */
	bool IsHidingUpstairs() const { return bHidingUpstairs; }
	/** Server: an officer's laid eyes on the hidden burglar: they give up, fight or run. */
	void Found(AFTOCharacter* Officer);
	/** A drunk to be talked round (the "Drunk" call). */
	bool IsDrunkCall() const;
	/** How far the officers have talked the drunk round (of DrunkStages), and how worked up they are. */
	int32 GetDrunkStage() const { return DrunkStage; }
	int32 GetDrunkTemper() const { return DrunkTemper; }
	/** Which of the options on the talk panel is the friendly one right now (tests). */
	int32 GetDrunkRightAnswer() const;
	static constexpr int32 DrunkStages = 3;
	virtual FText GetTalkTitle() const override;
	virtual void GetTalkOptions(const AFTOCharacter* Officer, TArray<FText>& OutOptions) const override;

	// IFTOTalkable, lying low: they'll talk (nervously), and a search turns up the goods.
	virtual bool TalkChoice(AFTOCharacter* Officer, int32 Index) override;
	/** Wrestling with an officer or being cuffed: the scene's on hold meanwhile. */
	bool IsInArrest() const { return ArrestState == EFTOPerpArrest::Struggling || ArrestState == EFTOPerpArrest::Cuffing; }
	/** 0-1: how close the officers are to winning a struggle. */
	float GetStruggleMeter() const { return StruggleMeter; }
	/** 0-1 through the cuffing (every machine). */
	float GetCuffProgress() const;
	/** The longest an arrest can hold an officer in place: the wrestle at its longest, then the cuffs. */
	float GetLongestArrestSeconds() const { return StruggleMaxSeconds + CuffSeconds; }
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
	/** A suspect who's going to run (the incident's EscapeChance) bolts as soon as an officer in sight gets this close. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float SpookDistance = 1300.f;

	/** Walking off from the scene, then strolling about while lying low. */
	UPROPERTY(EditDefaultsOnly, Category="Getaway") float LeaveSpeed = 230.f;
	UPROPERTY(EditDefaultsOnly, Category="Getaway") float HideSpeed = 150.f;
	/** Lying low, they might bolt if an officer in sight comes this close... */
	UPROPERTY(EditDefaultsOnly, Category="Getaway") float NervousDistance = 500.f;
	/** ...with this chance per second. */
	UPROPERTY(EditDefaultsOnly, Category="Getaway") float BoltChancePerSecond = 0.35f;

protected:
	virtual void ApplyLook() override;
	/** A crook in the suit (not one lying low: they've ditched it, unless they're hiding in a crowd of hot dogs). */
	virtual bool WantsHotDogSuit() const override;
	virtual bool ForcesHotDogSuit() const override;
	virtual void OnArrived() override;
	virtual void FaceOfficer(const AActor* Officer) override;
	virtual FString GetSmallTalk() override;
	virtual FString AnswerWhatTheySaw() override;
	virtual FString Contraband() override;
	virtual void ArrestForWhatWasFound(AFTOCharacter* Officer) override;

	/** Server: hide away in the building (upstairs if there's an upstairs): the squad has to find us. */
	void HideInBuilding();
	/** Somewhere in our building to hide (capsule centre), and whether it's upstairs. */
	bool FindHidingPlace(FVector& OutWhere, bool& bOutUpstairs) const;
	/** The drunk's lines this stage (an index into the table), and the order the officer's answers are shown in. */
	int32 DrunkLineFor(int32 Stage) const;
	void DrunkOptions(int32 Stage, int32 OutOrder[3]) const;
	/** Server: the officer said one of the answers on the panel (3: "You're under arrest"). */
	bool DrunkAnswer(AFTOCharacter* Officer, int32 Index);
	/** An officer on foot standing guard at our building's front door, if there is one (a burglar). */
	AFTOCharacter* DoorGuard(const AActor* Except = nullptr) const;
	/** Server: heading out of the door, straight into Guard: caught, and that's teamwork. */
	void CaughtAtTheDoor(AFTOCharacter* Guard);
	/** Server: the burglar's done (or nobody found them): out of the door with the goods, unless someone's guarding it. */
	void SlipOut();
	/** Server: talked round: off home, and that's the call handled. */
	void CalmDown(AFTOCharacter* Officer);

	/** Server: armed and cornered: pick an officer, turn to them, and fire now and then. */
	void TickShooting(float DeltaSeconds);
	bool IsStillFighting() const;
	AActor* FindTarget() const;
	void Shoot();

	/** Server: getting on with the crime (walking the shelves, going from bin to bin, spraying the wall). */
	void TickDeed(float DeltaSeconds);
	/** Server: set the crime up (where they'll go, the wall to tag) once we know what it is. */
	void BeginDeed();
	/** Server: the next stop in a crime that moves about (a shelf, a bin to kick, the far kerb). */
	void NextDeedStop();
	/** A breakable bit of street furniture near Around, still standing: its component, instance and where it is. */
	bool FindSomethingToSmash(const FVector& Around, UInstancedStaticMeshComponent*& OutISM, int32& OutInstance, FVector& OutWhere) const;
	/** Server: slip off into the crowd: walking from the scene (bSeen: running, and the police saw us go). */
	void GoIntoHiding(bool bSeen);
	/** Server: lying low: the next leg round the sidewalks. */
	void ContinueHiding();

	/** Server: the arrest side of things (struggles, chases, waiting for the cuffs). */
	void TickArrest(float DeltaSeconds);
	/** Server: squared up to an officer, fists up. */
	void BeginFighting(AFTOCharacter* Officer);
	/** Server: closing in and swinging, till someone's on the floor. */
	void TickFighting(float DeltaSeconds);
	/** Server: a brawl (bar fight, street brawl) before the police arrive: trading blows with the other brawlers. */
	void TickBrawl(float DeltaSeconds);
	/** Server: who we're fighting, when the next swing comes, and when the fight started. */
	TWeakObjectPtr<AFTOCharacter> FightTarget;
	float NextSwing = 0.f;
	float FightStartTime = 0.f;
	/** Server: the chase in a fight: where we were last sent, when next to look, and when they were last in reach. */
	FVector ChaseGoal = FVector::ZeroVector;
	float NextChase = 0.f;
	float LastReachable = 0.f;
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
	/** A clear line from our eyes to them. */
	bool CanSee(const AActor* Other) const;
	AFTOCityGenerator* FindCity();
	/** Cosmetic, every machine: a cartoon dust cloud around a struggle. */
	void UpdateScuffleCloud(float DeltaSeconds);

	UFUNCTION() void OnRep_Armed();
	UFUNCTION() void OnRep_Loot();

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Gun;
	/** The swag bag. */
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Loot;
	UPROPERTY() TObjectPtr<USkeletalMesh> SuspectLook;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> ScuffleCloud;

	UPROPERTY(Replicated) TObjectPtr<AFTOIncident> Incident;
	UPROPERTY(ReplicatedUsing=OnRep_Look) bool bCriminal = true;
	UPROPERTY(ReplicatedUsing=OnRep_Armed) bool bArmed = false;
	/** Dressed like anyone else (the incident's bStreetClothes), or ditched the striped jumper to lie low. */
	UPROPERTY(ReplicatedUsing=OnRep_Look) bool bStreetClothes = false;
	UPROPERTY(ReplicatedUsing=OnRep_Look) bool bDisguised = false;
	UPROPERTY(ReplicatedUsing=OnRep_Loot) bool bHasLoot = false;
	/** Gun up at an officer right now. */
	UPROPERTY(Replicated) bool bShooting = false;
	UPROPERTY(Replicated) float AimPitch = 0.f;

	UPROPERTY(Replicated) EFTOPerpArrest ArrestState = EFTOPerpArrest::None;
	UPROPERTY(Replicated) TObjectPtr<AFTOCharacter> Arrester;
	UPROPERTY(Replicated) float StruggleMeter = 0.f;
	/** Server world time the cuffing started (a moment with hands on head, then the cuffs). */
	UPROPERTY(Replicated) float CuffStartTime = 0.f;
	/** A drunk: answers they've liked, and ones they haven't. */
	UPROPERTY(Replicated) uint8 DrunkStage = 0;
	UPROPERTY(Replicated) uint8 DrunkTemper = 0;

	// Server: the twists.
	/** A pickpocket among the crowd (Hiding from the start, but staying put), and when they next shuffle about. */
	bool bInCrowd = false;
	float NextShuffle = 0.f;
	bool bHidingUpstairs = false;
	/** Found upstairs: there's nowhere to run (they give up instead). */
	bool bCornered = false;

	FVector Home = FVector::ZeroVector;
	float HomeYaw = 0.f;

	// Server: the crime in progress.
	/** Roll at setup: will they run when the police come? */
	bool bWillRun = false;
	/** Server time they're done and leave with the goods (0 = they're not going anywhere). */
	float DeedEndTime = 0.f;
	/** Stops for a crime that moves about, and which one's next. */
	TArray<FVector> DeedStops;
	int32 DeedStop = 0;
	/** Paused at a stop (or walking to the next) until then. */
	float DeedPauseUntil = 0.f;
	bool bDeedWalking = false;
	/** The vandal's next target, and when they hit it. */
	TWeakObjectPtr<UInstancedStaticMeshComponent> SmashISM;
	int32 SmashInstance = INDEX_NONE;
	FVector SmashAt = FVector::ZeroVector;
	float SmashTime = 0.f;
	TWeakObjectPtr<AFTOGraffitiTag> Tag;
	float DeedCheckAccumulator = 0.f;
	float DeedStartTime = 0.f;
	/** Lying low: strolling the block like a pedestrian (after getting clear of the scene). */
	bool bWandering = false;
	/** How fast the current getaway leg goes. */
	float RunSpeed = 620.f;
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
