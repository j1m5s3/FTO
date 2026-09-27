#include "Crime/FTOArrestee.h"
#include "Audio/FTOFootsteps.h"
#include "Animation/FTOCharacterAnimInstance.h"
#include "City/FTOCityGenerator.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Scoring/FTOScoring.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicles/FTOCruiser.h"
#include "FTO.h"

namespace
{
	constexpr float HalfHeight = 92.f;
	/** Breadcrumbs are dropped this far apart. */
	constexpr float TrailStep = 40.f;
	/** Anything more than a jump between breadcrumbs is a teleport: catch up rather than walk it. */
	constexpr float TrailJump = 800.f;
	constexpr float CellWalkSpeed = 160.f;
}

AFTOArrestee::AFTOArrestee()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(15.f);

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(35.f, HalfHeight);
	Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Capsule;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> SuspectMesh(TEXT("/Game/FTO/Characters/Civilians/SK_Suspect.SK_Suspect"));
	Footsteps = CreateDefaultSubobject<UFTOFootsteps>(TEXT("Footsteps"));
	Footsteps->Volume = 0.5f;
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Capsule);
	Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -HalfHeight), FRotator(0.f, -90.f, 0.f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Body->SetAnimInstanceClass(UFTOCharacterAnimInstance::StaticClass());
	if (SuspectMesh.Succeeded())
	{
		Body->SetSkeletalMeshAsset(SuspectMesh.Object);
	}

	Tag = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Tag"));
	Tag->SetupAttachment(Capsule);
	Tag->SetRelativeLocation(FVector(0.f, 0.f, 140.f));
	Tag->SetHorizontalAlignment(EHTA_Center);
	Tag->SetWorldSize(34.f);
	Tag->SetTextRenderColor(FColor(160, 210, 255));
	Tag->SetText(INVTEXT("CUFFED"));
}

void AFTOArrestee::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOArrestee, Escort);
	DOREPLIFETIME(AFTOArrestee, State);
	DOREPLIFETIME(AFTOArrestee, RideVehicle);
	DOREPLIFETIME(AFTOArrestee, RideSeat);
	DOREPLIFETIME(AFTOArrestee, NetLocation);
	DOREPLIFETIME(AFTOArrestee, NetYaw);
	DOREPLIFETIME(AFTOArrestee, AnimSpeed);
	DOREPLIFETIME(AFTOArrestee, Crime);
	DOREPLIFETIME(AFTOArrestee, bJailed);
}

void AFTOArrestee::Init(AFTOCharacter* Officer, float InBookingRelief, const FText& InCrime)
{
	check(HasAuthority());
	Escort = Officer;
	BookingRelief = InBookingRelief;
	Crime = InCrime;
	NetLocation = GetActorLocation();
	NetYaw = GetActorRotation().Yaw;

	for (TActorIterator<AFTOCityGenerator> It(GetWorld()); It; ++It)
	{
		City = *It;
		break;
	}

	if (AFTOPlayerController* PC = Officer ? Cast<AFTOPlayerController>(Officer->GetController()) : nullptr)
	{
		PC->ClientToast(FText::Format(INVTEXT("Cuffed! Walk the suspect into the precinct's holding cells to book them ({0})."), Crime), FLinearColor(0.6f, 0.85f, 1.f));
	}
}

void AFTOArrestee::BeginPlay()
{
	Super::BeginPlay();
	// We take over from the perp mid-pose: kneeling, freshly cuffed (then up and off with the officer).
	if (UFTOCharacterAnimInstance* Anim = Cast<UFTOCharacterAnimInstance>(Body->GetAnimInstance()))
	{
		Anim->SnapToAction(EFTOAnimAction::Cuffed);
	}
}

bool AFTOArrestee::IsGettingUp() const
{
	return GetGameTimeSinceCreation() < GetUpSeconds;
}

EFTOAnimAction AFTOArrestee::GetAnimAction() const
{
	if (State == EFTOArresteeState::InCruiser || bJailed)
	{
		return EFTOAnimAction::SitCuffed;
	}
	return IsGettingUp() ? EFTOAnimAction::Cuffed : EFTOAnimAction::None;
}

EFTOAimPose AFTOArrestee::GetAimPose() const
{
	// On our feet, wrists cuffed behind the back (the sitting poses have their own).
	const bool bOnFoot = State == EFTOArresteeState::Escorted || (State == EFTOArresteeState::Booked && !bJailed);
	return bOnFoot && !IsGettingUp() ? EFTOAimPose::Cuffed : EFTOAimPose::None;
}

void AFTOArrestee::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		ServerTick(DeltaSeconds);
		NetLocation = GetActorLocation();
		NetYaw = GetActorRotation().Yaw;
	}
	else if (State == EFTOArresteeState::Escorted || State == EFTOArresteeState::Booked)
	{
		SetActorLocation(FMath::VInterpTo(GetActorLocation(), NetLocation, DeltaSeconds, 10.f));
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, NetYaw, 0.f), DeltaSeconds, 10.f));
	}

	// Keep the tag readable.
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			const FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - Tag->GetComponentLocation();
			Tag->SetWorldRotation(FRotator(0.f, ToCamera.Rotation().Yaw, 0.f));
		}
	}
}

FVector AFTOArrestee::FootstepOf(const AActor* Officer) const
{
	// The officer's capsule is a little taller than ours; stand on the same floor.
	return Officer->GetActorLocation() - FVector(0.f, 0.f, 96.f - HalfHeight);
}

void AFTOArrestee::ServerTick(float DeltaSeconds)
{
	if (State == EFTOArresteeState::Escaped || IsGettingUp())
	{
		return;
	}
	if (State == EFTOArresteeState::Booked)
	{
		WalkIntoCell(DeltaSeconds);
		return;
	}

	if (!IsValid(Escort))
	{
		// Arresting officer left the game: make a run for it.
		AloneTime += DeltaSeconds;
		AnimSpeed = 0.f;
		if (AloneTime > 5.f)
		{
			Escape();
		}
		return;
	}

	if (AActor* Vehicle = Escort->GetCurrentVehicle())
	{
		if (State != EFTOArresteeState::InCruiser)
		{
			// Hop in if we were right there; otherwise we've been left on the kerb.
			if (FVector::DistSquared2D(Vehicle->GetActorLocation(), GetActorLocation()) < FMath::Square(1500.f))
			{
				SetInCruiser(Vehicle);
			}
			else
			{
				AloneTime += DeltaSeconds;
				AnimSpeed = 0.f;
			}
		}
	}
	else
	{
		if (State == EFTOArresteeState::InCruiser)
		{
			// Officer got out: out we come too.
			LeaveCruiser();
		}

		FollowTrail(DeltaSeconds);
		AloneTime = FVector::DistSquared2D(Escort->GetActorLocation(), GetActorLocation()) > FMath::Square(2500.f) ? AloneTime + DeltaSeconds : 0.f;

		if (IsAtHoldingCells())
		{
			Book();
			return;
		}
	}

	if (AloneTime > EscapeAfter)
	{
		Escape();
	}
}

void AFTOArrestee::FollowTrail(float DeltaSeconds)
{
	// Drop a breadcrumb wherever the officer goes. A big jump is a teleport: catch up rather than walk it.
	const FVector Footstep = FootstepOf(Escort);
	if (!Trail.IsEmpty() && FVector::Dist(Trail.Last(), Footstep) > TrailJump)
	{
		Trail.Reset();
		SetActorLocation(Footstep - Escort->GetActorForwardVector() * FollowDistance);
	}
	if (Trail.IsEmpty() || FVector::Dist(Trail.Last(), Footstep) > TrailStep)
	{
		Trail.Add(Footstep);
	}
	if (Trail.Num() > 600)
	{
		Trail.RemoveAt(0, Trail.Num() - 600);
	}

	// How far we are from the officer, the way they went.
	float Remaining = FVector::Dist(GetActorLocation(), Trail[0]);
	for (int32 i = 0; i + 1 < Trail.Num(); ++i)
	{
		Remaining += FVector::Dist(Trail[i], Trail[i + 1]);
	}
	const float Gap = Remaining - FollowDistance;
	if (Gap <= 10.f)
	{
		AnimSpeed = 0.f;
		return;
	}

	const float Speed = FMath::Min(MaxSpeed, 120.f + Gap * 3.f);
	float Step = FMath::Min(Gap, Speed * DeltaSeconds);
	AnimSpeed = Speed;
	FVector Here = GetActorLocation();
	FVector Heading = FVector::ZeroVector;
	while (Step > 0.f && !Trail.IsEmpty())
	{
		const FVector ToNext = Trail[0] - Here;
		const float Length = ToNext.Size();
		if (Length > KINDA_SMALL_NUMBER)
		{
			Heading = ToNext / Length;
		}
		if (Length <= Step)
		{
			Here = Trail[0];
			Step -= Length;
			Trail.RemoveAt(0);
		}
		else
		{
			Here += Heading * Step;
			Step = 0.f;
		}
	}
	SetActorLocation(Here);
	if (!Heading.IsNearlyZero())
	{
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Heading.Rotation().Yaw, 0.f), DeltaSeconds, 8.f));
	}
}

bool AFTOArrestee::IsAtHoldingCells() const
{
	if (!City)
	{
		return false;
	}
	const FFTOBuilding* Precinct = City->FindBuilding(EFTOBuildingType::Precinct);
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	return Precinct && Precinct->Contains(Feet) && FVector::DistSquared2D(GetActorLocation(), City->GetHoldingCellsLocation()) < FMath::Square(CellRadius);
}

void AFTOArrestee::SetInCruiser(AActor* Cruiser)
{
	// Behind the passenger, unless someone's already sulking there.
	RideSeat = EFTOSeat::RearRight;
	for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->RideVehicle == Cruiser && It->RideSeat == EFTOSeat::RearRight)
		{
			RideSeat = EFTOSeat::RearLeft;
		}
	}
	RideVehicle = Cruiser;
	State = EFTOArresteeState::InCruiser;
	AnimSpeed = 0.f;
	Trail.Reset();
	OnRep_State();
}

void AFTOArrestee::LeaveCruiser()
{
	RideVehicle = nullptr;
	RideSeat = EFTOSeat::None;
	State = EFTOArresteeState::Escorted;
	OnRep_State();
	SetActorLocation(FootstepOf(Escort) - Escort->GetActorForwardVector() * FollowDistance);
	NetLocation = GetActorLocation();
	Trail.Reset();
}

void AFTOArrestee::ApplyRide()
{
	const AFTOCruiser* Cruiser = Cast<AFTOCruiser>(RideVehicle);
	const FName Socket = FTOSeats::SeatSocket(RideSeat);
	if (State == EFTOArresteeState::InCruiser && Cruiser && Socket != NAME_None)
	{
		AttachToComponent(Cruiser->GetSeatParent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
		SetActorRelativeLocation(FVector(0.f, 0.f, HalfHeight));
		SetActorRelativeRotation(FRotator::ZeroRotator);
	}
	else if (GetAttachParentActor())
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
	}
}

void AFTOArrestee::OnRep_State()
{
	ApplyRide();

	// Visible on foot, in the back seat and in the cell; the tag only on the way (it'd poke through the roof).
	Body->SetVisibility(State != EFTOArresteeState::Escaped);
	Tag->SetVisibility(State == EFTOArresteeState::Escorted);
}

void AFTOArrestee::Book()
{
	// Nothing to book them for: the desk sergeant lets them go, and nobody earns anything.
	if (bWrongful)
	{
		if (AFTOPlayerController* PC = Escort ? Cast<AFTOPlayerController>(Escort->GetController()) : nullptr)
		{
			PC->ClientToast(INVTEXT("The desk sergeant let them go: they hadn't done anything."), FLinearColor(1.f, 0.6f, 0.3f));
		}
		Destroy();
		return;
	}
	State = EFTOArresteeState::Booked;
	AnimSpeed = 0.f;
	Trail.Reset();
	OnRep_State();

	// A free place on a cell bench (or the longest-serving occupant gets taken off to court).
	const FFTOBuilding* Precinct = City ? City->FindBuilding(EFTOBuildingType::Precinct) : nullptr;
	if (Precinct && Precinct->CellSpots.Num() > 0)
	{
		TArray<AFTOArrestee*> Taken;
		Taken.SetNumZeroed(Precinct->CellSpots.Num());
		for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
		{
			if (*It != this && It->State == EFTOArresteeState::Booked && Taken.IsValidIndex(It->CellIndex))
			{
				Taken[It->CellIndex] = *It;
			}
		}
		CellIndex = Taken.IndexOfByKey(nullptr);
		if (CellIndex == INDEX_NONE)
		{
			CellIndex = 0;
			float Oldest = TNumericLimits<float>::Max();
			for (int32 i = 0; i < Taken.Num(); ++i)
			{
				if (Taken[i]->GetLifeSpan() < Oldest)
				{
					Oldest = Taken[i]->GetLifeSpan();
					CellIndex = i;
				}
			}
			Taken[CellIndex]->Destroy();
		}

		// Up to the bars, through the cell door, onto the bench.
		const FTransform& Door = Precinct->CellDoors[CellIndex];
		const FVector Into = Door.GetRotation().GetForwardVector();
		const FVector Up(0.f, 0.f, HalfHeight);
		CellPath = { Door.GetLocation() - Into * 110.f + Up, Door.GetLocation() + Into * 90.f + Up, Precinct->CellSpots[CellIndex].GetLocation() + Up };
		CellYaw = Precinct->CellSpots[CellIndex].Rotator().Yaw;
	}

	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->AddChaos(-BookingRelief);
		++GS->SuspectsBooked;
		GS->MulticastPlaySound(AFTOGameState::Sounds().Chime, GetActorLocation(), 1.f);
	}
	if (AFTOPlayerController* PC = Escort ? Cast<AFTOPlayerController>(Escort->GetController()) : nullptr)
	{
		PC->ClientToast(FText::Format(INVTEXT("Booked! {0}. The city breathes a little easier."), Crime), FLinearColor(0.4f, 1.f, 0.5f));
	}
	FTOScoring::Award(Escort, EFTOScore::Booked, GetActorLocation() + FVector(0.f, 0.f, 120.f));
	UE_LOG(LogFTO, Log, TEXT("Suspect booked: %s (cell place %d)"), *Crime.ToString(), CellIndex);
	SetLifeSpan(CellTime);
}

void AFTOArrestee::WalkIntoCell(float DeltaSeconds)
{
	if (bJailed)
	{
		return;
	}
	if (CellPath.IsEmpty())
	{
		// Sat down on the bench.
		bJailed = true;
		AnimSpeed = 0.f;
		SetActorRotation(FRotator(0.f, CellYaw, 0.f));
		return;
	}
	const FVector ToNext = CellPath[0] - GetActorLocation();
	const float Step = CellWalkSpeed * DeltaSeconds;
	AnimSpeed = CellWalkSpeed;
	if (ToNext.Size() <= Step)
	{
		SetActorLocation(CellPath[0]);
		CellPath.RemoveAt(0);
	}
	else
	{
		SetActorLocation(GetActorLocation() + ToNext.GetSafeNormal() * Step);
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, ToNext.Rotation().Yaw, 0.f), DeltaSeconds, 8.f));
	}
}

void AFTOArrestee::Escape()
{
	State = EFTOArresteeState::Escaped;
	OnRep_State();

	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->AddChaos(2.f);
	}
	if (AFTOPlayerController* PC = Escort ? Cast<AFTOPlayerController>(Escort->GetController()) : nullptr)
	{
		PC->ClientToast(INVTEXT("Your suspect slipped their cuffs and ran off!"), FLinearColor(1.f, 0.5f, 0.3f));
	}
	SetLifeSpan(1.f);
}
