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
#include "Engine/World.h"
#include "Art/FTOArt.h"
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

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMat(FTOArt::BaseMaterialPath);
	BaseMaterial = BaseMat.Object;

	// No skeletal mesh yet.
	GetMesh()->SetVisibility(false);

	PrimaryActorTick.bCanEverTick = true;
}

void AFTOCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AFTOCharacter, bSprinting, COND_SkipOwner);
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
}

void AFTOCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Skin and a navy cap band; the uniform itself follows the badge colour.
	const AFTOPlayerState* PS = GetPlayerState<AFTOPlayerState>();
	FTOArt::ApplyColor(HeadMesh, BaseMaterial, FTOArt::SkinTone(PS ? PS->GetBadgeIndex() : 0));
	FTOArt::ApplyColor(CapMesh, BaseMaterial, FLinearColor(0.02f, 0.03f, 0.09f));
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
	if (!PS || !BodyMesh)
	{
		return;
	}

	if (!UniformMaterial)
	{
		UniformMaterial = FTOArt::ApplyColor(BodyMesh, BaseMaterial, PS->GetOfficerColor());
	}
	FTOArt::SetColor(UniformMaterial, PS->GetOfficerColor());

	if (HeadMesh)
	{
		FTOArt::ApplyColor(HeadMesh, BaseMaterial, FTOArt::SkinTone(PS->GetBadgeIndex()));
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
void AFTOCharacter::WhistlePressed() {}

void AFTOCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocallyControlled())
	{
		FocusAccumulator += DeltaSeconds;
		if (FocusAccumulator >= 0.1f)
		{
			FocusAccumulator = 0.f;
			UpdateFocus();
		}
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
