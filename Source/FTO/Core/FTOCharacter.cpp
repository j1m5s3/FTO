#include "Core/FTOCharacter.h"
#include "Core/FTOInputConfig.h"
#include "Core/FTOPlayerController.h"
#include "Core/FTOPlayerState.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Interaction/FTOInteractable.h"
#include "Engine/OverlapResult.h"
#include "Physics/FTOKnockdownComponent.h"
#include "City/FTOPedestrian.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Animation/FTOCharacterAnimInstance.h"
#include "Art/FTOArt.h"
#include "Core/FTOGameState.h"
#include "Crime/FTOIncident.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Vehicles/FTOCruiser.h"
#include "FTO.h"

AFTOCharacter::AFTOCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->bOrientRotationToMovement = true;
	MoveComp->RotationRate = FRotator(0.f, 720.f, 0.f);
	MoveComp->JumpZVelocity = 550.f;
	MoveComp->AirControl = 0.4f;
	MoveComp->MaxWalkSpeed = WalkSpeed;
	MoveComp->BrakingDecelerationWalking = 2000.f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 550.f;
	CameraBoom->SocketOffset = FVector(0.f, 60.f, 120.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 12.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	Knockdown = CreateDefaultSubobject<UFTOKnockdownComponent>(TEXT("Knockdown"));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMat(FTOArt::BaseMaterialPath);
	BaseMaterial = BaseMat.Object;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> OfficerMesh(TEXT("/Game/FTO/Characters/Officer/SK_Officer.SK_Officer"));
	if (OfficerMesh.Succeeded())
	{
		// The Blender model faces +Y with feet at its origin.
		USkeletalMeshComponent* Body = GetMesh();
		Body->SetSkeletalMeshAsset(OfficerMesh.Object);
		Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()), FRotator(0.f, -90.f, 0.f));
		Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Body->SetAnimInstanceClass(UFTOCharacterAnimInstance::StaticClass());
		Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	}
	else
	{
		// Bean cop placeholder built from engine basic shapes.
		static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

		auto MakePiece = [this](FName Name, UStaticMesh* PieceMesh, const FVector& Loc, const FVector& Scale) -> UStaticMeshComponent*
		{
			UStaticMeshComponent* Piece = CreateDefaultSubobject<UStaticMeshComponent>(Name);
			Piece->SetupAttachment(RootComponent);
			Piece->SetStaticMesh(PieceMesh);
			Piece->SetRelativeLocation(Loc);
			Piece->SetRelativeScale3D(Scale);
			Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Piece->SetGenerateOverlapEvents(false);
			return Piece;
		};

		BodyMesh = MakePiece(TEXT("BodyMesh"), CylinderMesh.Object, FVector(0.f, 0.f, -25.f), FVector(0.8f, 0.8f, 1.3f));
		HeadMesh = MakePiece(TEXT("HeadMesh"), SphereMesh.Object,   FVector(0.f, 0.f, 60.f),  FVector(0.75f));
		CapMesh  = MakePiece(TEXT("CapMesh"),  CylinderMesh.Object, FVector(8.f, 0.f, 95.f),  FVector(0.8f, 0.8f, 0.2f));
		GetMesh()->SetVisibility(false);
	}

	PrimaryActorTick.bCanEverTick = true;
}

void AFTOCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AFTOCharacter, bSprinting, COND_SkipOwner);
	DOREPLIFETIME(AFTOCharacter, TimedAction);
	DOREPLIFETIME(AFTOCharacter, CurrentVehicle);
	DOREPLIFETIME(AFTOCharacter, CurrentSeat);
	DOREPLIFETIME(AFTOCharacter, TimedActionEnd);
}

void AFTOCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetController());
	if (!EIC || !PC)
	{
		UE_LOG(LogFTO, Error, TEXT("%s: needs an EnhancedInputComponent and AFTOPlayerController."), *GetNameSafe(this));
		return;
	}

	UFTOInputConfig* Input = PC->GetInputConfig();
	EIC->BindAction(Input->Move, ETriggerEvent::Triggered, this, &AFTOCharacter::Move);
	EIC->BindAction(Input->Look, ETriggerEvent::Triggered, this, &AFTOCharacter::Look);
	EIC->BindAction(Input->Jump, ETriggerEvent::Started, this, &ACharacter::Jump);
	EIC->BindAction(Input->Jump, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	EIC->BindAction(Input->Sprint, ETriggerEvent::Started, this, &AFTOCharacter::SprintStarted);
	EIC->BindAction(Input->Sprint, ETriggerEvent::Completed, this, &AFTOCharacter::SprintStopped);
	EIC->BindAction(Input->Interact, ETriggerEvent::Started, this, &AFTOCharacter::InteractPressed);
	EIC->BindAction(Input->Interact, ETriggerEvent::Completed, this, &AFTOCharacter::InteractReleased);
	EIC->BindAction(Input->Whistle, ETriggerEvent::Started, this, &AFTOCharacter::WhistlePressed);
	EIC->BindAction(Input->Camera, ETriggerEvent::Started, this, &AFTOCharacter::ToggleCamera);
}

void AFTOCharacter::BeginPlay()
{
	Super::BeginPlay();

	Knockdown->OnKnockedDown.AddUObject(this, &AFTOCharacter::HandleKnockedDown);
	Knockdown->OnRecovered.AddUObject(this, &AFTOCharacter::HandleRecovered);

	if (CapMesh)
	{
		FTOArt::ApplyColor(CapMesh, BaseMaterial, FLinearColor(0.02f, 0.03f, 0.09f));
	}
	RefreshOfficerColor();
}

void AFTOCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	RefreshOfficerColor();
}

void AFTOCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	RefreshOfficerColor();
}

void AFTOCharacter::RefreshOfficerColor()
{
	const AFTOPlayerState* PS = GetPlayerState<AFTOPlayerState>();
	if (!PS)
	{
		return;
	}

	// Real model: vertex alpha marks the shirt, so one tint colours just the uniform.
	// Placeholder: the body cylinder is the uniform.
	UPrimitiveComponent* UniformTarget = BodyMesh ? static_cast<UPrimitiveComponent*>(BodyMesh) : GetMesh();
	if (!UniformMaterial)
	{
		UniformMaterial = FTOArt::ApplyColor(UniformTarget, BaseMaterial, PS->GetOfficerColor());
		// Imported meshes can carry several (identical) slots; they all share the one tint.
		for (int32 Slot = 1; Slot < UniformTarget->GetNumMaterials(); ++Slot)
		{
			UniformTarget->SetMaterial(Slot, UniformMaterial);
		}
	}
	FTOArt::SetColor(UniformMaterial, PS->GetOfficerColor());

	if (HeadMesh && !HeadMaterial)
	{
		HeadMaterial = FTOArt::ApplyColor(HeadMesh, BaseMaterial, FTOArt::SkinTone(PS->GetBadgeIndex()));
	}
}

void AFTOCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	if (!Controller)
	{
		return;
	}

	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Axis.X);
}

void AFTOCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void AFTOCharacter::SprintStarted()
{
	bSprinting = true;
	ApplySprint();
	ServerSetSprinting(true);
}

void AFTOCharacter::SprintStopped()
{
	bSprinting = false;
	ApplySprint();
	ServerSetSprinting(false);
}

void AFTOCharacter::ServerSetSprinting_Implementation(bool bNewSprinting)
{
	bSprinting = bNewSprinting;
	ApplySprint();
}

void AFTOCharacter::OnRep_Sprinting()
{
	ApplySprint();
}

void AFTOCharacter::ApplySprint()
{
	GetCharacterMovement()->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed;
}

void AFTOCharacter::InteractReleased() {}
void AFTOCharacter::WhistlePressed()
{
	if (CurrentVehicle)
	{
		ServerToggleVehicleSiren(); // riding shotgun: work the lights instead
		return;
	}
	ServerWhistle();
}

void AFTOCharacter::ServerToggleVehicleSiren_Implementation()
{
	if (AFTOCruiser* Cruiser = Cast<AFTOCruiser>(CurrentVehicle))
	{
		Cruiser->SetSiren(!Cruiser->IsSirenOn());
	}
}

void AFTOCharacter::ServerLeaveVehicle_Implementation()
{
	if (AFTOCruiser* Cruiser = Cast<AFTOCruiser>(CurrentVehicle))
	{
		Cruiser->LetOut(this);
	}
}

void AFTOCharacter::ServerWhistle_Implementation()
{
	// FWEEEET! Everyone nearby stops and looks, and anything shady nearby gets called in.
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextWhistleTime)
	{
		return;
	}
	NextWhistleTime = Now + 2.f;

	AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
	if (GS)
	{
		GS->MulticastPlaySound(AFTOGameState::Sounds().Whistle, GetActorLocation(), 1.f);
	}
	PlayTimedAction(EFTOAnimAction::Cheer, 0.6f); // arm up, whistle in mouth

	for (TActorIterator<AFTOPedestrian> It(GetWorld()); It; ++It)
	{
		if (FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation()) < FMath::Square(WhistleRadius))
		{
			It->FreezeFor(this, 2.5f);
		}
	}
	if (GS)
	{
		for (AFTOIncident* Incident : GS->GetIncidents())
		{
			if (Incident && Incident->GetState() == EFTOIncidentState::Unreported &&
				FVector::DistSquared2D(Incident->GetActorLocation(), GetActorLocation()) < FMath::Square(WhistleRadius))
			{
				Incident->ReportByOfficer();
			}
		}
	}
}

void AFTOCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocallyControlled() && !CurrentVehicle)
	{
		FocusAccumulator += DeltaSeconds;
		if (FocusAccumulator >= 0.1f)
		{
			FocusAccumulator = 0.f;
			UpdateFocus();
		}
	}
	else
	{
		FocusedInteractable.Reset();
	}

	// Riding along, the view turns with the car so "ahead" stays ahead.
	if (IsLocallyControlled() && CurrentVehicle && Controller)
	{
		const float VehicleYaw = CurrentVehicle->GetActorRotation().Yaw;
		if (bTrackVehicleYaw)
		{
			Controller->SetControlRotation(Controller->GetControlRotation() + FRotator(0.f, FMath::FindDeltaAngleDegrees(LastVehicleYaw, VehicleYaw), 0.f));
		}
		LastVehicleYaw = VehicleYaw;
		bTrackVehicleYaw = true;
	}
	else
	{
		bTrackVehicleYaw = false;
	}
}

void AFTOCharacter::UpdateFocus()
{
	FocusedInteractable.Reset();

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOInteractFocus), false, this);

	if (!GetWorld()->OverlapMultiByObjectType(Overlaps, GetActorLocation(), FQuat::Identity, Objects, FCollisionShape::MakeSphere(800.f), Params))
	{
		return;
	}

	// Prefer what the officer is facing, then what's closest.
	const FVector Facing = GetActorForwardVector();
	float BestScore = TNumericLimits<float>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		const IFTOInteractable* Interactable = Cast<IFTOInteractable>(Actor);
		if (!Interactable || !Interactable->CanInteract(this))
		{
			continue;
		}

		const FVector ToTarget = Interactable->GetInteractLocation() - GetActorLocation();
		const float Distance = ToTarget.Size2D();
		if (Distance > Interactable->GetInteractRange())
		{
			continue;
		}

		const float FacingDot = FVector::DotProduct(Facing, ToTarget.GetSafeNormal2D());
		const float Score = Distance * (1.5f - FacingDot);
		if (Score < BestScore)
		{
			BestScore = Score;
			FocusedInteractable = Actor;
		}
	}
}

void AFTOCharacter::InteractPressed()
{
	if (CurrentVehicle)
	{
		ServerLeaveVehicle();
		return;
	}
	UpdateFocus();
	if (AActor* Target = FocusedInteractable.Get())
	{
		ServerInteract(Target);
	}
}

void AFTOCharacter::ServerInteract_Implementation(AActor* Target)
{
	IFTOInteractable* Interactable = Cast<IFTOInteractable>(Target);
	if (!Interactable || !Interactable->CanInteract(this))
	{
		return;
	}

	// Generous tolerance for latency, but no interacting from across the map.
	const float Distance = FVector::Dist2D(Interactable->GetInteractLocation(), GetActorLocation());
	if (Distance <= Interactable->GetInteractRange() + 250.f)
	{
		Interactable->Interact(this);
	}
}

void AFTOCharacter::PlayTimedAction(EFTOAnimAction Action, float Duration)
{
	check(HasAuthority());
	TimedAction = Action;
	TimedActionEnd = GetWorld()->GetTimeSeconds() + Duration;
}

EFTOAnimAction AFTOCharacter::GetAnimAction() const
{
	if (CurrentVehicle && CurrentSeat != EFTOSeat::None)
	{
		return FTOSeats::RidingPose(CurrentSeat);
	}

	const UWorld* World = GetWorld();
	const AFTOGameState* GS = World ? World->GetGameState<AFTOGameState>() : nullptr;

	if (GS && GS->GetShiftPhase() == EFTOShiftPhase::Survived)
	{
		return EFTOAnimAction::Cheer;
	}

	const float Now = GS ? GS->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.f);
	if (TimedAction != EFTOAnimAction::None && Now < TimedActionEnd)
	{
		return TimedAction;
	}

	// Standing still at a live scene: take notes, look busy.
	if (GS && !IsAnimAirborne() && GetAnimSpeed() < 40.f)
	{
		for (const AFTOIncident* Incident : GS->GetIncidents())
		{
			if (Incident && Incident->IsActive() &&
				FVector::DistSquared2D(Incident->GetActorLocation(), GetActorLocation()) <= FMath::Square(Incident->GetSceneRadius()))
			{
				return EFTOAnimAction::Interact;
			}
		}
	}
	return EFTOAnimAction::None;
}

bool AFTOCharacter::IsAnimAirborne() const
{
	return GetCharacterMovement() && GetCharacterMovement()->IsFalling();
}

float AFTOCharacter::GetAnimSpeed() const
{
	return GetVelocity().Size2D();
}

void AFTOCharacter::EnterVehicle(AActor* Vehicle, EFTOSeat Seat)
{
	check(HasAuthority());
	CurrentVehicle = Vehicle;
	CurrentSeat = Seat;
	ApplyVehicleState();
}

void AFTOCharacter::ExitVehicle(const FVector& Location, float Yaw)
{
	check(HasAuthority());
	CurrentVehicle = nullptr;
	CurrentSeat = EFTOSeat::None;
	ApplyVehicleState();
	TeleportTo(Location, FRotator(0.f, Yaw, 0.f));

	// A remote officer who stayed in control (riding shotgun) is told directly; a driver gets
	// re-possessed by the cruiser, which restarts them at the right spot anyway.
	if (GetController() && !IsLocallyControlled())
	{
		ClientExitedVehicle(Location, Yaw);
	}
}

void AFTOCharacter::ClientExitedVehicle_Implementation(FVector_NetQuantize Location, float Yaw)
{
	CurrentVehicle = nullptr;
	CurrentSeat = EFTOSeat::None;
	ApplyVehicleState();
	TeleportTo(Location, FRotator(0.f, Yaw, 0.f));
}

void AFTOCharacter::OnRep_CurrentVehicle()
{
	ApplyVehicleState();
}

void AFTOCharacter::ApplyVehicleState()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	const AFTOCruiser* Cruiser = Cast<AFTOCruiser>(CurrentVehicle);
	USceneComponent* SeatParent = Cruiser ? Cruiser->GetSeatParent() : nullptr;
	const FName SeatSocket = FTOSeats::SeatSocket(CurrentSeat);

	SetActorHiddenInGame(false);
	if (SeatParent && SeatSocket != NAME_None)
	{
		// Sit in the seat (visible through the glass) and ride along with the car. Movement is
		// switched off entirely so nothing fights the attachment.
		SetActorEnableCollision(false);
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
		MoveComp->SetComponentTickEnabled(false);
		GetMesh()->SetRelativeLocationAndRotation(GetBaseTranslationOffset(), GetBaseRotationOffset());
		AttachToComponent(SeatParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SeatSocket);
		SetActorRelativeLocation(FVector(0.f, 0.f, GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
		SetActorRelativeRotation(FRotator::ZeroRotator);

		if (IsLocallyControlled() && Controller)
		{
			Controller->SetControlRotation(FRotator(-10.f, CurrentVehicle->GetActorRotation().Yaw, 0.f));
		}
		bSeated = true;
	}
	else if (bSeated)
	{
		// (Attachment replication may already have detached us on a client.)
		if (GetAttachParentActor())
		{
			DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
		SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
		SetActorEnableCollision(true);
		MoveComp->SetComponentTickEnabled(true);
		MoveComp->SetMovementMode(MOVE_Walking);
		bSeated = false;
	}
	ApplyCameraMode();
}

void AFTOCharacter::ToggleCamera()
{
	AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetController());
	if (PC && CurrentVehicle)
	{
		PC->bPreferInteriorView = !PC->bPreferInteriorView;
		ApplyCameraMode();
	}
}

void AFTOCharacter::ApplyCameraMode()
{
	// A driver looks through the cruiser's cameras (it hides their head for its seat view), so
	// this only ever takes over for passengers.
	const AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetController());
	const bool bRiding = CurrentVehicle != nullptr;
	const bool bPassenger = bRiding && CurrentSeat != EFTOSeat::Driver;
	const bool bInterior = bPassenger && PC && PC->IsLocalController() && PC->bPreferInteriorView;

	// On foot: over the shoulder. Riding along: pulled back to take in the car, or from the seat.
	CameraBoom->bDoCollisionTest = !bRiding;
	CameraBoom->bEnableCameraLag = !bInterior;
	CameraBoom->TargetArmLength = bInterior ? 0.f : (bRiding ? 850.f : 550.f);
	CameraBoom->SocketOffset = bInterior ? FVector::ZeroVector : (bRiding ? FVector(0.f, 0.f, 150.f) : FVector(0.f, 60.f, 120.f));
	CameraBoom->SetRelativeLocation(bInterior ? FTOSeats::EyeOffset(GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()) : FVector::ZeroVector);

	if (IsLocallyControlled() && CurrentSeat != EFTOSeat::Driver)
	{
		SetHeadHidden(bInterior);
	}
}

void AFTOCharacter::SetHeadHidden(bool bHide)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body || !Body->GetSkeletalMeshAsset())
	{
		return;
	}
	if (bHide)
	{
		Body->HideBoneByName(TEXT("head"), PBO_None);
	}
	else
	{
		Body->UnHideBoneByName(TEXT("head"));
	}
}

void AFTOCharacter::HandleKnockedDown()
{
	GetCharacterMovement()->DisableMovement();
}

void AFTOCharacter::HandleRecovered()
{
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}
