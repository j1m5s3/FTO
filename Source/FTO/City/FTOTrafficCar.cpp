#include "City/FTOTrafficCar.h"
#include "City/FTOCityGenerator.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameMode.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Crime/FTOCrimeDirector.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	FText ViolationText(EFTOCarViolation V)
	{
		switch (V)
		{
		case EFTOCarViolation::Speeding:        return INVTEXT("speeding");
		case EFTOCarViolation::BrokenTaillight: return INVTEXT("broken taillight");
		case EFTOCarViolation::ExpiredTags:     return INVTEXT("expired tags");
		case EFTOCarViolation::NoisyExhaust:    return INVTEXT("exhaust that sounds like a goose");
		default:                                return FText::GetEmpty();
		}
	}

	const FIntPoint Directions[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
}

AFTOTrafficCar::AFTOTrafficCar()
{
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->InitBoxExtent(FVector(230.f, 110.f, 80.f));
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	RootComponent = Collision;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	Chassis = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Chassis"));
	Chassis->SetupAttachment(Collision);
	Chassis->SetStaticMesh(CubeMesh.Object);
	Chassis->SetRelativeLocation(FVector(0.f, 0.f, -10.f));
	Chassis->SetRelativeScale3D(FVector(4.6f, 2.1f, 0.9f));
	Chassis->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Cabin = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cabin"));
	Cabin->SetupAttachment(Collision);
	Cabin->SetStaticMesh(CubeMesh.Object);
	Cabin->SetRelativeLocation(FVector(-30.f, 0.f, 70.f));
	Cabin->SetRelativeScale3D(FVector(2.4f, 1.9f, 0.8f));
	Cabin->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	for (int32 i = 0; i < 4; ++i)
	{
		UStaticMeshComponent* Wheel = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Wheel%d"), i));
		Wheel->SetupAttachment(Collision);
		Wheel->SetStaticMesh(CylinderMesh.Object);
		Wheel->SetRelativeLocation(FVector(i < 2 ? 150.f : -150.f, i % 2 ? 105.f : -105.f, -55.f));
		Wheel->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
		Wheel->SetRelativeScale3D(FVector(0.8f, 0.8f, 0.35f));
		Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wheels.Add(Wheel);
	}

	Indicator = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Indicator"));
	Indicator->SetupAttachment(Collision);
	Indicator->SetRelativeLocation(FVector(0.f, 0.f, 260.f));
	Indicator->SetHorizontalAlignment(EHTA_Center);
	Indicator->SetWorldSize(140.f);
	Indicator->SetTextRenderColor(FColor(255, 200, 40));
	Indicator->SetText(INVTEXT("!"));
	Indicator->SetVisibility(false);

	TurnRate = 220.f;
}

void AFTOTrafficCar::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOTrafficCar, LookSeed);
	DOREPLIFETIME(AFTOTrafficCar, CarState);
	DOREPLIFETIME(AFTOTrafficCar, Violation);
}

void AFTOTrafficCar::BeginPlay()
{
	Super::BeginPlay();
	OnRep_Look();
	RefreshIndicator();
}

void AFTOTrafficCar::OnRep_Look()
{
	FRandomStream LookRng(LookSeed);
	if (!PaintMaterial)
	{
		PaintMaterial = Chassis->CreateAndSetMaterialInstanceDynamic(0);
		Cabin->SetMaterial(0, PaintMaterial);
	}
	if (PaintMaterial)
	{
		PaintMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor::MakeFromHSV8(uint8(LookRng.RandRange(0, 255)), 200, 240));
	}
	// Some cars are comically long, some are tiny.
	const float Length = LookRng.FRandRange(0.8f, 1.25f);
	Chassis->SetRelativeScale3D(FVector(4.6f * Length, 2.1f, 0.9f));
}

void AFTOTrafficCar::OnRep_CarState()
{
	RefreshIndicator();
}

void AFTOTrafficCar::RefreshIndicator()
{
	const bool bShow = Violation != EFTOCarViolation::None && CarState != EFTOCarState::Fleeing;
	Indicator->SetVisibility(bShow || CarState == EFTOCarState::Fleeing);
	Indicator->SetText(CarState == EFTOCarState::Fleeing ? INVTEXT("!!!") : INVTEXT("!"));
	Indicator->SetTextRenderColor(CarState == EFTOCarState::Fleeing ? FColor(255, 40, 40) : FColor(255, 200, 40));
}

void AFTOTrafficCar::RollViolation()
{
	const float Roll = Rng.FRand();
	if (Roll < 0.08f)      Violation = EFTOCarViolation::Speeding;
	else if (Roll < 0.14f) Violation = EFTOCarViolation::BrokenTaillight;
	else if (Roll < 0.19f) Violation = EFTOCarViolation::ExpiredTags;
	else if (Roll < 0.23f) Violation = EFTOCarViolation::NoisyExhaust;
	else                   Violation = EFTOCarViolation::None;
	RefreshIndicator();
}

void AFTOTrafficCar::StartDriving(AFTOCityGenerator* InCity, int32 InI, int32 InJ, int32 InSeed)
{
	check(HasAuthority());
	City = InCity;
	Rng.Initialize(InSeed);
	LookSeed = InSeed;
	OnRep_Look();
	RollViolation();

	Node = FIntPoint(InI, InJ);
	Heading = Directions[Rng.RandRange(0, 3)];

	const FVector Start = LanePoint(Node.X, Node.Y, Heading);
	Segment.From = Start;
	Segment.To = Start;
	SetActorLocation(Start);
	SetActorRotation(FRotator(0.f, FVector(Heading.X, Heading.Y, 0.f).Rotation().Yaw, 0.f));
	DriveToNextIntersection();
}

FVector AFTOTrafficCar::LanePoint(int32 I, int32 J, const FIntPoint& InHeading) const
{
	// Drive on the right: right of forward is (-fwd.Y, fwd.X) in UE's left-handed XY.
	const FVector Forward(InHeading.X, InHeading.Y, 0.f);
	const FVector Right(-Forward.Y, Forward.X, 0.f);
	return City->GetIntersection(I, J) + Right * City->GetRoadWidth() * 0.25f + FVector(0.f, 0.f, 95.f);
}

void AFTOTrafficCar::DriveToNextIntersection()
{
	if (!City)
	{
		return;
	}

	// Pick a heading at this intersection: prefer not to U-turn.
	TArray<FIntPoint> Options;
	for (const FIntPoint& Dir : Directions)
	{
		const FIntPoint Next = Node + Dir;
		const bool bInside = Next.X >= 0 && Next.Y >= 0 && Next.X < City->NumIntersectionsX() && Next.Y < City->NumIntersectionsY();
		const bool bUTurn = Dir == FIntPoint(-Heading.X, -Heading.Y);
		if (bInside && !bUTurn)
		{
			// Going straight is more common than turning.
			Options.Add(Dir);
			if (Dir == Heading) { Options.Add(Dir); Options.Add(Dir); }
		}
	}
	if (Options.Num() == 0)
	{
		Options.Add(FIntPoint(-Heading.X, -Heading.Y));
	}

	Heading = Options[Rng.RandRange(0, Options.Num() - 1)];
	Node += Heading;

	float Speed = CruiseSpeed * Rng.FRandRange(0.85f, 1.1f);
	if (Violation == EFTOCarViolation::Speeding) { Speed *= 1.7f; }
	if (CarState == EFTOCarState::Fleeing)      { Speed = CruiseSpeed * 2.2f; }

	PendingTarget = LanePoint(Node.X, Node.Y, Heading);
	PendingSpeed = Speed;
	MoveTo(PendingTarget, PendingSpeed);
}

void AFTOTrafficCar::OnArrived()
{
	switch (CarState)
	{
	case EFTOCarState::PullingOver:
		CarState = EFTOCarState::Stopped;
		Hold();
		RefreshIndicator();
		break;

	case EFTOCarState::Fleeing:
		if (GetWorld()->GetTimeSeconds() >= FleeUntil)
		{
			Destroy(); // got away... the incident on the board is what matters now
			return;
		}
		DriveToNextIntersection();
		break;

	case EFTOCarState::Driving:
		DriveToNextIntersection();
		break;

	default:
		break;
	}
}

bool AFTOTrafficCar::IsPathBlocked() const
{
	const FVector Forward = GetActorForwardVector();
	const FVector Start = GetActorLocation() + Forward * 260.f;
	const FVector End = Start + Forward * 450.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOCarAhead), false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);

	FHitResult Hit;
	return GetWorld()->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, Objects, FCollisionShape::MakeSphere(100.f), Params);
}

void AFTOTrafficCar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Spin the wheels while moving.
	if (GetCurrentSpeed() > 0.f)
	{
		for (UStaticMeshComponent* Wheel : Wheels)
		{
			Wheel->AddLocalRotation(FRotator(0.f, GetCurrentSpeed() * DeltaSeconds * 0.8f, 0.f));
		}
	}

	if (Indicator->IsVisible())
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				const FVector ToCam = PC->PlayerCameraManager->GetCameraLocation() - Indicator->GetComponentLocation();
				Indicator->SetWorldRotation(FRotator(0.f, ToCam.Rotation().Yaw, 0.f));
			}
		}
	}

	// Server: brake for officers, citizens and other cars. Fleeing cars don't care (much).
	if (!HasAuthority() || (CarState != EFTOCarState::Driving && CarState != EFTOCarState::Fleeing))
	{
		return;
	}

	BlockCheckAccumulator += DeltaSeconds;
	if (BlockCheckAccumulator < 0.2f)
	{
		return;
	}
	BlockCheckAccumulator = 0.f;

	const bool bBlocked = CarState == EFTOCarState::Driving && IsPathBlocked();
	if (bBlocked && !bWaitingForClearRoad)
	{
		bWaitingForClearRoad = true;
		Hold();
	}
	else if (!bBlocked && bWaitingForClearRoad)
	{
		bWaitingForClearRoad = false;
		MoveTo(PendingTarget, PendingSpeed);
	}
}

float AFTOTrafficCar::GetInteractRange() const
{
	// Pulling over a moving car is easier from a bit further away.
	return CarState == EFTOCarState::Driving ? 700.f : 400.f;
}

bool AFTOTrafficCar::CanInteract(const AFTOCharacter* Officer) const
{
	switch (CarState)
	{
	case EFTOCarState::Driving: return Violation != EFTOCarViolation::None;
	case EFTOCarState::Stopped: return true;
	default:                    return false;
	}
}

FText AFTOTrafficCar::GetInteractPrompt(const AFTOCharacter* Officer) const
{
	if (CarState == EFTOCarState::Stopped)
	{
		return FText::Format(INVTEXT("Write ticket ({0})"), ViolationText(Violation));
	}
	return FText::Format(INVTEXT("Pull over ({0})"), ViolationText(Violation));
}

void AFTOTrafficCar::Interact(AFTOCharacter* Officer)
{
	check(HasAuthority());

	if (CarState == EFTOCarState::Driving && Violation != EFTOCarViolation::None)
	{
		// Signal, pull over to the curb just ahead.
		CarState = EFTOCarState::PullingOver;
		bWaitingForClearRoad = false;
		const FVector Right = FVector(-GetActorForwardVector().Y, GetActorForwardVector().X, 0.f);
		const FVector Curb = GetActorLocation() + GetActorForwardVector() * 400.f + Right * (City ? City->GetRoadWidth() * 0.2f : 200.f);
		MoveTo(Curb, 350.f);
		RefreshIndicator();
		return;
	}

	if (CarState == EFTOCarState::Stopped)
	{
		CarState = EFTOCarState::WritingTicket;
		GetWorldTimerManager().SetTimer(TicketTimer, this, &AFTOTrafficCar::FinishTicket, TicketSeconds, false);

		if (AFTOPlayerController* PC = Officer ? Cast<AFTOPlayerController>(Officer->GetController()) : nullptr)
		{
			PC->ClientToast(INVTEXT("\"Is there a problem, officer?\""), FLinearColor::White);
		}
	}
}

void AFTOTrafficCar::FinishTicket()
{
	AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();

	if (Rng.FRand() < WantedChance)
	{
		// Plot twist: the driver is wanted. Floor it!
		CarState = EFTOCarState::Fleeing;
		FleeUntil = GetWorld()->GetTimeSeconds() + 20.f;
		Violation = EFTOCarViolation::None;
		RefreshIndicator();

		if (AFTOGameMode* GM = GetWorld()->GetAuthGameMode<AFTOGameMode>())
		{
			GM->GetCrimeDirector()->SpawnIncidentAt(TEXT("CarChase"), GetActorLocation(), true);
		}
		if (GS)
		{
			GS->AddChaos(3.f);
		}

		// Rejoin the grid from the nearest intersection in our direction of travel.
		DriveToNextIntersection();
		return;
	}

	if (GS)
	{
		GS->AddChaos(-TicketChaosRelief);
		++GS->TrafficStops;
	}

	Violation = EFTOCarViolation::None;
	CarState = EFTOCarState::Driving;
	RefreshIndicator();

	// Merge back into traffic.
	MoveTo(PendingTarget, CruiseSpeed);
}
