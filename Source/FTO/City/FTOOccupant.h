#pragma once

#include "CoreMinimal.h"
#include "City/FTOPedestrian.h"
#include "City/FTOCityKit.h"
#include "FTOOccupant.generated.h"

/** Who someone is to the building they're in. */
UENUM(BlueprintType)
enum class EFTOOccupantRole : uint8
{
	Staff,		// behind the counter, tending bar, at a desk, on the forklift
	Customer,	// browsing, eating, drinking, queueing
	Resident,	// at home
	Officer,	// precinct staff: the desk sergeant, the quartermaster
	Crook		// lying low; an officer can question them
};

/**
 * Somebody inside a building: the clerk, the bartender, a diner in a booth, a family at home, a crook lying low.
 * They keep to their spot (sat, working, chatting) with the odd shuffle, stop for a chat with officers, put their
 * hands up for a whistle, and react to trouble in their building (hands up in a hold-up, cheering on a brawl).
 * Spawned and cleared away by AFTOInteriorLife as officers come and go; server-driven, replicated like the crowd.
 */
UCLASS()
class FTO_API AFTOOccupant : public AFTOPedestrian
{
	GENERATED_BODY()

public:
	AFTOOccupant();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: take up Spot in building Index. Crooks only ever take standing spots. */
	void Settle(int32 Index, EFTOBuildingType Type, const FFTOSpot& InSpot, EFTOOccupantRole InRole, int32 InSeed);

	/** Server: trouble in the building: do Action (hands up, cower, cheer...) facing Toward. None = all clear. */
	void React(EFTOAnimAction Action, const FVector& Toward);
	/** Server: get stuck in: stand at Where (on the floor) facing Toward, doing Action (brawling, arguing). */
	void Confront(const FVector& Where, const FVector& Toward, EFTOAnimAction Action);

	/** Server: back to our spot and our business. */
	virtual void Resume() override;

	EFTOOccupantRole GetRole() const { return OccupantRole; }
	const FFTOSpot& GetSpot() const { return Spot; }

	// IFTOInteractable
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual void Interact(AFTOCharacter* Officer) override;
	virtual bool TalkChoice(AFTOCharacter* Officer, int32 Index) override;

	// IFTOAnimatedActor
	virtual EFTOAnimAction GetAnimAction() const override;

protected:
	virtual void OnArrived() override;
	virtual void ApplyLook() override;
	virtual void FaceOfficer(const AActor* Officer) override;
	virtual FString GetSmallTalk() override;
	virtual FString Contraband() override;
	virtual int32 GetBuildingForCrime() const override { return BuildingIndex; }

	/** Server: now and then, shuffle about near our spot (standing folk with nothing else going on). */
	void Fidget();
	void ScheduleFidget();
	/** Server: an officer asks a crook what they're up to. Sometimes the answer is a confession. */
	void Question(AFTOCharacter* Officer);
	FVector SpotLocation() const { return Spot.Transform.GetLocation() + FVector(0.f, 0.f, HalfHeight); }
	float SpotYaw() const { return Spot.Transform.Rotator().Yaw; }

	UPROPERTY(ReplicatedUsing=OnRep_Look) EFTOOccupantRole OccupantRole = EFTOOccupantRole::Customer;
	UPROPERTY(Replicated) EFTOBuildingType BuildingType = EFTOBuildingType::Shop;
	/** What they do at their spot (Sit, Work, Talk, None...). */
	UPROPERTY(Replicated) EFTOAnimAction PostAction = EFTOAnimAction::None;
	/** What trouble in the building has them doing instead. */
	UPROPERTY(Replicated) EFTOAnimAction Reaction = EFTOAnimAction::None;
	UPROPERTY(Replicated) bool bSeated = false;
	/** Crooks: already been questioned (and got away with it). */
	UPROPERTY(Replicated) bool bQuestioned = false;

	UPROPERTY() TObjectPtr<USkeletalMesh> OfficerLook;
	UPROPERTY() TObjectPtr<USkeletalMesh> OfficerLookF;
	UPROPERTY() TObjectPtr<USkeletalMesh> CrookLook;

	FFTOSpot Spot;
	int32 BuildingIndex = INDEX_NONE;
	bool bConfronting = false;
	/** Fidgeting: off our spot for a moment. */
	bool bWandered = false;
	FTimerHandle FidgetTimer;
};
