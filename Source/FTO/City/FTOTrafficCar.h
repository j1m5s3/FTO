#pragma once

#include "CoreMinimal.h"
#include "City/FTOPathMover.h"
#include "Interaction/FTOInteractable.h"
#include "FTOTrafficCar.generated.h"

class AFTOCityGenerator;
class AFTOIncident;
class UBoxComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EFTOCarState : uint8
{
	Driving,
	PullingOver,
	Stopped,	// waiting for the officer to write the ticket
	WritingTicket,
	Fleeing,
	Busted		// caught at the end of a chase
};

UENUM(BlueprintType)
enum class EFTOCarViolation : uint8
{
	None,
	Speeding,
	BrokenTaillight,
	ExpiredTags,
	NoisyExhaust
};

/**
 * Ambient traffic driving the road grid. Cars with a violation show a "!" and
 * can be pulled over for a routine traffic stop; a few are wanted and bolt.
 */
UCLASS()
class FTO_API AFTOTrafficCar : public AFTOPathMover, public IFTOInteractable
{
	GENERATED_BODY()

public:
	AFTOTrafficCar();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: start driving from an intersection. */
	void StartDriving(AFTOCityGenerator* InCity, int32 InI, int32 InJ, int32 InSeed);

	UFUNCTION(BlueprintPure, Category="Traffic") EFTOCarState GetCarState() const { return CarState; }
	UFUNCTION(BlueprintPure, Category="Traffic") EFTOCarViolation GetViolation() const { return Violation; }

	/** Server: pull over if we have a violation (officer on foot or a siren behind us). */
	bool RequestPullOver();

	// IFTOInteractable
	virtual bool CanInteract(const AFTOCharacter* Officer) const override;
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual void Interact(AFTOCharacter* Officer) override;
	virtual FVector GetInteractLocation() const override { return GetActorLocation(); }
	virtual float GetInteractRange() const override;

	/** Height of the collision box centre above the road. */
	static constexpr float RideHeight = 95.f;

	UPROPERTY(EditDefaultsOnly, Category="Traffic") float CruiseSpeed = 900.f;
	UPROPERTY(EditDefaultsOnly, Category="Traffic") float TicketSeconds = 3.f;
	/** Chance a stopped car turns out to be wanted and flees. */
	UPROPERTY(EditDefaultsOnly, Category="Traffic") float WantedChance = 0.15f;
	UPROPERTY(EditDefaultsOnly, Category="Traffic") float TicketChaosRelief = 2.f;

protected:
	virtual void BeginPlay() override;
	virtual void OnArrived() override;

	void DriveToNextIntersection();
	FVector LanePoint(int32 I, int32 J, const FIntPoint& Heading) const;
	bool IsPathBlocked(bool bIncludeCars) const;
	void FinishTicket();
	void RollViolation();

	UFUNCTION() void OnRep_Look();
	UFUNCTION() void OnRep_CarState();
	void RefreshIndicator();

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UBoxComponent> Collision;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Body;
	/** Body styles to pick from; index 4 is the taxi, 5 the ice cream truck. */
	UPROPERTY() TArray<TObjectPtr<UStaticMesh>> BodyStyles;
	UPROPERTY(VisibleAnywhere, Category="Components") TArray<TObjectPtr<UStaticMeshComponent>> Wheels;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UTextRenderComponent> Indicator;

	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PaintMaterial;
	UPROPERTY(Transient) TObjectPtr<AFTOCityGenerator> City;

	UPROPERTY(ReplicatedUsing=OnRep_Look) int32 LookSeed = 0;
	UPROPERTY(ReplicatedUsing=OnRep_CarState) EFTOCarState CarState = EFTOCarState::Driving;
	UPROPERTY(ReplicatedUsing=OnRep_CarState) EFTOCarViolation Violation = EFTOCarViolation::None;

	FRandomStream Rng;
	FIntPoint Node = FIntPoint::ZeroValue;		// intersection we're heading to
	FIntPoint Heading = FIntPoint(1, 0);
	FVector PendingTarget = FVector::ZeroVector;
	float PendingSpeed = 0.f;
	bool bWaitingForClearRoad = false;
	float WaitStartTime = 0.f;
	float IgnoreCarsUntil = 0.f;
	/** Seconds a car will wait behind other cars before nudging through. */
	float MaxPatience = 3.f;
	float BlockCheckAccumulator = 0.f;
	float FleeUntil = 0.f;
	FTimerHandle TicketTimer;

	/** The chase this car is the target of, if it fled a stop. */
	UPROPERTY(Transient) TObjectPtr<AFTOIncident> ChaseIncident;

	void Bust();
};
