#include "Crime/FTOArrestee.h"
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
#include "UObject/ConstructorHelpers.h"
#include "FTO.h"

namespace
{
	constexpr float HalfHeight = 92.f;
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
	DOREPLIFETIME(AFTOArrestee, NetLocation);
	DOREPLIFETIME(AFTOArrestee, NetYaw);
	DOREPLIFETIME(AFTOArrestee, AnimSpeed);
	DOREPLIFETIME(AFTOArrestee, Crime);
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
		PrecinctLocation = It->GetPrecinctLocation();
		break;
	}

	if (AFTOPlayerController* PC = Officer ? Cast<AFTOPlayerController>(Officer->GetController()) : nullptr)
	{
		PC->ClientToast(FText::Format(INVTEXT("Cuffed! Bring the suspect to the precinct to book them ({0})."), Crime), FLinearColor(0.6f, 0.85f, 1.f));
	}
}

EFTOAnimAction AFTOArrestee::GetAnimAction() const
{
	return AnimSpeed < 20.f ? EFTOAnimAction::Cheer : EFTOAnimAction::None;
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
	else if (State == EFTOArresteeState::Escorted)
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

void AFTOArrestee::ServerTick(float DeltaSeconds)
{
	if (State == EFTOArresteeState::Booked || State == EFTOArresteeState::Escaped)
	{
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
			}
		}
		if (State == EFTOArresteeState::InCruiser && FVector::DistSquared2D(Vehicle->GetActorLocation(), PrecinctLocation) < FMath::Square(BookingRadius))
		{
			Book();
			return;
		}
	}
	else
	{
		if (State == EFTOArresteeState::InCruiser)
		{
			// Officer got out: out we come too.
			DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			SetActorLocation(Escort->GetActorLocation() - Escort->GetActorForwardVector() * FollowDistance);
			State = EFTOArresteeState::Escorted;
			OnRep_State();
		}

		// Trot along behind the officer.
		const FVector Target = Escort->GetActorLocation() - Escort->GetActorForwardVector() * FollowDistance - FVector(0.f, 0.f, 96.f - HalfHeight);
		const FVector ToTarget = Target - GetActorLocation();
		const float Distance = ToTarget.Size2D();
		const float Speed = Distance > 30.f ? FMath::Min(MaxSpeed, Distance * 3.f) : 0.f;
		AnimSpeed = Speed;
		if (Speed > 0.f)
		{
			const FVector Step = ToTarget.GetSafeNormal() * FMath::Min(Distance, Speed * DeltaSeconds);
			SetActorLocation(GetActorLocation() + Step);
			SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, ToTarget.Rotation().Yaw, 0.f), DeltaSeconds, 8.f));
		}

		AloneTime = Distance > 2500.f ? AloneTime + DeltaSeconds : 0.f;

		if (FVector::DistSquared2D(Escort->GetActorLocation(), PrecinctLocation) < FMath::Square(BookingRadius))
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

void AFTOArrestee::SetInCruiser(AActor* Cruiser)
{
	State = EFTOArresteeState::InCruiser;
	AnimSpeed = 0.f;
	AttachToActor(Cruiser, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	OnRep_State();
}

void AFTOArrestee::OnRep_State()
{
	const bool bVisible = State == EFTOArresteeState::Escorted;
	Body->SetVisibility(bVisible);
	Tag->SetVisibility(bVisible);
}

void AFTOArrestee::Book()
{
	State = EFTOArresteeState::Booked;
	OnRep_State();

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
	UE_LOG(LogFTO, Log, TEXT("Suspect booked: %s"), *Crime.ToString());
	SetLifeSpan(1.f);
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
