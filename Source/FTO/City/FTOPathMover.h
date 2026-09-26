#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FTOPathMover.generated.h"

/**
 * One straight leg of movement. Replicated instead of transforms: clients evaluate
 * the same leg against server time, so ambient crowds move smoothly for almost no bandwidth.
 */
USTRUCT()
struct FFTOMoveSegment
{
	GENERATED_BODY()

	UPROPERTY() FVector_NetQuantize From = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize To = FVector::ZeroVector;
	UPROPERTY() float StartTime = 0.f;
	/** Units per second. 0 = standing still at From. */
	UPROPERTY() float Speed = 0.f;
};

/**
 * Base for kinematic ambient movers (pedestrians, traffic). The server feeds legs;
 * everyone evaluates them locally.
 */
UCLASS(Abstract)
class FTO_API AFTOPathMover : public AActor
{
	GENERATED_BODY()

public:
	AFTOPathMover();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: start a new leg from the current position. */
	void MoveTo(const FVector& Target, float Speed);
	/** Server: stand still where we are. */
	void Hold();

	/** Server: jump to a spot and stand there (e.g. after being knocked flying). */
	void TeleportAndHold(const FVector& Location);

	/** While true the actor isn't moved along its path (ragdolling, etc.). */
	virtual bool IsMovementFrozen() const { return false; }

	bool HasArrived() const;
	FVector EvaluateLocation() const;
	float GetCurrentSpeed() const { return Segment.Speed; }
	FVector GetMoveDirection() const;

protected:
	/** Server: called when the current leg is finished. */
	virtual void OnArrived() {}

	float GetNetTime() const;

	UPROPERTY(Replicated) FFTOMoveSegment Segment;

	/** Degrees per second the body turns to face travel. */
	UPROPERTY(EditDefaultsOnly, Category="Movement") float TurnRate = 360.f;

	/** Extra per-frame cosmetic hook (bobbing etc.). */
	virtual void TickCosmetics(float DeltaSeconds) {}

private:
	bool bArrivalHandled = false;
};
