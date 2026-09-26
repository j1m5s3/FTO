#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Animation/FTOAnimatedActor.h"
#include "Vehicles/FTOVehicleSeats.h"
#include "FTOArrestee.generated.h"

class AFTOCharacter;
class AFTOCityGenerator;
class UCapsuleComponent;
class USkeletalMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EFTOArresteeState : uint8
{
	Escorted,	// cuffed, trotting after the arresting officer
	InCruiser,	// in the back seat
	Booked,		// walked into a holding cell, sat sulking on the bench
	Escaped
};

/**
 * A cuffed suspect. Follows in the arresting officer's footsteps (so through doorways, not walls); rides in
 * the back of their cruiser (visibly, behind the cage); walk them into the precinct's holding cells to book
 * them for bonus chaos relief, and they let themselves into a cell and sit down. Left alone too long, they
 * wander off. Server-driven; clients interpolate a replicated position.
 */
UCLASS()
class FTO_API AFTOArrestee : public AActor, public IFTOAnimatedActor
{
	GENERATED_BODY()

public:
	AFTOArrestee();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: cuff and start following Officer. Relief is paid out on booking. */
	void Init(AFTOCharacter* Officer, float InBookingRelief, const FText& InCrime);

	UFUNCTION(BlueprintPure, Category="Arrest") AFTOCharacter* GetEscort() const { return Escort; }
	UFUNCTION(BlueprintPure, Category="Arrest") EFTOArresteeState GetArrestState() const { return State; }
	FText GetCrime() const { return Crime; }
	/** Sat in a cell (booked and settled). */
	bool IsJailed() const { return bJailed; }

	/** The vehicle and back seat this suspect is sat in, if any. */
	AActor* GetRideVehicle() const { return RideVehicle; }
	EFTOSeat GetRideSeat() const { return RideSeat; }

	// IFTOAnimatedActor: trot along, hands up whenever we stop (it's a fair cop), sulk in the back seat and the cell.
	virtual EFTOAnimAction GetAnimAction() const override;
	virtual float GetAnimSpeed() const override { return AnimSpeed; }

	/** Within this distance of the holding cells (inside the precinct) a suspect gets booked. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float CellRadius = 450.f;
	/** Seconds without their escort nearby before they make a run for it. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float EscapeAfter = 45.f;
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float FollowDistance = 150.f;
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float MaxSpeed = 700.f;
	/** How long a booked suspect sits in the cell before being taken off to court. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float CellTime = 120.f;

protected:
	void ServerTick(float DeltaSeconds);
	/** Server: step along the escort's trail, keeping FollowDistance behind them. */
	void FollowTrail(float DeltaSeconds);
	/** Server: walk the booked path into the cell, then sit. */
	void WalkIntoCell(float DeltaSeconds);
	bool IsAtHoldingCells() const;
	void Book();
	void Escape();
	void SetInCruiser(AActor* Cruiser);
	void LeaveCruiser();
	/** Sits in (or climbs out of) the back seat to match the replicated state, on every machine. */
	void ApplyRide();
	/** Where to stand, height-wise, when following Officer. */
	FVector FootstepOf(const AActor* Officer) const;

	UFUNCTION() void OnRep_State();

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UCapsuleComponent> Capsule;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UTextRenderComponent> Tag;

	UPROPERTY(Replicated) TObjectPtr<AFTOCharacter> Escort;
	UPROPERTY(ReplicatedUsing=OnRep_State) EFTOArresteeState State = EFTOArresteeState::Escorted;
	UPROPERTY(ReplicatedUsing=OnRep_State) TObjectPtr<AActor> RideVehicle;
	UPROPERTY(ReplicatedUsing=OnRep_State) EFTOSeat RideSeat = EFTOSeat::None;
	UPROPERTY(Replicated) FVector_NetQuantize10 NetLocation;
	UPROPERTY(Replicated) float NetYaw = 0.f;
	UPROPERTY(Replicated) float AnimSpeed = 0.f;
	UPROPERTY(Replicated) FText Crime;
	UPROPERTY(Replicated) bool bJailed = false;

	UPROPERTY(Transient) TObjectPtr<AFTOCityGenerator> City;

	float BookingRelief = 4.f;
	float AloneTime = 0.f;
	/** Server: where the escort has walked, oldest first. */
	TArray<FVector> Trail;
	/** Server: the way into our cell, then the bench. */
	TArray<FVector> CellPath;
	float CellYaw = 0.f;
	int32 CellIndex = INDEX_NONE;
};
