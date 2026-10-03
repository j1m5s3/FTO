#include "City/FTOTrafficCar.h"
#include "Engine/OverlapResult.h"
#include "Physics/FTOImpact.h"
#include "Physics/FTOVehicleDamage.h"
#include "City/FTOCityGenerator.h"
#include "Core/FTOCharacter.h"
#include "Vehicles/FTOCruiser.h"
#include "Core/FTOGameMode.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Crime/FTOCrimeDirector.h"
#include "Crime/FTOIncident.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Scoring/FTOScoring.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Animation/FTOCharacterAnimInstance.h"
#include "Art/FTOArt.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Vehicles/FTOVehicleSeats.h"

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

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMat(FTOArt::BaseMaterialPath);
	BaseMaterial = BaseMat.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> VehicleMat(FTOArt::VehicleMaterialPath);
	VehicleMaterial = VehicleMat.Object;

	// In-house vehicle models (Tools/Blender/build_vehicles.py): origin on the ground, facing +X.
	for (const TCHAR* Style : { TEXT("SM_Car_Sedan"), TEXT("SM_Car_Hatchback"), TEXT("SM_Car_Van"), TEXT("SM_Car_Pickup"), TEXT("SM_Car_Taxi"), TEXT("SM_Car_IceCream") })
	{
		ConstructorHelpers::FObjectFinder<UStaticMesh> Finder(*FString::Printf(TEXT("/Game/FTO/Vehicles/%s.%s"), Style, Style));
		ConstructorHelpers::FObjectFinder<UStaticMesh> Beaten(*FString::Printf(TEXT("/Game/FTO/Vehicles/%s_Dented.%s_Dented"), Style, Style));
		if (Finder.Succeeded())
		{
			BodyStyles.Add(Finder.Object);
			DentedStyles.Add(Beaten.Object);
		}
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WheelAsset(TEXT("/Game/FTO/Vehicles/SM_Wheel.SM_Wheel"));

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Collision);
	Body->SetRelativeLocation(FVector(0.f, 0.f, -RideHeight));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Wheels ride on the body mesh's SOCKET_Wheel_* sockets.
	static const FName WheelSockets[] = { TEXT("Wheel_FL"), TEXT("Wheel_FR"), TEXT("Wheel_RL"), TEXT("Wheel_RR") };
	for (int32 i = 0; i < 4; ++i)
	{
		UStaticMeshComponent* Wheel = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Wheel%d"), i));
		Wheel->SetupAttachment(Body, WheelSockets[i]);
		Wheel->SetUsingAbsoluteScale(true); // never inherit socket/import scale
		Wheel->SetStaticMesh(WheelAsset.Object);
		Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wheels.Add(Wheel);
	}

	// Citizens in the seats (Seat_* sockets on every body; see Tools/Blender/build_vehicles.py).
	for (int32 Index = 1; Index <= 8; ++Index)
	{
		const FString Path = FString::Printf(TEXT("/Game/FTO/Characters/Civilians/SK_Civilian_%02d.SK_Civilian_%02d"), Index, Index);
		ConstructorHelpers::FObjectFinder<USkeletalMesh> Look(*Path);
		if (Look.Succeeded())
		{
			OccupantLooks.Add(Look.Object);
		}
	}
	for (const EFTOSeat Seat : { EFTOSeat::Driver, EFTOSeat::Passenger, EFTOSeat::RearLeft, EFTOSeat::RearRight })
	{
		USkeletalMeshComponent* Occupant = CreateDefaultSubobject<USkeletalMeshComponent>(*FString::Printf(TEXT("Occupant%d"), int32(Seat)));
		Occupant->SetupAttachment(Body, FTOSeats::SeatSocket(Seat));
		Occupant->SetRelativeRotation(FRotator(0.f, -90.f, 0.f)); // Blender characters face +Y
		Occupant->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Occupant->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Occupant->SetAnimInstanceClass(UFTOCharacterAnimInstance::StaticClass());
		Occupant->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		Occupant->bEnableUpdateRateOptimizations = true;
		Occupant->SetCastShadow(false); // they're in the car's shadow anyway
		Occupant->SetVisibility(false);
		Occupants.Add(Occupant);
	}

	Damage = CreateDefaultSubobject<UFTOVehicleDamage>(TEXT("Damage"));

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
	DOREPLIFETIME(AFTOTrafficCar, bDriverOut);
}

void AFTOTrafficCar::BeginPlay()
{
	Super::BeginPlay();
	OnRep_Look();
	RefreshIndicator();
	if (HasAuthority())
	{
		Damage->OnWrecked.AddUObject(this, &AFTOTrafficCar::HandleWrecked);
	}
}

void AFTOTrafficCar::HandleWrecked()
{
	// A getaway ends here: the driver climbs out and gives up (the chase incident takes it from there).
	if (CarState == EFTOCarState::Fleeing && ChaseIncident)
	{
		ChaseIncident->TalkedDown();
		return;
	}
	if (CarState == EFTOCarState::Busted)
	{
		return;
	}
	CarState = EFTOCarState::Wrecked;
	Violation = EFTOCarViolation::None;
	bWaitingForClearRoad = false;
	GetWorldTimerManager().ClearTimer(TicketTimer);
	Hold();
	RefreshIndicator();
	SetLifeSpan(30.f); // towed away
}

void AFTOTrafficCar::OnRep_Look()
{
	if (BodyStyles.Num() == 0)
	{
		return;
	}

	// Mostly everyday cars; the odd taxi, and very occasionally an ice cream truck.
	FRandomStream LookRng(LookSeed);
	const float Roll = LookRng.FRand();
	int32 Style = LookRng.RandRange(0, FMath::Min(3, BodyStyles.Num() - 1));
	if (Roll < 0.05f && BodyStyles.IsValidIndex(5))
	{
		Style = 5;
	}
	else if (Roll < 0.15f && BodyStyles.IsValidIndex(4))
	{
		Style = 4;
	}
	Body->SetStaticMesh(BodyStyles[Style]);

	const FLinearColor Paint = FLinearColor::MakeFromHSV8(uint8(LookRng.RandRange(0, 255)), 190, 240);
	if (!PaintMaterial)
	{
		PaintMaterial = FTOArt::ApplyColor(Body, VehicleMaterial ? VehicleMaterial.Get() : BaseMaterial.Get(), Paint, 0.f, FTOArt::BodySlot(Body));
	}
	FTOArt::SetColor(PaintMaterial, Paint);
	Damage->SetBody(Body, DentedStyles.IsValidIndex(Style) ? DentedStyles[Style].Get() : nullptr);

	SeatOccupants(LookRng, Style);
}

void AFTOTrafficCar::SeatOccupants(FRandomStream& LookRng, int32 Style)
{
	// Everyone has a driver and some bring company; taxi fares ride in the back.
	const bool bTaxi = Style == 4;
	const bool bIceCream = Style == 5;
	for (int32 i = 0; i < Occupants.Num(); ++i)
	{
		USkeletalMeshComponent* Occupant = Occupants[i];
		const EFTOSeat Seat = static_cast<EFTOSeat>(i + 1);
		float Chance = 0.f;
		switch (Seat)
		{
		case EFTOSeat::Driver:    Chance = 1.f; break;
		case EFTOSeat::Passenger: Chance = bTaxi ? 0.f : (bIceCream ? 0.2f : 0.35f); break;
		case EFTOSeat::RearLeft:  Chance = bTaxi ? 0.2f : 0.12f; break;
		case EFTOSeat::RearRight: Chance = bTaxi ? 0.75f : 0.15f; break;
		default: break;
		}

		// Same draws whatever the outcome, so every machine seats the same people.
		const float Roll = LookRng.FRand();
		const int32 Look = LookRng.RandRange(0, FMath::Max(0, OccupantLooks.Num() - 1));
		const uint8 Hue = uint8(LookRng.RandRange(0, 255));

		const bool bPresent = OccupantLooks.IsValidIndex(Look) && Roll < Chance && Body->DoesSocketExist(FTOSeats::SeatSocket(Seat));
		Occupant->SetVisibility(bPresent);
		Occupant->SetComponentTickEnabled(bPresent);
		if (!bPresent)
		{
			Occupant->SetSkeletalMeshAsset(nullptr);
			continue;
		}

		Occupant->SetSkeletalMeshAsset(OccupantLooks[Look]);
		UMaterialInstanceDynamic* Shirt = FTOArt::ApplyColor(Occupant, BaseMaterial, FLinearColor::MakeFromHSV8(Hue, 170, 235));
		for (int32 Slot = 1; Slot < Occupant->GetNumMaterials(); ++Slot)
		{
			Occupant->SetMaterial(Slot, Shirt);
		}
	}
	if (bDriverOut && Occupants.Num() > 0)
	{
		Occupants[0]->SetVisibility(false); // out on the road, giving themselves up
	}
}

EFTOAnimAction AFTOTrafficCar::GetAnimActionFor(const USkeletalMeshComponent* Mesh) const
{
	if (CarState == EFTOCarState::Busted || CarState == EFTOCarState::Wrecked)
	{
		return EFTOAnimAction::SitHandsUp;
	}
	return Occupants.Num() > 0 && Mesh == Occupants[0] ? EFTOAnimAction::Drive : EFTOAnimAction::Ride;
}

void AFTOTrafficCar::OnRep_CarState()
{
	RefreshIndicator();
	if (bDriverOut && Occupants.Num() > 0)
	{
		Occupants[0]->SetVisibility(false);
	}
}

void AFTOTrafficCar::RefreshIndicator()
{
	switch (CarState)
	{
	case EFTOCarState::Fleeing:
		Indicator->SetVisibility(true);
		Indicator->SetText(INVTEXT("!!!"));
		Indicator->SetTextRenderColor(FColor(255, 40, 40));
		break;
	case EFTOCarState::Busted:
		Indicator->SetVisibility(true);
		Indicator->SetText(INVTEXT("BUSTED"));
		Indicator->SetTextRenderColor(FColor(60, 255, 90));
		break;
	default:
		Indicator->SetVisibility(Violation != EFTOCarViolation::None);
		Indicator->SetText(INVTEXT("!"));
		Indicator->SetTextRenderColor(FColor(255, 200, 40));
		break;
	}
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
	MaxPatience = Rng.FRandRange(2.f, 4.5f);
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
	if (CarState == EFTOCarState::Fleeing)      { Speed = CruiseSpeed * 2.2f * (1.f - 0.3f * GetFlatTyres()); }

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

bool AFTOTrafficCar::IsPathBlocked(bool bIncludeCars) const
{
	const FVector Forward = GetActorForwardVector();
	const FVector Start = GetActorLocation() + Forward * 260.f;
	const FVector End = Start + Forward * 450.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOCarAhead), false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	if (bIncludeCars)
	{
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	}

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
			// Roll forward: 360 degrees per wheel circumference (r = 38 cm).
			Wheel->AddLocalRotation(FRotator(-GetCurrentSpeed() * DeltaSeconds * 1.508f, 0.f, 0.f));
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

	// Server: chases end when the incident does (normally with the driver climbing out to give up).
	if (HasAuthority() && CarState == EFTOCarState::Fleeing && ChaseIncident)
	{
		if (ChaseIncident->GetState() == EFTOIncidentState::Resolved || ChaseIncident->IsSubdued())
		{
			Bust();
		}
		else if (ChaseIncident->GetState() == EFTOIncidentState::Failed)
		{
			ChaseIncident = nullptr;
			FleeUntil = FMath::Min(FleeUntil, GetWorld()->GetTimeSeconds() + 4.f); // got away
		}
	}

	// Server: brake for officers, citizens and other cars. Fleeing cars don't care (much).
	if (!HasAuthority() || (CarState != EFTOCarState::Driving && CarState != EFTOCarState::Fleeing))
	{
		return;
	}

	// A getaway car doesn't stop for anyone: whoever's in front of the bumper goes flying.
	if (CarState == EFTOCarState::Fleeing && GetCurrentSpeed() >= FTOImpact::MinRunOverSpeed)
	{
		TArray<FOverlapResult> InTheWay;
		const FVector Ahead = GetActorLocation() + GetActorForwardVector() * 200.f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOGetawayBumper), false, this);
		if (GetWorld()->OverlapMultiByObjectType(InTheWay, Ahead, GetActorQuat(), FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeBox(FVector(90.f, 120.f, 90.f)), Params))
		{
			for (const FOverlapResult& Overlap : InTheWay)
			{
				FTOImpact::RunOver(Overlap.GetActor(), GetActorLocation(), GetMoveDirection() * GetCurrentSpeed(), nullptr);
			}
		}
	}

	BlockCheckAccumulator += DeltaSeconds;
	if (BlockCheckAccumulator < 0.2f)
	{
		return;
	}
	BlockCheckAccumulator = 0.f;

	// Cars that have waited a while get impatient and stop yielding to other cars (never to people),
	// which breaks four-way standoffs at intersections.
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bImpatient = Now < IgnoreCarsUntil;
	const bool bBlocked = CarState == EFTOCarState::Driving && IsPathBlocked(!bImpatient);
	if (bBlocked && !bWaitingForClearRoad)
	{
		bWaitingForClearRoad = true;
		WaitStartTime = Now;
		Hold();
	}
	else if (bBlocked && bWaitingForClearRoad && Now - WaitStartTime > MaxPatience)
	{
		IgnoreCarsUntil = Now + 2.5f;
		WaitStartTime = Now;
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

	if (RequestPullOver())
	{
		return;
	}

	if (CarState == EFTOCarState::Stopped)
	{
		CarState = EFTOCarState::WritingTicket;
		TicketOfficer = Officer;
		GetWorldTimerManager().SetTimer(TicketTimer, this, &AFTOTrafficCar::FinishTicket, TicketSeconds, false);
		if (Officer)
		{
			Officer->PlayTimedAction(EFTOAnimAction::Clipboard, TicketSeconds);
		}

		if (AFTOPlayerController* PC = Officer ? Cast<AFTOPlayerController>(Officer->GetController()) : nullptr)
		{
			PC->ClientToast(INVTEXT("\"Is there a problem, officer?\""), FLinearColor::White);
		}
	}
}

bool AFTOTrafficCar::RequestPullOver()
{
	check(HasAuthority());
	if (CarState != EFTOCarState::Driving || Violation == EFTOCarViolation::None)
	{
		return false;
	}

	// Signal, pull over to the curb just ahead.
	CarState = EFTOCarState::PullingOver;
	bWaitingForClearRoad = false;
	const FVector Right = FVector(-GetActorForwardVector().Y, GetActorForwardVector().X, 0.f);
	const FVector Curb = GetActorLocation() + GetActorForwardVector() * 400.f + Right * (City ? City->GetRoadWidth() * 0.2f : 200.f);
	MoveTo(Curb, 350.f);
	RefreshIndicator();
	return true;
}

void AFTOTrafficCar::FinishTicket()
{
	AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();

	if (Rng.FRand() < WantedChance)
	{
		// Plot twist: the driver is wanted.
		MakeGetaway();
		return;
	}

	if (GS)
	{
		GS->AddChaos(-TicketChaosRelief);
		++GS->TrafficStops;
		GS->MulticastPlaySound(AFTOGameState::Sounds().Chime, GetActorLocation(), 0.8f);
	}
	FTOScoring::Award(TicketOfficer.Get(), EFTOScore::Ticket, GetActorLocation() + FVector(0.f, 0.f, 200.f));

	Violation = EFTOCarViolation::None;
	CarState = EFTOCarState::Driving;
	RefreshIndicator();

	// Merge back into traffic.
	MoveTo(PendingTarget, CruiseSpeed);
}

void AFTOTrafficCar::MakeGetaway(FName ChaseCrime, float Seconds, float Toughness)
{
	check(HasAuthority());
	// Floor it! The chase incident rides along with us.
	CarState = EFTOCarState::Fleeing;
	// A crook's getaway car now: running it off the road is fair game, not property damage.
	Damage->bCitizensCar = false;
	FleeUntil = GetWorld()->GetTimeSeconds() + Seconds;
	Damage->Toughness = Toughness;
	Violation = EFTOCarViolation::None;
	bWaitingForClearRoad = false;
	RefreshIndicator();

	if (AFTOGameMode* GM = GetWorld()->GetAuthGameMode<AFTOGameMode>())
	{
		ChaseIncident = GM->GetCrimeDirector()->SpawnIncidentAt(ChaseCrime.IsNone() ? FName(TEXT("CarChase")) : ChaseCrime, GetActorLocation(), true);
		if (ChaseIncident)
		{
			ChaseIncident->FollowActor(this);
		}
	}
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->AddChaos(3.f);
	}

	// Rejoin the grid from the nearest intersection in our direction of travel.
	DriveToNextIntersection();
}

FVector AFTOTrafficCar::GetWheelLocation(int32 Index) const
{
	return Wheels.IsValidIndex(Index) ? Wheels[Index]->GetComponentLocation() : GetActorLocation();
}

void AFTOTrafficCar::OnRep_Tyres()
{
	// A flat sags onto its rim.
	for (int32 i = 0; i < Wheels.Num(); ++i)
	{
		if ((FlatTyres & (1 << i)) && !(ShownFlat & (1 << i)))
		{
			ShownFlat |= (1 << i);
			Wheels[i]->AddRelativeLocation(FVector(0.f, 0.f, -9.f));
		}
	}
}

void AFTOTrafficCar::RoundHit(const FVector& At, AController* By)
{
	check(HasAuthority());
	for (int32 i = 0; i < Wheels.Num() && i < 8; ++i)
	{
		if ((FlatTyres & (1 << i)) || FVector::DistSquared(Wheels[i]->GetComponentLocation(), At) > FMath::Square(75.f))
		{
			continue;
		}
		FlatTyres |= (1 << i);
		OnRep_Tyres();
		if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
		{
			GS->MulticastPlaySound(AFTOGameState::Sounds().Ricochet, At, 1.f);
		}
		AFTOPlayerController* Shooter = Cast<AFTOPlayerController>(By);
		if (CarState != EFTOCarState::Fleeing || !ChaseIncident)
		{
			break;
		}
		if (GetFlatTyres() < 2)
		{
			if (Shooter)
			{
				Shooter->ClientToast(INVTEXT("Tyre's out! One more and they're going nowhere."), FLinearColor(0.6f, 0.85f, 1.f));
			}
			break;
		}
		// Two flats: the getaway's over. Whoever shot them out, and whoever was driving them, did it together.
		const AFTOCharacter* Gunner = Shooter ? Cast<AFTOCharacter>(Shooter->GetPawn()) : nullptr;
		if (Gunner)
		{
			FTOScoring::Award(Gunner, EFTOScore::Teamwork, GetActorLocation() + FVector(0.f, 0.f, 250.f));
			if (const AFTOCruiser* Cruiser = Gunner->IsRidingShotgun() ? Cast<AFTOCruiser>(Gunner->GetCurrentVehicle()) : nullptr)
			{
				FTOScoring::Award(Cruiser, EFTOScore::Teamwork, GetActorLocation() + FVector(0.f, 0.f, 300.f));
			}
		}
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(It->Get()); PC && PC->GetPawn() &&
				FVector::DistSquared2D(PC->GetPawn()->GetActorLocation(), GetActorLocation()) < FMath::Square(6000.f))
			{
				PC->ClientToast(INVTEXT("Tyres shot out! The getaway car grinds to a halt. Cuff the driver (E)!"), FLinearColor(0.4f, 1.f, 0.5f));
			}
		}
		ChaseIncident->TalkedDown();
		break;
	}
}

void AFTOTrafficCar::DriverSurrenders()
{
	check(HasAuthority());
	bDriverOut = true;
	Bust();
}

FVector AFTOTrafficCar::GetDriverDoorLocation() const
{
	// Out from the driver's seat, clear of the door, down on the road.
	const FVector Seat = Occupants.Num() > 0 ? Occupants[0]->GetComponentLocation() : GetActorLocation();
	const FVector Right = GetActorRightVector();
	const float Side = FVector::DotProduct(Seat - GetActorLocation(), Right) >= 0.f ? 1.f : -1.f;
	FVector Door = Seat + Right * Side * 150.f;
	Door.Z = GetActorLocation().Z - RideHeight;
	return Door;
}

void AFTOTrafficCar::Bust()
{
	CarState = EFTOCarState::Busted;
	ChaseIncident = nullptr;
	Hold();
	RefreshIndicator();
	SetLifeSpan(20.f); // towed away
}
