#include "City/FTOPathMover.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

AFTOPathMover::AFTOPathMover()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(4.f);
	SetNetCullDistanceSquared(FMath::Square(15000.f));
}

void AFTOPathMover::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOPathMover, Segment);
}

float AFTOPathMover::GetNetTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.f;
	}
	if (const AGameStateBase* GS = World->GetGameState())
	{
		return HasAuthority() ? World->GetTimeSeconds() : GS->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

FVector AFTOPathMover::EvaluateLocation() const
{
	const float Distance = FVector::Dist(Segment.From, Segment.To);
	if (Segment.Speed <= 0.f || Distance < KINDA_SMALL_NUMBER)
	{
		return Segment.Speed <= 0.f ? FVector(Segment.From) : FVector(Segment.To);
	}
	const float Alpha = FMath::Clamp((GetNetTime() - Segment.StartTime) * Segment.Speed / Distance, 0.f, 1.f);
	return FMath::Lerp(FVector(Segment.From), FVector(Segment.To), Alpha);
}

FVector AFTOPathMover::GetMoveDirection() const
{
	return (FVector(Segment.To) - FVector(Segment.From)).GetSafeNormal2D();
}

bool AFTOPathMover::HasArrived() const
{
	if (Segment.Speed <= 0.f)
	{
		return false;
	}
	const float Distance = FVector::Dist(Segment.From, Segment.To);
	return (GetNetTime() - Segment.StartTime) * Segment.Speed >= Distance;
}

void AFTOPathMover::MoveTo(const FVector& Target, float Speed)
{
	check(HasAuthority());
	Segment.From = EvaluateLocation();
	Segment.To = Target;
	Segment.StartTime = GetNetTime();
	Segment.Speed = Speed;
	Segment.bFace = false;
	bArrivalHandled = false;
	ForceNetUpdate();
}

void AFTOPathMover::Hold()
{
	check(HasAuthority());
	const FVector Here = EvaluateLocation();
	Segment.From = Here;
	Segment.To = Here;
	Segment.StartTime = GetNetTime();
	Segment.Speed = 0.f;
	Segment.bFace = false;
	bArrivalHandled = true;
	ForceNetUpdate();
}

void AFTOPathMover::TeleportAndHold(const FVector& Location)
{
	check(HasAuthority());
	Segment.From = Location;
	Segment.To = Location;
	Segment.StartTime = GetNetTime();
	Segment.Speed = 0.f;
	Segment.bFace = false;
	bArrivalHandled = true;
	SetActorLocation(Location);
	ForceNetUpdate();
}

void AFTOPathMover::FaceYaw(float Yaw)
{
	check(HasAuthority());
	Segment.bFace = true;
	Segment.FaceYaw = FRotator::NormalizeAxis(Yaw);
	ForceNetUpdate();
}

void AFTOPathMover::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsMovementFrozen())
	{
		return;
	}

	const FVector NewLocation = EvaluateLocation();
	if (!NewLocation.Equals(GetActorLocation(), 0.01f))
	{
		SetActorLocation(NewLocation);
	}

	const FVector Dir = GetMoveDirection();
	if (Segment.Speed > 0.f && !Dir.IsNearlyZero())
	{
		const FRotator Target = Dir.Rotation();
		SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, Target.Yaw, 0.f), DeltaSeconds, TurnRate));
	}
	else if (Segment.bFace && FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Segment.FaceYaw)) > 0.5f)
	{
		SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, Segment.FaceYaw, 0.f), DeltaSeconds, TurnRate));
	}

	TickCosmetics(DeltaSeconds);

	if (HasAuthority() && !bArrivalHandled && HasArrived())
	{
		bArrivalHandled = true;
		OnArrived();
	}
}
