#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/FTOInteractable.h"
#include "Interaction/FTOTalkable.h"
#include "FTOLift.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;

/**
 * One stop of a building's lift: a pair of steel doors in the wall (on the street outside for the ground floor,
 * inside on every floor above). E at the doors brings up the buttons (the conversation panel: up a floor, down a
 * floor, the top, the street); the doors close, the lift hums, and a moment later everyone standing at the doors
 * steps out of the doors on the floor they picked. Server-driven; the city generator spawns a set per building
 * (AFTOCityGenerator::SpawnGameplayMarkers), every machine sees the doors.
 */
UCLASS(NotPlaceable)
class FTO_API AFTOLift : public AActor, public IFTOInteractable, public IFTOTalkable
{
	GENERATED_BODY()

public:
	AFTOLift();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: link a building's stops, street level first. */
	static void LinkStops(const TArray<AFTOLift*>& Stops);

	int32 GetFloor() const { return Floor; }
	int32 GetNumFloors() const { return NumFloors; }
	/** Where someone stepping out of these doors stands (capsule centre). */
	FVector GetArrivalPoint() const;

	/** Server: take everyone at these doors to Target (a floor number), after the doors close. */
	void Ride(int32 Target);

	// IFTOInteractable
	virtual bool CanInteract(const AFTOCharacter* Officer) const override { return Officer != nullptr; }
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual void Interact(AFTOCharacter* Officer) override;
	virtual FVector GetInteractLocation() const override;
	virtual float GetInteractRange() const override { return 220.f; }

	// IFTOTalkable: the lift's buttons.
	virtual FText GetTalkTitle() const override;
	virtual void GetTalkOptions(const AFTOCharacter* Officer, TArray<FText>& OutOptions) const override;
	virtual bool TalkChoice(AFTOCharacter* Officer, int32 Index) override;

	/** Seconds from the doors closing to stepping out on the new floor. */
	UPROPERTY(EditDefaultsOnly, Category="Lift") float RideSeconds = 1.8f;

protected:
	virtual void BeginPlay() override;
	/** Server: the ride's over: everyone who was at the doors steps out upstairs (or down). */
	void Arrive();
	FText FloorName(int32 Which) const;

	UFUNCTION() void OnRep_Floor();

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USceneComponent> Root;
	/** What officers focus on to use the lift (queries only; it blocks nothing). */
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UBoxComponent> Focus;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Frame;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> DoorLeft;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> DoorRight;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Lamp;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UTextRenderComponent> Sign;

	UPROPERTY(ReplicatedUsing=OnRep_Floor) int32 Floor = 0;
	UPROPERTY(ReplicatedUsing=OnRep_Floor) int32 NumFloors = 1;
	/** Doors shut (a ride's under way from or to here). */
	UPROPERTY(Replicated) bool bClosed = false;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> LampMaterial;

	// Server
	TArray<TWeakObjectPtr<AFTOLift>> Stops;
	TArray<TWeakObjectPtr<AActor>> Riders;
	int32 RideTo = INDEX_NONE;
	FTimerHandle RideTimer;
	/** Every machine: how far open the doors are (0-1). */
	float Open = 0.f;
};
