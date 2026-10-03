#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/FTOInteractable.h"
#include "Interaction/FTOTalkable.h"
#include "FTOPrecinctBoard.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM()
enum class EFTOBoardKind : uint8
{
	Locker,		// outfits for the officer, liveries for the fleet
	Upgrades	// precinct upgrades bought with career points
};

/**
 * A noticeboard on the precinct wall (FTOCareer): E brings up its options on the conversation panel. The locker board
 * changes the officer's outfit and the fleet's livery (whatever the precinct's rank has unlocked); the upgrades board
 * spends career points on precinct upgrades. Server-driven; the career it shows is the game state's (replicated).
 */
UCLASS(NotPlaceable)
class FTO_API AFTOPrecinctBoard : public AActor, public IFTOInteractable, public IFTOTalkable
{
	GENERATED_BODY()

public:
	AFTOPrecinctBoard();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: which board this is. */
	void SetKind(EFTOBoardKind InKind);
	EFTOBoardKind GetKind() const { return Kind; }

	// IFTOInteractable
	virtual bool CanInteract(const AFTOCharacter* Officer) const override { return Officer != nullptr; }
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual void Interact(AFTOCharacter* Officer) override;
	virtual FVector GetInteractLocation() const override;
	virtual float GetInteractRange() const override { return 260.f; }

	// IFTOTalkable
	virtual FText GetTalkTitle() const override;
	virtual void GetTalkOptions(const AFTOCharacter* Officer, TArray<FText>& OutOptions) const override;
	virtual bool TalkChoice(AFTOCharacter* Officer, int32 Index) override;

protected:
	UFUNCTION() void OnRep_Kind();
	/** The upgrades on offer now: the cheapest three not yet bought. */
	TArray<FName> OnOffer() const;

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Board;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UTextRenderComponent> Sign;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UBoxComponent> Focus;

	UPROPERTY(ReplicatedUsing=OnRep_Kind) EFTOBoardKind Kind = EFTOBoardKind::Locker;
};
