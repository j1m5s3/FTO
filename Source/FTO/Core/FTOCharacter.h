#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Animation/FTOAnimatedActor.h"
#include "Vehicles/FTOVehicleSeats.h"
#include "Interaction/FTOInteractable.h"
#include "Weapons/FTOWeapons.h"
#include "Combat/FTOFighting.h"
#include "FTOCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UFTOKnockdownComponent;
struct FInputActionValue;

/** A move an officer is locked into with someone else (cuffing a suspect, wrestling one): where, which way, what. */
USTRUCT()
struct FFTOSyncedAction
{
	GENERATED_BODY()

	/** None when there's no move on. */
	UPROPERTY() EFTOAnimAction Action = EFTOAnimAction::None;
	/** Where the officer stands for it (capsule centre) and which way they face. */
	UPROPERTY() FVector_NetQuantize10 Location = FVector::ZeroVector;
	UPROPERTY() float Yaw = 0.f;
	/** Who with. */
	UPROPERTY() TObjectPtr<AActor> Partner;
};

/**
 * A player officer: the in-house Blender-built cop (Tools/Blender/build_officer.py) with its
 * uniform tinted in the player's badge colour. Falls back to a "bean cop" made of engine
 * primitives if the art hasn't been imported.
 *
 * Carries up to three weapons (a taser as standard issue; the precinct armory hands out the rest). Drawing one
 * brings it up to the shoulder with an over-the-shoulder camera; every round is flown by UFTOBallistics. Shot
 * down, an officer stays down until a partner helps them up (they're interactable while they're down).
 *
 * Arrests are two-person moves (see AFTOPerp): the officer steps in behind a kneeling suspect and cuffs them, or
 * squares up to one who fights back and wrestles them down (mashing Interact).
 */
UCLASS()
class FTO_API AFTOCharacter : public ACharacter, public IFTOAnimatedActor, public IFTOInteractable
{
	GENERATED_BODY()

public:
	AFTOCharacter();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void OnRep_PlayerState() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void Tick(float DeltaSeconds) override;

	/** Server: sit in a vehicle seat, visibly, riding along until ExitVehicle. */
	void EnterVehicle(AActor* Vehicle, EFTOSeat Seat);

	/** Server: climb out at the given spot. */
	void ExitVehicle(const FVector& Location, float Yaw);

	UFUNCTION(BlueprintPure, Category="FTO")
	AActor* GetCurrentVehicle() const { return CurrentVehicle; }

	EFTOSeat GetCurrentSeat() const { return CurrentSeat; }

	/** Hides this officer's head on this machine only, so a seat-view camera isn't inside it. */
	void SetHeadHidden(bool bHide);

	/** Local player riding along: get out (what E does in a seat). */
	void LeaveVehicle() { ServerLeaveVehicle(); }

	/** Local player riding along: chase camera or the view from the seat (what C does). */
	void ToggleSeatView() { ToggleCamera(); }

	UFTOKnockdownComponent* GetKnockdown() const { return Knockdown; }

	/** Blow the police whistle (what Q does on foot). */
	void BlowWhistle() { ServerWhistle(); }

	/** Flying tackle (what F does on foot): dive forward and bowl over whoever's in the way. Close to someone and not
	 *  running, F grabs hold of them instead and throws them. */
	void TacklePressed();

	// ---- Hand to hand (Combat/FTOFighting) ----
	/** A punch (what Fire does with no weapon up): jab, cross, hook, uppercut, one after another. */
	void PunchPressed();
	/** A kick (G): a front kick, or a roundhouse to finish a run of punches. */
	void KickPressed();
	/** On foot, empty-handed, on their feet and not mid-move. */
	bool CanFight() const;
	/** Server: fists up for a while (after throwing or taking a punch). */
	void EnterFightStance(float Seconds = 3.f);

	/** Local player: what E does (use whatever's in focus; heave in a struggle). */
	void PressInteract() { InteractPressed(); }

	/** Server: play a full-body action for a while (ticket writing, chatting). */
	void PlayTimedAction(EFTOAnimAction Action, float Duration);

	// ---- Conversations (IFTOTalkable) ----
	/** Server: start talking to Who (someone IFTOTalkable), ending any other conversation. */
	void BeginTalk(AActor* Who);
	/** Server: that's the end of the conversation (the other side hears TalkEnded). */
	void EndTalk();
	/** Who this officer's talking to, if anyone (the owner's machine and the server). */
	AActor* GetTalkingTo() const { return TalkingTo; }
	/** Local player: say option Index (what 1-4 do while talking; also for the smoke test). */
	void TalkPressed(int32 Index);
	/** How far an officer can wander off before a conversation's over. */
	static constexpr float TalkRange = 400.f;

	// IFTOAnimatedActor
	virtual EFTOAnimAction GetAnimAction() const override;
	virtual bool IsAnimAirborne() const override;
	virtual float GetAnimSpeed() const override;
	virtual EFTOAimPose GetAimPose() const override;
	virtual float GetAimPitch() const override;
	/** Where this officer is aiming. */
	FRotator GetAimRotation() const;

	// IFTOInteractable: a downed partner can be helped up.
	virtual bool CanInteract(const AFTOCharacter* Officer) const override;
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual void Interact(AFTOCharacter* Officer) override;
	virtual FVector GetInteractLocation() const override;

	// ---- Weapons ----
	/** Server: the armory hands over a weapon: into a free slot (or swapped for the one in hand), or a restock if they already carry one. */
	void GiveWeapon(EFTOWeapon Weapon);
	bool HasWeapon(EFTOWeapon Weapon) const { return Loadout.Contains(Weapon); }
	bool HasFreeSlot() const { return Loadout.Contains(EFTOWeapon::None); }
	EFTOWeapon GetWeaponInSlot(int32 Slot) const { return Loadout.IsValidIndex(Slot) ? Loadout[Slot] : EFTOWeapon::None; }
	int32 GetDrawnSlot() const { return DrawnSlot; }
	EFTOWeapon GetDrawnWeapon() const { return GetWeaponInSlot(DrawnSlot); }
	int32 GetClip(int32 Slot) const { return Clips.IsValidIndex(Slot) ? Clips[Slot] : 0; }
	int32 GetSpare(int32 Slot) const { return Spares.IsValidIndex(Slot) ? Spares[Slot] : 0; }
	bool IsReloading() const;

	/** Local player: draw slot Slot (what 1, 2, 3 do; again puts it away), or put the weapon away with INDEX_NONE. */
	void SelectSlot(int32 Slot);
	/** Local player: fire the weapon in hand at the crosshair (what the left mouse button does). */
	void FirePressed();

	/** Server: flattened by gunfire: down until a partner helps them up, or Seconds pass. False if already down. */
	bool GoDown(const FVector& Launch, float Seconds);
	bool IsDowned() const { return bDowned; }

	// ---- Two-person moves (cuffing a suspect, wrestling one) ----
	/**
	 * Server: lock this officer into a move with Partner: they step onto the spot (Feet, on the floor) facing Yaw,
	 * eased over a moment on every machine, and play Action, unable to move or draw, until EndSyncedAction.
	 */
	void BeginSyncedAction(EFTOAnimAction Action, const FVector& Feet, float Yaw, AActor* Partner);
	void EndSyncedAction();
	bool IsInSyncedAction() const { return SyncedAction.Action != EFTOAnimAction::None; }
	EFTOAnimAction GetSyncedAction() const { return SyncedAction.Action; }
	AActor* GetSyncedPartner() const { return SyncedAction.Partner; }
	/** On their feet and free to act (not knocked down, seeing stars, in a car or in the middle of a move). */
	bool IsReadyForAction() const;

	/** The interactable the local officer would use if they pressed Interact now. */
	AActor* GetFocusedInteractable() const { return FocusedInteractable.Get(); }

	/** Re-tints the uniform from the owning player state's badge colour. */
	void RefreshOfficerColor();

	UFUNCTION(BlueprintPure, Category="FTO")
	bool IsSprinting() const { return bSprinting; }

	UPROPERTY(EditDefaultsOnly, Category="FTO|Movement")
	float WalkSpeed = 500.f;

	UPROPERTY(EditDefaultsOnly, Category="FTO|Movement")
	float SprintSpeed = 850.f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** Ragdoll when bowled over, tackled or knocked out. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UFTOKnockdownComponent> Knockdown;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<class UFTOFootsteps> Footsteps;

	void HandleKnockedDown();
	void HandleRecovered();

	/** Placeholder body pieces. */
	UPROPERTY(VisibleAnywhere, Category="Components|Placeholder")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category="Components|Placeholder")
	TObjectPtr<UStaticMeshComponent> HeadMesh;

	UPROPERTY(VisibleAnywhere, Category="Components|Placeholder")
	TObjectPtr<UStaticMeshComponent> CapMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;
	/** The two officer models: every other badge number is a woman. */
	UPROPERTY() TObjectPtr<USkeletalMesh> OfficerModel;
	UPROPERTY() TObjectPtr<USkeletalMesh> OfficerModelF;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> UniformMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HeadMaterial;

	/** The vehicle this officer is riding in, if any, and where they sit. */
	UPROPERTY(ReplicatedUsing=OnRep_CurrentVehicle)
	TObjectPtr<AActor> CurrentVehicle;

	UPROPERTY(ReplicatedUsing=OnRep_CurrentVehicle)
	EFTOSeat CurrentSeat = EFTOSeat::None;

	UFUNCTION()
	void OnRep_CurrentVehicle();

	void ApplyVehicleState();

	/** Chase camera, or the view from the seat (C) while riding along. */
	void ApplyCameraMode();
	void ToggleCamera();

	/** The server moved us out of a car: land there now rather than wait for a movement correction. */
	UFUNCTION(Client, Reliable)
	void ClientExitedVehicle(FVector_NetQuantize Location, float Yaw);

	/** Riding shotgun: E gets out, Q works the lights. */
	UFUNCTION(Server, Reliable)
	void ServerLeaveVehicle();

	UFUNCTION(Server, Reliable)
	void ServerToggleVehicleSiren();

	/** A passenger's view turns with the car. */
	float LastVehicleYaw = 0.f;
	bool bTrackVehicleYaw = false;

	/** Whether this machine has us set up in a seat (attached, movement and collision off). */
	bool bSeated = false;

	/** Short replicated full-body action (e.g. writing a ticket) everyone should see. */
	UPROPERTY(Replicated)
	EFTOAnimAction TimedAction = EFTOAnimAction::None;

	/** Server world time the timed action ends. */
	UPROPERTY(Replicated)
	float TimedActionEnd = 0.f;

	UPROPERTY(ReplicatedUsing=OnRep_Sprinting)
	bool bSprinting = false;

	UFUNCTION()
	void OnRep_Sprinting();

	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bNewSprinting);

	void ApplySprint();

	// Input handlers
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void SprintStarted();
	void SprintStopped();

	/** Picks the nearest usable interactable in range (local player only). */
	void UpdateFocus();
	/** Is there a wall (or window, or cell bars) between From and To? Furniture doesn't count. */
	bool IsWallBetween(const FVector& From, const FVector& To, const AActor* Target) const;

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target);

	UFUNCTION(Server, Reliable)
	void ServerTalkChoice(int32 Index);
	UFUNCTION(Server, Reliable)
	void ServerEndTalk();
	UFUNCTION() void OnRep_TalkingTo();
	UPROPERTY(ReplicatedUsing=OnRep_TalkingTo) TObjectPtr<AActor> TalkingTo;
	/** Server: when the conversation was last moving (it ends if it sits idle). */
	float TalkIdleSince = 0.f;
	/** Local: the conversation keys are live (their mapping context is on). */
	bool bTalkKeys = false;
	void SetTalkKeys(bool bWant);

	TWeakObjectPtr<AActor> FocusedInteractable;
	float FocusAccumulator = 0.f;

	/** Interact is routed to gameplay systems in later features. */
	virtual void InteractPressed();
	virtual void InteractReleased();
	virtual void WhistlePressed();

	UFUNCTION(Server, Unreliable)
	void ServerWhistle();
	float NextWhistleTime = 0.f;

	/** Can this officer dive right now (on foot, on their feet, not mid-air, not too soon after the last)? */
	bool CanTackle() const;
	/** The dive itself: launch forward (the owner predicts it, the server does it for real). */
	void LaunchTackle();
	UFUNCTION(Server, Reliable)
	void ServerTackle();
	UFUNCTION(Server, Reliable)
	void ServerFight(EFTOMove Move);
	/** Server: whoever we've got hold of goes over. */
	void ThrowGrabbed();
	/** Server: punches strung together (and when the last one went). */
	int32 FightCombo = 0;
	float LastSwingTime = -100.f;
	TWeakObjectPtr<AActor> Grabbed;
	FTimerHandle ThrowTimer;
	/** Server world time the fists come down again. */
	UPROPERTY(Replicated) float FightStanceUntil = 0.f;
	/** Server: during the dive, look for someone to land on. */
	void CheckTackle();
	FTimerHandle TackleTimer;
	float TackleReachUntil = 0.f;
	float NextTackleTime = 0.f;

	/** How far a whistle carries (citizens stop, nearby crimes get called in). */
	UPROPERTY(EditDefaultsOnly, Category="FTO")
	float WhistleRadius = 1800.f;

	/** Seconds between tackles. */
	UPROPERTY(EditDefaultsOnly, Category="FTO")
	float TackleCooldown = 1.2f;

	// ---- Two-person moves ----
	UPROPERTY(ReplicatedUsing=OnRep_SyncedAction)
	FFTOSyncedAction SyncedAction;

	UFUNCTION()
	void OnRep_SyncedAction();
	/** Every machine: stop and start easing onto the spot for a move, or let go once it's over. */
	void ApplySyncedAction();
	/** Server and owner: ease onto the move's spot (everyone else sees it through replicated movement). */
	void TickSyncedAction();

	/** Mashing Interact in a struggle goes straight to the suspect we're wrestling. */
	UFUNCTION(Server, Reliable)
	void ServerMash();

	/** Whether this machine has us locked into a move, and the ease onto its spot: from here, starting then. */
	bool bInSyncedAction = false;
	FVector SyncFrom = FVector::ZeroVector;
	FQuat SyncFromRotation = FQuat::Identity;
	float SyncStartTime = -1.f;
	static constexpr float SyncEaseSeconds = 0.3f;

	// ---- Weapons ----
	/** Where the weapon goes: in hand along the aim when drawn, else on the hip (sidearms) or slung on the back. */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	/** What's in each of the three slots (None = empty). */
	UPROPERTY(ReplicatedUsing=OnRep_Loadout)
	TArray<EFTOWeapon> Loadout;

	/** Rounds in the magazine, and spare, per slot (only the owner needs to know). */
	UPROPERTY(Replicated)
	TArray<int32> Clips;
	UPROPERTY(Replicated)
	TArray<int32> Spares;

	/** The slot in hand, or INDEX_NONE with everything put away. */
	UPROPERTY(ReplicatedUsing=OnRep_Loadout)
	int32 DrawnSlot = INDEX_NONE;

	/** The last slot drawn (what the right mouse button brings back out). */
	int32 LastDrawnSlot = 0;

	/** Server world time a reload finishes (0 when not reloading). */
	UPROPERTY(Replicated)
	float ReloadEnd = 0.f;

	/** Shot down and waiting for a partner. */
	UPROPERTY(Replicated)
	bool bDowned = false;

	UFUNCTION()
	void OnRep_Loadout();

	/** Movement and rotation for the weapon being up or away (every machine). */
	void ApplyWeaponStance();
	/** Puts WeaponMesh where it belongs this frame (every machine). */
	void UpdateWeaponMesh();
	/** Local player: ease the camera over the shoulder while a weapon is up. */
	void UpdateAimCamera(float DeltaSeconds);
	/** Local player: what the crosshair is on (a point up to 100 m away). */
	FVector GetCrosshairTarget() const;

	void DrawPressed();
	void ReloadPressed();
	void NextWeaponPressed();
	void PrevWeaponPressed();
	void CycleWeapon(int32 Step);

	UFUNCTION(Server, Reliable)
	void ServerSelectSlot(int32 Slot);
	/** The server turned down a weapon swap the owner already made on their screen: put it back. */
	UFUNCTION(Client, Reliable)
	void ClientSetDrawnSlot(int32 Slot);
	UFUNCTION(Server, Reliable)
	void ServerFire(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Aim, int32 Seed);
	UFUNCTION(Server, Reliable)
	void ServerReload();
	void FinishReload();

	FTimerHandle ReloadTimer;
	float NextShotTime = 0.f;
	float NextServerShotTime = 0.f;
	float AimBlend = 0.f;

	UPROPERTY(EditDefaultsOnly, Category="FTO|Movement")
	float DrawnWalkSpeed = 380.f;
};
