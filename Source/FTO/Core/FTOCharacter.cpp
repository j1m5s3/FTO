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

	auto MakePiece = [this](FName Name, UStaticMesh* Mesh, const FVector& Loc, const FVector& Scale) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Piece = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Piece->SetupAttachment(RootComponent);
		Piece->SetStaticMesh(Mesh);
		Piece->SetRelativeLocation(Loc);
		Piece->SetRelativeScale3D(Scale);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetGenerateOverlapEvents(false);
		return Piece;
	};

	BodyMesh = MakePiece(TEXT("BodyMesh"), CylinderMesh.Object, FVector(0.f, 0.f, -25.f), FVector(0.8f, 0.8f, 1.3f));
	HeadMesh = MakePiece(TEXT("HeadMesh"), SphereMesh.Object,   FVector(0.f, 0.f, 60.f),  FVector(0.75f));
	CapMesh  = MakePiece(TEXT("CapMesh"),  CylinderMesh.Object, FVector(8.f, 0.f, 95.f),  FVector(0.8f, 0.8f, 0.2f));

	// No skeletal mesh yet.
	GetMesh()->SetVisibility(false);
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
		UniformMaterial = BodyMesh->CreateAndSetMaterialInstanceDynamic(0);
		if (CapMesh && UniformMaterial)
		{
			CapMesh->SetMaterial(0, UniformMaterial);
		}
	}

	if (UniformMaterial)
	{
		// BasicShapeMaterial exposes a "Color" vector parameter.
		UniformMaterial->SetVectorParameterValue(TEXT("Color"), PS->GetOfficerColor());
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

void AFTOCharacter::InteractPressed() {}
void AFTOCharacter::InteractReleased() {}
void AFTOCharacter::WhistlePressed() {}
