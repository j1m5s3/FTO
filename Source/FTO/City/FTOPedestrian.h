#pragma once

#include "CoreMinimal.h"
#include "City/FTOPathMover.h"
#include "Interaction/FTOInteractable.h"
#include "Animation/FTOAnimatedActor.h"
#include "Interaction/FTOTalkable.h"
#include "FTOPedestrian.generated.h"

class AFTOCityGenerator;
class UCapsuleComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UFTOKnockdownComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * An ambient citizen strolling the sidewalks. Officers can stop them for a word (IFTOTalkable): ask what they've
 * seen (a tip-off about trouble nobody's reported, or a sighting of a suspect on the run), pass the time of day, or
 * search them and arrest them. Some are carrying something they shouldn't (they're arrested for it); arresting
 * someone who's clean is a wrongful arrest, and the city holds it against the police (searching them costs a
 * little goodwill too).
 */
UCLASS()
class FTO_API AFTOPedestrian : public AFTOPathMover, public IFTOInteractable, public IFTOAnimatedActor, public IFTOTalkable
{
	GENERATED_BODY()

public:
	AFTOPedestrian();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: start walking the ring around this block. */
	void StartWandering(AFTOCityGenerator* InCity, int32 InBlockX, int32 InBlockY, int32 InCorner, int32 InSeed);

	/** Server: an officer blew a whistle: stop, hands up, look at them for a moment. */
	void FreezeFor(const AActor* Officer, float Seconds);

	/** Server: carry on with whatever we were doing (after a chat, a whistle, a tumble...). */
	virtual void Resume();

	// IFTOInteractable
	virtual bool CanInteract(const AFTOCharacter* Officer) const override;
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual void Interact(AFTOCharacter* Officer) override;
	virtual FVector GetInteractLocation() const override { return GetActorLocation(); }

	// IFTOAnimatedActor
	virtual EFTOAnimAction GetAnimAction() const override;
	virtual float GetAnimSpeed() const override { return GetCurrentSpeed(); }

	// IFTOTalkable
	virtual FText GetTalkTitle() const override;
	virtual void GetTalkOptions(const AFTOCharacter* Officer, TArray<FText>& OutOptions) const override;
	virtual bool TalkChoice(AFTOCharacter* Officer, int32 Index) override;
	virtual void TalkEnded(AFTOCharacter* Officer) override;

	/** Been searched (every machine), and what turned up (nothing, if empty). */
	bool WasSearched() const { return bSearched; }
	const FString& GetFound() const { return Found; }

	/** Chaos for searching someone who turns out to be clean, and for arresting them. */
	static constexpr float CleanSearchChaos = 0.5f;
	static constexpr float WrongfulArrestChaos = 4.f;

	virtual bool IsMovementFrozen() const override;
	UFTOKnockdownComponent* GetKnockdown() const { return Knockdown; }

	int32 GetLookSeed() const { return LookSeed; }

	/** What we look like, the way a witness would put it ("green top, bald with a beard"). */
	FString DescribeLook() const;
	/** The same, for someone dressed from Seed. */
	FString DescribeLookOf(int32 Seed) const;
	/** A look seed for someone who could be mistaken for Seed (the same outfit, or the same colour top) but isn't. */
	int32 LookAlikeSeed(int32 Seed, int32 Salt) const;

	/** Capsule half-height; the path runs this far above the sidewalk. */
	static constexpr float HalfHeight = 92.f;

protected:
	virtual void BeginPlay() override;
	virtual void OnArrived() override;

	void WalkToNextCorner();

	/** Server: turn to look at an officer who's talking to (or whistling at) us. */
	virtual void FaceOfficer(const AActor* Officer);
	/** A line for an officer who stops for a chat and has no tip-off coming. */
	virtual FString GetSmallTalk();

	// ---- Conversations ----
	/** Server: "Seen anything?": a sighting of a suspect on the run, a tip-off, or nothing. */
	virtual FString AnswerWhatTheySaw();
	/** Server: what a search turns up ("a stolen wallet"), or empty if they're clean. Rolled once. */
	virtual FString Contraband();
	/** Server: the officer's found something and is arresting them for it (a crime scene springs up right here, with
	 *  them as its perp, and the arrest goes from there). */
	virtual void ArrestForWhatWasFound(AFTOCharacter* Officer);
	/** Server: arrested for nothing: cuffed and walked to the cells, and the city's not happy about it. */
	void WrongfulArrest(AFTOCharacter* Officer);
	/** The building we're in (for a crime that turns up indoors), if any. */
	virtual int32 GetBuildingForCrime() const { return INDEX_NONE; }
	/** Server: stand still facing the officer while we talk. */
	void HoldForTalk(AFTOCharacter* Officer);
	/** Server: hands up for the pat-down, then back to talking. */
	void EndSearchPose();

	/** Searched, and what turned up (replicated for the conversation panel). */
	UPROPERTY(Replicated) bool bSearched = false;
	UPROPERTY(Replicated) FString Found;
	/** Talking to an officer right now (server). */
	TWeakObjectPtr<AFTOCharacter> TalkingWith;
	FTimerHandle SearchPoseTimer;

	UFUNCTION() void OnRep_Look();
	/** Dress the body from LookSeed (every machine). */
	virtual void ApplyLook();
	/** Tint every material slot of Body with Color. */
	void PaintBody(const FLinearColor& Color);

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UCapsuleComponent> Capsule;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UFTOKnockdownComponent> Knockdown;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<class UFTOFootsteps> Footsteps;

	void HandleRecovered();

	/** Every machine: dressed as a hot dog (the HotDogs mutator) if WantsHotDogSuit says so. */
	void UpdateHotDogSuit();
	/** Suspects (and a pickpocket's crowd) on hot dog day. */
	virtual bool WantsHotDogSuit() const { return false; }
	/** Dressed as a hot dog whatever the day (the "Costumed" twist). */
	virtual bool ForcesHotDogSuit() const { return false; }
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> HotDogSuit;

	/** Civilian variants to pick from (Tools/Blender/build_civilians.py). */
	UPROPERTY() TArray<TObjectPtr<USkeletalMesh>> Looks;

	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	/** Picks the variant and shirt colour; rolled on the server. */
	UPROPERTY(ReplicatedUsing=OnRep_Look) int32 LookSeed = 0;

	/** Stopped for a chat with an officer. */
	UPROPERTY(Replicated) bool bChatting = false;

	/** Startled by a whistle. */
	UPROPERTY(Replicated) bool bHandsUp = false;
	/** Turned round, hands up against an imaginary wall, being patted down. */
	UPROPERTY(Replicated) bool bBeingSearched = false;

	UPROPERTY(Transient) TObjectPtr<AFTOCityGenerator> City;

	FRandomStream Rng;
	int32 BlockX = 0;
	int32 BlockY = 0;
	int32 Corner = 0;
	int32 Direction = 1;
	float WalkSpeed = 150.f;
	float ChatCooldownUntil = 0.f;
	FTimerHandle ResumeTimer;
};
