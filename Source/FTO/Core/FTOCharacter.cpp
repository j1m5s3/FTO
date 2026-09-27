#include "Core/FTOCharacter.h"
#include "Audio/FTOFootsteps.h"
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
#include "Physics/FTOImpact.h"
#include "Weapons/FTOBallistics.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "City/FTOPedestrian.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Animation/FTOCharacterAnimInstance.h"
#include "AnimationRuntime.h"
#include "Art/FTOArt.h"
#include "Core/FTOGameState.h"
#include "Crime/FTOIncident.h"
#include "Crime/FTOPerp.h"
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
	Footsteps = CreateDefaultSubobject<UFTOFootsteps>(TEXT("Footsteps"));

	// The weapon in hand (or on the hip, or slung): placed in world space every frame (see UpdateWeaponMesh).
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(RootComponent);
	WeaponMesh->SetUsingAbsoluteLocation(true);
	WeaponMesh->SetUsingAbsoluteRotation(true);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetCastShadow(false);
	WeaponMesh->SetVisibility(false);

	// Standard issue: a taser in the first slot; the armory hands out the rest.
	Loadout = { EFTOWeapon::Taser, EFTOWeapon::None, EFTOWeapon::None };
	const FFTOWeaponSpec& Taser = FTOWeapons::Spec(EFTOWeapon::Taser);
	Clips = { Taser.Magazine, 0, 0 };
	Spares = { Taser.Magazine * Taser.SpareMagazines, 0, 0 };

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
	DOREPLIFETIME(AFTOCharacter, Loadout);
	DOREPLIFETIME_CONDITION(AFTOCharacter, Clips, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AFTOCharacter, Spares, COND_OwnerOnly);
	DOREPLIFETIME(AFTOCharacter, DrawnSlot);
	DOREPLIFETIME(AFTOCharacter, ReloadEnd);
	DOREPLIFETIME(AFTOCharacter, bDowned);
	DOREPLIFETIME(AFTOCharacter, SyncedAction);
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
	EIC->BindAction(Input->Tackle, ETriggerEvent::Started, this, &AFTOCharacter::TacklePressed);
	EIC->BindAction(Input->Draw, ETriggerEvent::Started, this, &AFTOCharacter::DrawPressed);
	EIC->BindAction(Input->Fire, ETriggerEvent::Started, this, &AFTOCharacter::FirePressed);
	EIC->BindAction(Input->Reload, ETriggerEvent::Started, this, &AFTOCharacter::ReloadPressed);
	EIC->BindAction(Input->NextWeapon, ETriggerEvent::Started, this, &AFTOCharacter::NextWeaponPressed);
	EIC->BindAction(Input->PrevWeapon, ETriggerEvent::Started, this, &AFTOCharacter::PrevWeaponPressed);
	for (int32 Slot = 0; Slot < Input->Slots.Num(); ++Slot)
	{
		EIC->BindAction(Input->Slots[Slot], ETriggerEvent::Started, this, &AFTOCharacter::SelectSlot, Slot);
	}
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
	if (!Controller || (Knockdown && Knockdown->IsDazed()) || IsInSyncedAction())
	{
		return; // seeing stars, or busy cuffing someone: sit tight a moment
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
	// With a weapon up it's a careful walk, sprint or no sprint.
	GetCharacterMovement()->MaxWalkSpeed = GetDrawnWeapon() != EFTOWeapon::None ? DrawnWalkSpeed : (bSprinting ? SprintSpeed : WalkSpeed);
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

bool AFTOCharacter::CanTackle() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	return !CurrentVehicle && GetWorld()->GetTimeSeconds() >= NextTackleTime && Movement && Movement->IsMovingOnGround() &&
		!(Knockdown && (Knockdown->IsDown() || Knockdown->IsDazed()));
}

void AFTOCharacter::TacklePressed()
{
	if (!CanTackle())
	{
		return;
	}
	if (!HasAuthority())
	{
		// Predict the dive here so it's instant; the server does it for real and decides who gets flattened.
		NextTackleTime = GetWorld()->GetTimeSeconds() + TackleCooldown;
		LaunchTackle();
	}
	ServerTackle();
}

void AFTOCharacter::LaunchTackle()
{
	// Low and fast, with a little hop so the dive clears the kerb.
	LaunchCharacter(GetActorForwardVector() * 950.f + FVector(0.f, 0.f, 220.f), true, true);
}

void AFTOCharacter::ServerTackle_Implementation()
{
	if (!CanTackle())
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	NextTackleTime = Now + TackleCooldown;
	LaunchTackle();
	PlayTimedAction(EFTOAnimAction::Tackle, 0.75f);
	TackleReachUntil = Now + 0.45f;
	GetWorldTimerManager().SetTimer(TackleTimer, this, &AFTOCharacter::CheckTackle, 0.05f, true, 0.05f);
}

void AFTOCharacter::CheckTackle()
{
	if (GetWorld()->GetTimeSeconds() > TackleReachUntil)
	{
		GetWorldTimerManager().ClearTimer(TackleTimer);
		return;
	}
	// Whoever's just ahead of the dive goes down (one per tackle).
	const FVector Fwd = GetActorForwardVector();
	TArray<FOverlapResult> InReach;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOTackle), false, this);
	if (GetWorld()->OverlapMultiByObjectType(InReach, GetActorLocation() + Fwd * 90.f, GetActorQuat(), FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeCapsule(60.f, 90.f), Params))
	{
		for (const FOverlapResult& Overlap : InReach)
		{
			if (Overlap.GetActor() != this && FTOImpact::Tackle(Overlap.GetActor(), Fwd, GetController()))
			{
				GetWorldTimerManager().ClearTimer(TackleTimer);
				return;
			}
		}
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

	// (Mid-arrest, E is spoken for: no prompts for anything else.)
	if (IsLocallyControlled() && !CurrentVehicle && !IsInSyncedAction())
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

	TickSyncedAction();
	UpdateWeaponMesh();
	if (IsLocallyControlled())
	{
		UpdateAimCamera(DeltaSeconds);
	}
}

bool AFTOCharacter::IsWallBetween(const FVector& From, const FVector& To, const AActor* Target) const
{
	// Walls, windows and cell bars make a room a room; counters and tables don't stop a conversation.
	auto IsWall = [](const UPrimitiveComponent* Component)
	{
		const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component);
		const FString Name = Mesh && Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetName() : FString();
		return Name.StartsWith(TEXT("SM_Wall")) || Name.StartsWith(TEXT("SM_IWall")) || Name.StartsWith(TEXT("SM_Corner")) ||
			Name.StartsWith(TEXT("SM_VaultWall")) || Name.StartsWith(TEXT("SM_CellBars"));
	};

	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOWallBetween), false, this);
	Params.AddIgnoredActor(Target);
	for (int32 Tries = 0; Tries < 4; ++Tries)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByObjectType(Hit, From, To, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			return false;
		}
		if (IsWall(Hit.GetComponent()))
		{
			return true;
		}
		Params.AddIgnoredComponent(Hit.GetComponent());
	}
	return false;
}

void AFTOCharacter::UpdateFocus()
{
	FocusedInteractable.Reset();

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody); // a downed partner, lying ragdolled
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

		// Not through walls (or shop windows): the clerk on the other side of the wall can't hear you.
		if (IsWallBetween(GetActorLocation() + FVector(0.f, 0.f, 50.f), Interactable->GetInteractLocation() + FVector(0.f, 0.f, 40.f), Actor))
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
	if (IsInSyncedAction())
	{
		// Wrestling a suspect: every press is a heave (cuffing just takes a moment).
		if (SyncedAction.Action == EFTOAnimAction::Struggle)
		{
			ServerMash();
		}
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

	// Lined up for the scoreboard (stars or no stars): the squad dances if the city made it, and slumps if it didn't.
	if (GS && GS->GetShiftPhase() == EFTOShiftPhase::Survived)
	{
		return EFTOAnimAction::Dance;
	}
	if (GS && GS->GetShiftPhase() == EFTOShiftPhase::Overrun)
	{
		return EFTOAnimAction::Slump;
	}

	if (Knockdown && Knockdown->IsDazed())
	{
		return EFTOAnimAction::Dazed;
	}
	if (IsInSyncedAction())
	{
		return SyncedAction.Action;
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
	DrawnSlot = INDEX_NONE; // weapons away in the car
	ReloadEnd = 0.f;
	GetWorldTimerManager().ClearTimer(ReloadTimer);
	OnRep_Loadout();
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
	if (HasAuthority())
	{
		// Down, the weapon goes away (and stays away until they draw it again), and whatever move they were in
		// the middle of is off.
		DrawnSlot = INDEX_NONE;
		OnRep_Loadout();
		EndSyncedAction();
	}
}

void AFTOCharacter::HandleRecovered()
{
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	if (HasAuthority())
	{
		bDowned = false;
	}
}

// ------------------------------------------------------------------------------------------
// Weapons
// ------------------------------------------------------------------------------------------

bool AFTOCharacter::IsReloading() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const float Now = GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	return ReloadEnd > 0.f && Now < ReloadEnd;
}

void AFTOCharacter::GiveWeapon(EFTOWeapon Weapon)
{
	check(HasAuthority());
	if (Weapon == EFTOWeapon::None)
	{
		return;
	}
	// Already carrying one: a restock. Otherwise the first free slot, or swap it for the one in hand.
	int32 Slot = Loadout.IndexOfByKey(Weapon);
	if (Slot == INDEX_NONE)
	{
		Slot = Loadout.IndexOfByKey(EFTOWeapon::None);
	}
	if (Slot == INDEX_NONE)
	{
		Slot = Loadout.IsValidIndex(DrawnSlot) ? DrawnSlot : FTOWeapons::MaxSlots - 1;
	}
	const FFTOWeaponSpec& Spec = FTOWeapons::Spec(Weapon);
	Loadout[Slot] = Weapon;
	Clips[Slot] = Spec.Magazine;
	Spares[Slot] = Spec.Magazine * Spec.SpareMagazines;
	DrawnSlot = Slot;
	LastDrawnSlot = Slot;
	ReloadEnd = 0.f;
	GetWorldTimerManager().ClearTimer(ReloadTimer);
	OnRep_Loadout();
}

void AFTOCharacter::OnRep_Loadout()
{
	if (Loadout.IsValidIndex(DrawnSlot))
	{
		LastDrawnSlot = DrawnSlot;
	}
	ApplyWeaponStance();
	UpdateWeaponMesh();
}

void AFTOCharacter::ApplyWeaponStance()
{
	// Weapon up: face where the camera looks and move at a careful walk. Put away: turn with movement again.
	const bool bDrawn = GetDrawnWeapon() != EFTOWeapon::None && !CurrentVehicle;
	bUseControllerRotationYaw = bDrawn;
	GetCharacterMovement()->bOrientRotationToMovement = !bDrawn;
	ApplySprint();
}

EFTOAimPose AFTOCharacter::GetAimPose() const
{
	if (CurrentVehicle || (Knockdown && (Knockdown->IsDown() || Knockdown->IsDazed())))
	{
		return EFTOAimPose::None;
	}
	const EFTOWeapon Weapon = GetDrawnWeapon();
	return Weapon == EFTOWeapon::None ? EFTOAimPose::None : FTOWeapons::Spec(Weapon).Pose;
}

FRotator AFTOCharacter::GetAimRotation() const
{
	// Where the controller points (the base aim rotation would follow whatever camera is being looked through);
	// everyone else's copy of us has no controller and goes by the replicated view pitch.
	return Controller ? Controller->GetControlRotation() : GetBaseAimRotation();
}

float AFTOCharacter::GetAimPitch() const
{
	return FMath::Clamp(FRotator::NormalizeAxis(GetAimRotation().Pitch), -60.f, 60.f);
}

void AFTOCharacter::UpdateWeaponMesh()
{
	// In hand when drawn; otherwise the last one drawn rides on the hip (sidearms) or across the back (long guns).
	const bool bDrawn = GetAimPose() != EFTOAimPose::None;
	EFTOWeapon Shown = bDrawn ? GetDrawnWeapon() : GetWeaponInSlot(LastDrawnSlot);
	if (Shown == EFTOWeapon::None)
	{
		Shown = GetWeaponInSlot(0);
	}
	const bool bVisible = Shown != EFTOWeapon::None && !CurrentVehicle && !(Knockdown && Knockdown->IsDown()) && GetMesh()->IsVisible();
	if (WeaponMesh->IsVisible() != bVisible)
	{
		WeaponMesh->SetVisibility(bVisible);
	}
	if (!bVisible)
	{
		return;
	}
	UStaticMesh* WeaponAsset = FTOWeapons::Mesh(Shown);
	if (WeaponMesh->GetStaticMesh() != WeaponAsset)
	{
		WeaponMesh->SetStaticMesh(WeaponAsset);
	}
	if (bDrawn)
	{
		FTOWeapons::HoldInHand(WeaponMesh, GetMesh(), GetAimRotation());
		return;
	}
	// Slung across the back (muzzle up over the right shoulder) or holstered on the right hip (muzzle down): placed
	// for someone standing tall, then carried by the spine or the pelvis so it stays put when they crouch, kneel or
	// wrestle.
	const bool bLongGun = FTOWeapons::Spec(Shown).bLongGun;
	const FName Bone = bLongGun ? FName(TEXT("spine")) : FName(TEXT("pelvis"));
	const FTransform Stowed = bLongGun ? FTransform(FRotator(62.f, 180.f, 0.f), FVector(-28.f, -12.f, -20.f)) : FTransform(FRotator(-90.f, 0.f, 0.f), FVector(2.f, 34.f, -22.f));
	const USkeletalMeshComponent* Body = GetMesh();
	const USkeletalMesh* Asset = Body->GetSkeletalMeshAsset();
	const int32 BoneIndex = Asset ? Asset->GetRefSkeleton().FindBoneIndex(Bone) : INDEX_NONE;
	if (BoneIndex == INDEX_NONE)
	{
		WeaponMesh->SetWorldTransform(Stowed * GetActorTransform());
		return;
	}
	const FTransform BoneStanding = FAnimationRuntime::GetComponentSpaceTransformRefPose(Asset->GetRefSkeleton(), BoneIndex) * Body->GetRelativeTransform();
	WeaponMesh->SetWorldTransform(Stowed.GetRelativeTransform(BoneStanding) * Body->GetSocketTransform(Bone));
}

void AFTOCharacter::UpdateAimCamera(float DeltaSeconds)
{
	if (CurrentVehicle)
	{
		return; // the seat camera has its own rules
	}
	// Over the right shoulder, closer in and a little tighter, while a weapon is up.
	const float Target = GetAimPose() != EFTOAimPose::None ? 1.f : 0.f;
	AimBlend = FMath::FInterpTo(AimBlend, Target, DeltaSeconds, 10.f);
	CameraBoom->TargetArmLength = FMath::Lerp(550.f, 240.f, AimBlend);
	CameraBoom->SocketOffset = FMath::Lerp(FVector(0.f, 60.f, 120.f), FVector(0.f, 75.f, 72.f), AimBlend);
	FollowCamera->SetFieldOfView(FMath::Lerp(90.f, 72.f, AimBlend));
}

FVector AFTOCharacter::GetCrosshairTarget() const
{
	const APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC || !PC->PlayerCameraManager)
	{
		return GetActorLocation() + GetActorForwardVector() * 10000.f;
	}
	// From the camera through the middle of the screen, starting past our own shoulder.
	const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
	const FVector Look = PC->PlayerCameraManager->GetCameraRotation().Vector();
	const FVector Start = Eye + Look * (FVector::Dist(Eye, GetActorLocation()) + 40.f);
	const FVector End = Eye + Look * 10000.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOCrosshair), false, this);
	FHitResult Hit;
	return GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_FTOProjectile, Params) ? Hit.ImpactPoint : End;
}

void AFTOCharacter::SelectSlot(int32 Slot)
{
	if (CurrentVehicle || (Knockdown && Knockdown->IsDown()) || IsInSyncedAction())
	{
		return;
	}
	// The same slot again puts it away; empty slots do nothing.
	if (Slot == DrawnSlot)
	{
		Slot = INDEX_NONE;
	}
	else if (GetWeaponInSlot(Slot) == EFTOWeapon::None)
	{
		return;
	}
	if (!HasAuthority())
	{
		DrawnSlot = Slot; // straight away here; the server agrees a moment later
		OnRep_Loadout();
	}
	ServerSelectSlot(Slot);
}

void AFTOCharacter::ServerSelectSlot_Implementation(int32 Slot)
{
	const int32 Asked = Slot;
	if (CurrentVehicle || (Knockdown && Knockdown->IsDown()) || IsInSyncedAction() || (Slot != INDEX_NONE && GetWeaponInSlot(Slot) == EFTOWeapon::None))
	{
		Slot = INDEX_NONE;
	}
	if (Slot != DrawnSlot)
	{
		// A reload doesn't survive swapping weapons.
		ReloadEnd = 0.f;
		GetWorldTimerManager().ClearTimer(ReloadTimer);
	}
	DrawnSlot = Slot;
	OnRep_Loadout();
	// Turned down: the owner already swapped on their screen, and if our answer matches what we had before, nothing
	// would replicate to correct them.
	if (Slot != Asked && !IsLocallyControlled())
	{
		ClientSetDrawnSlot(Slot);
	}
}

void AFTOCharacter::ClientSetDrawnSlot_Implementation(int32 Slot)
{
	DrawnSlot = Slot;
	OnRep_Loadout();
}

void AFTOCharacter::DrawPressed()
{
	if (Loadout.IsValidIndex(DrawnSlot))
	{
		SelectSlot(DrawnSlot); // put it away
		return;
	}
	const int32 Slot = GetWeaponInSlot(LastDrawnSlot) != EFTOWeapon::None
		? LastDrawnSlot : Loadout.IndexOfByPredicate([](EFTOWeapon Weapon) { return Weapon != EFTOWeapon::None; });
	if (Slot != INDEX_NONE)
	{
		SelectSlot(Slot);
	}
}

void AFTOCharacter::NextWeaponPressed()
{
	CycleWeapon(1);
}

void AFTOCharacter::PrevWeaponPressed()
{
	CycleWeapon(-1);
}

void AFTOCharacter::CycleWeapon(int32 Step)
{
	// On to the next slot with something in it, from the one in hand (or the last one used).
	const int32 From = Loadout.IsValidIndex(DrawnSlot) ? DrawnSlot : LastDrawnSlot;
	for (int32 k = 1; k <= FTOWeapons::MaxSlots; ++k)
	{
		const int32 Slot = ((From + Step * k) % FTOWeapons::MaxSlots + FTOWeapons::MaxSlots) % FTOWeapons::MaxSlots;
		if (GetWeaponInSlot(Slot) != EFTOWeapon::None && Slot != DrawnSlot)
		{
			SelectSlot(Slot);
			return;
		}
	}
}

void AFTOCharacter::FirePressed()
{
	if (!IsReadyForAction())
	{
		return;
	}
	// Nothing up yet: the first press draws.
	if (GetDrawnWeapon() == EFTOWeapon::None)
	{
		DrawPressed();
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextShotTime || IsReloading())
	{
		return;
	}
	const FFTOWeaponSpec& Spec = FTOWeapons::Spec(GetDrawnWeapon());
	if (GetClip(DrawnSlot) <= 0)
	{
		UGameplayStatics::PlaySoundAtLocation(this, AFTOGameState::Sounds().DryFire, GetActorLocation());
		ReloadPressed();
		return;
	}
	NextShotTime = Now + Spec.Interval;

	// From the muzzle toward whatever's under the crosshair.
	UpdateWeaponMesh();
	const FVector Muzzle = WeaponMesh->GetSocketLocation(TEXT("Muzzle"));
	const FVector Aim = (GetCrosshairTarget() - Muzzle).GetSafeNormal();
	const int32 Seed = FMath::Rand();
	if (!HasAuthority())
	{
		// Show it now; the server's copy decides what it hits.
		if (UFTOBallistics* Ballistics = UFTOBallistics::Get(GetWorld()))
		{
			Ballistics->Fire(this, GetDrawnWeapon(), Muzzle, Aim, Seed, true, false, true);
		}
		--Clips[DrawnSlot];
	}
	ServerFire(Muzzle, Aim, Seed);
}

void AFTOCharacter::ServerFire_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Aim, int32 Seed)
{
	const EFTOWeapon Weapon = GetDrawnWeapon();
	if (Weapon == EFTOWeapon::None || CurrentVehicle || GetClip(DrawnSlot) <= 0 || IsReloading() || (Knockdown && Knockdown->IsDown()) || IsInSyncedAction())
	{
		return;
	}
	// A little slack for latency, but no machine-gunning a pistol.
	const FFTOWeaponSpec& Spec = FTOWeapons::Spec(Weapon);
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextServerShotTime)
	{
		return;
	}
	NextServerShotTime = Now + Spec.Interval * 0.8f;
	// The shot has to leave from somewhere near our hands.
	FVector Muzzle = Origin;
	if (FVector::DistSquared(Muzzle, GetActorLocation()) > FMath::Square(250.f))
	{
		Muzzle = GetPawnViewLocation();
	}
	--Clips[DrawnSlot];
	if (UFTOBallistics* Ballistics = UFTOBallistics::Get(GetWorld()))
	{
		Ballistics->Fire(this, Weapon, Muzzle, Aim, Seed, true, true, GetNetMode() != NM_DedicatedServer);
	}
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastShot(this, Weapon, Muzzle, Aim, Seed, true);
	}
	// Empty: reload on the spot.
	if (Clips[DrawnSlot] <= 0)
	{
		ServerReload_Implementation();
	}
}

void AFTOCharacter::ReloadPressed()
{
	const EFTOWeapon Weapon = GetDrawnWeapon();
	if (Weapon != EFTOWeapon::None && !IsReloading() && GetClip(DrawnSlot) < FTOWeapons::Spec(Weapon).Magazine && GetSpare(DrawnSlot) > 0)
	{
		ServerReload();
	}
}

void AFTOCharacter::ServerReload_Implementation()
{
	const EFTOWeapon Weapon = GetDrawnWeapon();
	if (Weapon == EFTOWeapon::None || IsReloading() || GetSpare(DrawnSlot) <= 0 || GetClip(DrawnSlot) >= FTOWeapons::Spec(Weapon).Magazine)
	{
		return;
	}
	const float Seconds = FTOWeapons::Spec(Weapon).ReloadSeconds;
	ReloadEnd = GetWorld()->GetTimeSeconds() + Seconds;
	GetWorldTimerManager().SetTimer(ReloadTimer, this, &AFTOCharacter::FinishReload, Seconds, false);
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastPlaySound(AFTOGameState::Sounds().Reload, GetActorLocation(), 0.8f);
	}
}

void AFTOCharacter::FinishReload()
{
	ReloadEnd = 0.f;
	const EFTOWeapon Weapon = GetDrawnWeapon();
	if (Weapon == EFTOWeapon::None)
	{
		return;
	}
	const int32 Moved = FMath::Min(FTOWeapons::Spec(Weapon).Magazine - Clips[DrawnSlot], Spares[DrawnSlot]);
	Clips[DrawnSlot] += Moved;
	Spares[DrawnSlot] -= Moved;
}

// ------------------------------------------------------------------------------------------
// Down, and back up again
// ------------------------------------------------------------------------------------------

bool AFTOCharacter::GoDown(const FVector& Launch, float Seconds)
{
	check(HasAuthority());
	if (!Knockdown || Knockdown->IsDown() || CurrentVehicle)
	{
		return false;
	}
	bDowned = true;
	Knockdown->Knockdown(Launch, Seconds);
	if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(Controller))
	{
		PC->ClientToast(INVTEXT("You're down! A partner can help you up."), FLinearColor(1.f, 0.4f, 0.3f));
	}
	// The radio calls it in for them (the ping follows them until they're up).
	if (AFTOPlayerState* PS = GetPlayerState<AFTOPlayerState>())
	{
		PS->MakeCallout(EFTOCallout::OfficerDown, true);
	}
	return true;
}

bool AFTOCharacter::CanInteract(const AFTOCharacter* Officer) const
{
	return Officer && Officer != this && bDowned && Knockdown && Knockdown->IsDown();
}

FText AFTOCharacter::GetInteractPrompt(const AFTOCharacter* Officer) const
{
	return INVTEXT("Help your partner up");
}

void AFTOCharacter::Interact(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (!CanInteract(Officer))
	{
		return;
	}
	Officer->PlayTimedAction(EFTOAnimAction::Interact, 1.f);
	Knockdown->Recover();
	FTOScoring::Award(Officer, EFTOScore::Revive, GetActorLocation() + FVector(0.f, 0.f, 80.f));
	if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(Officer->GetController()))
	{
		PC->ClientToast(INVTEXT("Back on your feet, partner."), FLinearColor(0.5f, 0.9f, 1.f));
	}
	if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(Controller))
	{
		PC->ClientToast(INVTEXT("Your partner's got you. Back in it!"), FLinearColor(0.5f, 0.9f, 1.f));
	}
}

FVector AFTOCharacter::GetInteractLocation() const
{
	return Knockdown ? Knockdown->GetBodyLocation() : GetActorLocation();
}

// ------------------------------------------------------------------------------------------
// Two-person moves
// ------------------------------------------------------------------------------------------

bool AFTOCharacter::IsReadyForAction() const
{
	return !CurrentVehicle && !IsInSyncedAction() && !(Knockdown && (Knockdown->IsDown() || Knockdown->IsDazed()));
}

void AFTOCharacter::BeginSyncedAction(EFTOAnimAction Action, const FVector& Feet, float Yaw, AActor* Partner)
{
	check(HasAuthority());
	// Hands free: the weapon goes away (and a reload with it).
	if (DrawnSlot != INDEX_NONE)
	{
		DrawnSlot = INDEX_NONE;
		ReloadEnd = 0.f;
		GetWorldTimerManager().ClearTimer(ReloadTimer);
		OnRep_Loadout();
	}
	SyncedAction.Action = Action;
	SyncedAction.Location = Feet + FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	SyncedAction.Yaw = Yaw;
	SyncedAction.Partner = Partner;
	ForceNetUpdate();
	ApplySyncedAction();
}

void AFTOCharacter::EndSyncedAction()
{
	check(HasAuthority());
	if (IsInSyncedAction())
	{
		SyncedAction = FFTOSyncedAction();
		ForceNetUpdate();
		ApplySyncedAction();
	}
}

void AFTOCharacter::OnRep_SyncedAction()
{
	ApplySyncedAction();
}

void AFTOCharacter::ApplySyncedAction()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsInSyncedAction())
	{
		// Rooted to the spot (on the server and the officer's own machine alike, so nothing gets corrected), easing
		// onto it from wherever we are (a new move starts a new ease, from here).
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
		SyncFrom = GetActorLocation();
		SyncFromRotation = GetActorQuat();
		SyncStartTime = GetWorld()->GetTimeSeconds();
		bInSyncedAction = true;
	}
	else if (bInSyncedAction)
	{
		bInSyncedAction = false;
		SyncStartTime = -1.f;
		if (!CurrentVehicle && !(Knockdown && Knockdown->IsDown()))
		{
			MoveComp->SetMovementMode(MOVE_Walking);
		}
	}
}

void AFTOCharacter::TickSyncedAction()
{
	if (!bInSyncedAction || SyncStartTime < 0.f || !(HasAuthority() || IsLocallyControlled()))
	{
		return;
	}
	const float Alpha = FMath::Clamp((GetWorld()->GetTimeSeconds() - SyncStartTime) / SyncEaseSeconds, 0.f, 1.f);
	const float Smooth = Alpha * Alpha * (3.f - 2.f * Alpha);
	const FQuat Facing = FRotator(0.f, SyncedAction.Yaw, 0.f).Quaternion();
	SetActorLocationAndRotation(FMath::Lerp(SyncFrom, FVector(SyncedAction.Location), Smooth), FQuat::Slerp(SyncFromRotation, Facing, Smooth),
		false, nullptr, ETeleportType::TeleportPhysics);
	if (Alpha >= 1.f)
	{
		SyncStartTime = -1.f;
	}
}

void AFTOCharacter::ServerMash_Implementation()
{
	if (SyncedAction.Action == EFTOAnimAction::Struggle)
	{
		if (AFTOPerp* Perp = Cast<AFTOPerp>(SyncedAction.Partner))
		{
			Perp->Mash(this);
		}
	}
}
