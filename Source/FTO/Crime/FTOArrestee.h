#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Animation/FTOAnimatedActor.h"
#include "FTOArrestee.generated.h"

class AFTOCharacter;
class UCapsuleComponent;
class USkeletalMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EFTOArresteeState : uint8
{
	Escorted,	// cuffed, trotting after the arresting officer
	InCruiser,	// in the back seat
	Booked,
	Escaped
};

/**
 * A cuffed suspect. Follows the arresting officer; ride along in their cruiser; book them at
 * the precinct for bonus chaos relief. Left alone too long, they wander off.
 * Server-driven; clients interpolate a replicated position.
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

	// IFTOAnimatedActor: trot along, hands up whenever we stop (it's a fair cop).
	virtual EFTOAnimAction GetAnimAction() const override;
	virtual float GetAnimSpeed() const override { return AnimSpeed; }

	/** Within this distance of the precinct steps a suspect gets booked. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float BookingRadius = 1800.f;
	/** Seconds without their escort nearby before they make a run for it. */
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float EscapeAfter = 45.f;
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float FollowDistance = 150.f;
	UPROPERTY(EditDefaultsOnly, Category="Arrest") float MaxSpeed = 700.f;

protected:
	void ServerTick(float DeltaSeconds);
	void Book();
	void Escape();
	void SetInCruiser(AActor* Cruiser);

	UFUNCTION() void OnRep_State();

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UCapsuleComponent> Capsule;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UTextRenderComponent> Tag;

	UPROPERTY(Replicated) TObjectPtr<AFTOCharacter> Escort;
	UPROPERTY(ReplicatedUsing=OnRep_State) EFTOArresteeState State = EFTOArresteeState::Escorted;
	UPROPERTY(Replicated) FVector_NetQuantize10 NetLocation;
	UPROPERTY(Replicated) float NetYaw = 0.f;
	UPROPERTY(Replicated) float AnimSpeed = 0.f;
	UPROPERTY(Replicated) FText Crime;

	float BookingRelief = 4.f;
	float AloneTime = 0.f;
	FVector PrecinctLocation = FVector::ZeroVector;
};
