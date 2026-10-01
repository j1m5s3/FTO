#include "Vehicles/FTOCruiser.h"
#include "City/FTOCityKit.h"
#include "Audio/FTOAudio.h"
#include "Crime/FTOArrestee.h"
#include "Physics/FTODestruction.h"
#include "Physics/FTOImpact.h"
#include "Physics/FTOKnockdownComponent.h"
#include "Physics/FTOVehicleDamage.h"
#include "City/FTOCityGenerator.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Art/FTOArt.h"
#include "City/FTOTrafficCar.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Components/AudioComponent.h"
#include "Core/FTOInputConfig.h"
#include "Core/FTOPlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "FTO.h"

namespace
{
	constexpr float NetSendInterval = 1.f / 30.f;
	constexpr float WheelDegreesPerCm = 360.f / (2.f * PI * 38.f);
}

AFTOCruiser::AFTOCruiser()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(30.f);
	bAlwaysRelevant = true; // only a handful, and chases cross the whole map
	AutoPossessAI = EAutoPossessAI::Disabled;

	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->InitBoxExtent(FVector(240.f, 108.f, 72.f));
	Collision->SetCollisionProfileName(TEXT("Pawn"));
	RootComponent = Collision;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMat(FTOArt::BaseMaterialPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> VehicleMat(FTOArt::VehicleMaterialPath);
	VehicleMaterial = VehicleMat.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CruiserMesh(TEXT("/Game/FTO/Vehicles/SM_Car_Cruiser.SM_Car_Cruiser"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CruiserDented(TEXT("/Game/FTO/Vehicles/SM_Car_Cruiser_Dented.SM_Car_Cruiser_Dented"));
	DentedMesh = CruiserDented.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WheelMesh(TEXT("/Game/FTO/Vehicles/SM_Wheel.SM_Wheel"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	BaseMaterial = BaseMat.Object;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Collision);
	Body->SetStaticMesh(CruiserMesh.Object);
	Body->SetRelativeLocation(FVector(0.f, 0.f, -RideHeight));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static const FName WheelSockets[] = { TEXT("Wheel_FL"), TEXT("Wheel_FR"), TEXT("Wheel_RL"), TEXT("Wheel_RR") };
	for (int32 i = 0; i < 4; ++i)
	{
		UStaticMeshComponent* Wheel = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Wheel%d"), i));
		Wheel->SetupAttachment(Body, WheelSockets[i]);
		Wheel->SetUsingAbsoluteScale(true);
		Wheel->SetStaticMesh(WheelMesh.Object);
		Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wheels.Add(Wheel);
	}

	// Glowing lenses over the modelled light bar's red and blue blocks.
	auto MakeLens = [&](FName Name, float Y) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Lens = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Lens->SetupAttachment(Body, TEXT("Lightbar"));
		Lens->SetStaticMesh(CubeMesh.Object);
		Lens->SetRelativeLocation(FVector(0.f, Y, 0.f));
		Lens->SetRelativeScale3D(FVector(0.43f, 0.63f, 0.19f));
		Lens->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Lens->SetCastShadow(false);
		return Lens;
	};
	LightRed = MakeLens(TEXT("LightRed"), -40.f);
	LightBlue = MakeLens(TEXT("LightBlue"), 40.f);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(Collision);
	CameraBoom->TargetArmLength = 950.f;
	CameraBoom->SocketOffset = FVector(0.f, 0.f, 230.f);
	CameraBoom->SetRelativeRotation(FRotator(-12.f, 0.f, 0.f));
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 8.f;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 5.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);

	InteriorCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("InteriorCamera"));
	InteriorCamera->SetupAttachment(Body, FTOSeats::CameraSocket(EFTOSeat::Driver));
	InteriorCamera->SetFieldOfView(95.f);
	InteriorCamera->bAutoActivate = false;

	// Sounds are assigned in BeginPlay from the shared sound set.
	EngineAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineAudio"));
	EngineAudio->SetupAttachment(Collision);
	EngineAudio->bAutoActivate = false;
	SirenAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("SirenAudio"));
	SirenAudio->SetupAttachment(Collision);
	SirenAudio->bAutoActivate = false;
	SkidAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("SkidAudio"));
	SkidAudio->SetupAttachment(Collision);
	SkidAudio->bAutoActivate = false;

	Damage = CreateDefaultSubobject<UFTOVehicleDamage>(TEXT("Damage"));
	Damage->bCitizensCar = false;
}

void AFTOCruiser::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// The driver is authoritative over their own car, so never correct them.
	DOREPLIFETIME_CONDITION(AFTOCruiser, NetState, COND_SkipOwner);
	DOREPLIFETIME(AFTOCruiser, bSiren);
	DOREPLIFETIME(AFTOCruiser, StripeColor);
	DOREPLIFETIME(AFTOCruiser, Driver);
	DOREPLIFETIME(AFTOCruiser, Passenger);
}

void AFTOCruiser::BeginPlay()
{
	Super::BeginPlay();

	PaintMaterial = FTOArt::ApplyColor(Body, VehicleMaterial ? VehicleMaterial.Get() : BaseMaterial.Get(), StripeColor, 0.f, FTOArt::BodySlot(Body));
	const FFTOSoundSet& Sounds = AFTOGameState::Sounds();
	EngineAudio->SetSound(Sounds.EngineLoop);
	EngineAudio->AttenuationSettings = Sounds.World;
	EngineAudio->SetVolumeMultiplier(0.35f);
	SirenAudio->SetSound(Sounds.SirenLoop);
	SirenAudio->AttenuationSettings = Sounds.World;
	SkidAudio->SetSound(Sounds.TireSkidLoop);
	SkidAudio->AttenuationSettings = Sounds.World;
	SirenAudio->SetVolumeMultiplier(0.8f);

	RedMaterial = FTOArt::ApplyColor(LightRed, BaseMaterial, FLinearColor(1.f, 0.05f, 0.05f));
	BlueMaterial = FTOArt::ApplyColor(LightBlue, BaseMaterial, FLinearColor(0.1f, 0.25f, 1.f));

	Damage->SetBody(Body, DentedMesh);

	if (HasAuthority())
	{
		NetState.Location = GetActorLocation();
		NetState.Yaw = GetActorRotation().Yaw;
		HomeTransform = GetActorTransform();
	}
}

void AFTOCruiser::SetStripeColor(const FLinearColor& Color)
{
	StripeColor = Color;
	OnRep_StripeColor();
}

void AFTOCruiser::OnRep_StripeColor()
{
	FTOArt::SetColor(PaintMaterial, StripeColor);
}

void AFTOCruiser::OnRep_Siren()
{
	// Cosmetic flashing happens in UpdateCosmetics.
}

bool AFTOCruiser::IsSimulatingLocally() const
{
	// The driver simulates their own car; the server simulates empty cars rolling to a stop.
	return IsLocallyControlled() || (HasAuthority() && !IsPlayerControlled());
}

// ------------------------------------------------------------------------------------------
// Entering / exiting
// ------------------------------------------------------------------------------------------

USceneComponent* AFTOCruiser::GetSeatParent() const
{
	return Body;
}

bool AFTOCruiser::CanInteract(const AFTOCharacter* Officer) const
{
	return Officer && !Officer->GetCurrentVehicle() && (!Driver || !Passenger);
}

FText AFTOCruiser::GetInteractPrompt(const AFTOCharacter* Officer) const
{
	return Driver ? INVTEXT("Ride shotgun") : INVTEXT("Drive cruiser");
}

void AFTOCruiser::Interact(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (!Officer || Officer->GetCurrentVehicle())
	{
		return;
	}

	if (!Driver)
	{
		AController* OfficerController = Officer->GetController();
		if (!OfficerController)
		{
			return;
		}
		Driver = Officer;
		Officer->EnterVehicle(this, EFTOSeat::Driver);
		OfficerController->Possess(this);
		UE_LOG(LogFTO, Log, TEXT("%s is driving %s."), *GetNameSafe(OfficerController), *GetName());
	}
	else if (!Passenger)
	{
		// Riding shotgun: the officer keeps their own controls (look around, work the lights, hop out).
		Passenger = Officer;
		Officer->EnterVehicle(this, EFTOSeat::Passenger);
		UE_LOG(LogFTO, Log, TEXT("%s is riding shotgun in %s."), *GetNameSafe(Officer), *GetName());
	}
}

void AFTOCruiser::LetOut(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (Officer && Officer == Driver)
	{
		ExitDriver();
	}
	else if (Officer && Officer == Passenger)
	{
		ExitPassenger();
	}
}

void AFTOCruiser::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	ThrottleInput = SteerInput = 0.f;
	bHandbrake = false;
}

void AFTOCruiser::UnPossessed()
{
	Super::UnPossessed();
	ThrottleInput = SteerInput = 0.f;
	bHandbrake = false;
}

void AFTOCruiser::OnExit()
{
	if (HasAuthority())
	{
		ExitDriver();
	}
	else
	{
		ServerExit();
	}
}

void AFTOCruiser::ServerExit_Implementation()
{
	ExitDriver();
}

void AFTOCruiser::ExitDriver()
{
	AController* DriverController = GetController();
	AFTOCharacter* Officer = Driver;
	if (!Officer || !DriverController)
	{
		return;
	}

	// The driver's door is on the left.
	const FVector ExitLocation = FindExitSpot(-1.f);
	Driver = nullptr;
	Officer->ExitVehicle(ExitLocation, GetActorRotation().Yaw);
	DriverController->Possess(Officer);
}

void AFTOCruiser::ExitPassenger()
{
	AFTOCharacter* Officer = Passenger;
	if (!Officer)
	{
		return;
	}
	Passenger = nullptr;
	Officer->ExitVehicle(FindExitSpot(1.f), GetActorRotation().Yaw);
}

FVector AFTOCruiser::FindExitSpot(float Side) const
{
	// Out of our own door, or across and out of the other one if something's in the way.
	const FVector Here = GetActorLocation();
	const FVector Out = GetActorRightVector() * Side;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOCruiserExit), false, this);
	for (const float Direction : { 1.f, -1.f })
	{
		const FVector Spot = Here + Out * Direction * 210.f + FVector(0.f, 0.f, 20.f);
		FHitResult Hit;
		if (!GetWorld()->SweepSingleByChannel(Hit, Here, Spot, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(42.f, 90.f), Params))
		{
			return Spot;
		}
	}
	return Here + Out * 210.f + FVector(0.f, 0.f, 20.f);
}

// ------------------------------------------------------------------------------------------
// Input
// ------------------------------------------------------------------------------------------

void AFTOCruiser::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetController());
	if (!EIC || !PC)
	{
		return;
	}

	// Same actions as on foot: move = throttle/steer, jump = handbrake, interact = exit, whistle = siren.
	UFTOInputConfig* Input = PC->GetInputConfig();
	EIC->BindAction(Input->Move, ETriggerEvent::Triggered, this, &AFTOCruiser::OnMove);
	EIC->BindAction(Input->Move, ETriggerEvent::Completed, this, &AFTOCruiser::OnMoveReleased);
	EIC->BindAction(Input->Look, ETriggerEvent::Triggered, this, &AFTOCruiser::OnLook);
	EIC->BindAction(Input->Camera, ETriggerEvent::Started, this, &AFTOCruiser::OnToggleCamera);
	EIC->BindAction(Input->Jump, ETriggerEvent::Started, this, &AFTOCruiser::OnHandbrake);
	EIC->BindAction(Input->Jump, ETriggerEvent::Completed, this, &AFTOCruiser::OnHandbrakeReleased);
	EIC->BindAction(Input->Interact, ETriggerEvent::Started, this, &AFTOCruiser::OnExit);
	EIC->BindAction(Input->Whistle, ETriggerEvent::Started, this, &AFTOCruiser::OnSiren);
	EIC->BindAction(Input->Horn, ETriggerEvent::Started, this, &AFTOCruiser::OnHorn);
}

void AFTOCruiser::OnMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	ThrottleInput = FMath::Clamp(Axis.Y, -1.f, 1.f);
	SteerInput = FMath::Clamp(Axis.X, -1.f, 1.f);
}

void AFTOCruiser::OnMoveReleased(const FInputActionValue& Value)
{
	ThrottleInput = 0.f;
	SteerInput = 0.f;
}

void AFTOCruiser::OnLook(const FInputActionValue& Value)
{
	// Glance around the cabin or out of the side windows; eases back to the road when left alone.
	const FVector2D Axis = Value.Get<FVector2D>();
	LookYaw = FMath::Clamp(LookYaw + Axis.X * LookRate, -150.f, 150.f);
	LookPitch = FMath::Clamp(LookPitch - Axis.Y * LookRate, -35.f, 30.f);
	LastLookTime = GetWorld()->GetRealTimeSeconds();
}

void AFTOCruiser::OnToggleCamera()
{
	SetInteriorView(!IsInteriorView());
}

void AFTOCruiser::SetInteriorView(bool bInterior)
{
	if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetController()))
	{
		PC->bPreferInteriorView = bInterior;
	}
}

bool AFTOCruiser::IsInteriorView() const
{
	const AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetController());
	return PC && PC->IsLocalController() && PC->bPreferInteriorView;
}

void AFTOCruiser::UpdateViews(float DeltaSeconds)
{
	const bool bInterior = IsLocallyControlled() && IsInteriorView();

	if (GetWorld()->GetRealTimeSeconds() - LastLookTime > 1.5f)
	{
		LookYaw = FMath::FInterpTo(LookYaw, 0.f, DeltaSeconds, 2.f);
		LookPitch = FMath::FInterpTo(LookPitch, 0.f, DeltaSeconds, 2.f);
	}

	if (InteriorCamera->IsActive() != bInterior)
	{
		InteriorCamera->SetActive(bInterior);
		Camera->SetActive(!bInterior);
	}
	if (bInterior)
	{
		InteriorCamera->SetRelativeRotation(FRotator(LookPitch - 8.f, LookYaw, 0.f));
	}
	else
	{
		CameraBoom->SetRelativeRotation(FRotator(-12.f + LookPitch * 0.5f, LookYaw, 0.f));
	}

	// From the seat, the driver's own head would fill the view: hide it on this machine only.
	AFTOCharacter* HideFor = bInterior ? Driver.Get() : nullptr;
	if (HiddenHead.Get() != HideFor)
	{
		if (AFTOCharacter* Previous = HiddenHead.Get())
		{
			Previous->SetHeadHidden(false);
		}
		if (HideFor)
		{
			HideFor->SetHeadHidden(true);
		}
		HiddenHead = HideFor;
	}
}

void AFTOCruiser::OnHandbrake(const FInputActionValue& Value)
{
	bHandbrake = true;
}

void AFTOCruiser::OnHandbrakeReleased(const FInputActionValue& Value)
{
	bHandbrake = false;
}

void AFTOCruiser::OnSiren()
{
	if (HasAuthority())
	{
		bSiren = !bSiren;
	}
	else
	{
		ServerSetSiren(!bSiren);
	}
}

void AFTOCruiser::ServerSetSiren_Implementation(bool bOn)
{
	bSiren = bOn;
}

void AFTOCruiser::OnHorn()
{
	ServerHorn();
}

void AFTOCruiser::ServerHorn_Implementation()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextHornTime)
	{
		return;
	}
	NextHornTime = Now + 0.4f;
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastPlaySound(AFTOGameState::Sounds().Horn, GetActorLocation(), 0.9f);
	}
}

// ------------------------------------------------------------------------------------------
// Simulation
// ------------------------------------------------------------------------------------------

void AFTOCruiser::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// A fire with someone at the wheel smoulders on; left empty, it burns down (and goes up).
	if (Damage && HasAuthority())
	{
		Damage->bHoldFire = Driver != nullptr;
	}

	// Stop passing through people we bowled over a moment ago.
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 i = BowledOver.Num() - 1; i >= 0; --i)
	{
		if (Now >= BowledOver[i].Value || !BowledOver[i].Key.IsValid())
		{
			if (AActor* Victim = BowledOver[i].Key.Get())
			{
				Collision->IgnoreActorWhenMoving(Victim, false);
			}
			BowledOver.RemoveAtSwap(i);
		}
	}
	for (int32 i = BrokenThrough.Num() - 1; i >= 0; --i)
	{
		if (Now >= BrokenThrough[i].Value || !BrokenThrough[i].Key.IsValid())
		{
			if (UPrimitiveComponent* Thing = BrokenThrough[i].Key.Get())
			{
				Collision->IgnoreComponentWhenMoving(Thing, false);
			}
			BrokenThrough.RemoveAtSwap(i);
		}
	}

	if (IsSimulatingLocally())
	{
		Simulate(DeltaSeconds);

		if (HasAuthority())
		{
			NetState.Location = GetActorLocation();
			NetState.Yaw = GetActorRotation().Yaw;
			NetState.Speed = ForwardSpeed;
			NetState.Steer = SteerInput;
		}
		else
		{
			SendAccumulator += DeltaSeconds;
			if (SendAccumulator >= NetSendInterval)
			{
				SendAccumulator = 0.f;
				ServerMove(GetActorLocation(), GetActorRotation().Yaw, ForwardSpeed, LateralSpeed, SteerInput);
			}
		}
	}
	else if (!HasAuthority())
	{
		// Everyone else's car: glide towards the latest replicated state.
		SetActorLocation(FMath::VInterpTo(GetActorLocation(), NetState.Location, DeltaSeconds, 14.f));
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, NetState.Yaw, 0.f), DeltaSeconds, 14.f));
		ForwardSpeed = NetState.Speed;
		SteerInput = NetState.Steer;
	}

	if (HasAuthority())
	{
		TickMotorPool(DeltaSeconds);
		SirenScanAccumulator += DeltaSeconds;
		if (bSiren && SirenScanAccumulator >= 0.25f)
		{
			SirenScanAccumulator = 0.f;
			ServerSirenTick();
		}
	}

	UpdateCosmetics(DeltaSeconds);
	UpdateSkid(DeltaSeconds);
	UpdateViews(DeltaSeconds);
}

void AFTOCruiser::UpdateSkid(float DeltaSeconds)
{
	// Tyres squeal in a slide: measured from how the car's actually moving (works for everyone's car, on every machine).
	// The driver's machine knows the slide exactly; everyone else (the host included, for a client's car, which only
	// moves in ServerMove steps) smooths an estimate from how the car's moving.
	const FVector Here = GetActorLocation();
	const FVector Moved = DeltaSeconds > 0.f ? (Here - SkidLastLocation) / DeltaSeconds : FVector::ZeroVector;
	SkidLastLocation = Here;
	if (Moved.Size2D() < 8000.f) // (not a teleport)
	{
		SkidVelocity = FMath::VInterpTo(SkidVelocity, Moved, DeltaSeconds, 5.f);
	}
	const bool bKnown = IsSimulatingLocally();
	const float Sideways = bKnown ? FMath::Abs(LateralSpeed) : FMath::Abs(FVector::DotProduct(SkidVelocity, GetActorRightVector()));
	const float Speed = bKnown ? FMath::Abs(ForwardSpeed) + Sideways : SkidVelocity.Size2D();
	const float Target = Speed > 300.f ? FMath::Clamp((Sideways - 250.f) / 600.f, 0.f, 1.f) : 0.f;
	SkidLevel = FMath::FInterpTo(SkidLevel, Target, DeltaSeconds, 8.f);
	if (!SkidAudio)
	{
		return;
	}
	if (SkidLevel > 0.05f && !SkidAudio->IsPlaying())
	{
		SkidAudio->Play();
	}
	else if (SkidLevel <= 0.02f && SkidAudio->IsPlaying())
	{
		SkidAudio->Stop();
	}
	SkidAudio->SetVolumeMultiplier(0.9f * SkidLevel);
	SkidAudio->SetPitchMultiplier(0.9f + 0.25f * SkidLevel);
}

void AFTOCruiser::ServerMove_Implementation(FVector_NetQuantize10 Location, float Yaw, float Speed, float Lateral, float Steer)
{
	if (++RemoteMoveCount % 90 == 1)
	{
		UE_LOG(LogFTO, Log, TEXT("%s: remote driver at %s, %.0f km/h"), *GetName(), *FVector(Location).ToCompactString(), Speed * 0.036f);
	}

	SetActorLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f));
	ForwardSpeed = Speed;
	LateralSpeed = Lateral;
	SteerInput = Steer;

	NetState.Location = Location;
	NetState.Yaw = Yaw;
	NetState.Speed = Speed;
	NetState.Steer = Steer;
}

void AFTOCruiser::Simulate(float DeltaSeconds)
{
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	const FRotator OldRotation = GetActorRotation();
	const FVector Fwd = OldRotation.Vector().GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);

	// World velocity from last frame, expressed in the car's frame.
	FVector Velocity = Fwd * ForwardSpeed + Right * LateralSpeed;
	float Forward = FVector::DotProduct(Velocity, Fwd);
	float Lateral = FVector::DotProduct(Velocity, Right);

	// A write-off goes nowhere (it just rolls to a stop).
	const bool bWrecked = Damage && Damage->IsWrecked();
	const float Throttle = (Driver || bAutopilot) && !bWrecked ? ThrottleInput : 0.f;
	const float TopSpeed = MaxSpeed * (bSiren ? 1.15f : 1.f); // "code 3"
	if (Throttle > 0.f)
	{
		Forward += Acceleration * Throttle * Dt * (Forward < 0.f ? 2.5f : 1.f);
	}
	else if (Throttle < 0.f)
	{
		Forward += (Forward > 50.f ? BrakeDeceleration : Acceleration * 0.6f) * Throttle * Dt;
	}
	else
	{
		Forward = FMath::FInterpConstantTo(Forward, 0.f, Dt, CoastDeceleration);
	}
	if (bHandbrake)
	{
		Forward = FMath::FInterpConstantTo(Forward, 0.f, Dt, BrakeDeceleration * 0.35f);
	}
	Forward = FMath::Clamp(Forward, -MaxReverseSpeed, TopSpeed);

	// Sideways slide dies out quickly unless the handbrake is on (drift!).
	Lateral *= FMath::Exp(-(bHandbrake ? HandbrakeGrip : Grip) * Dt);

	// Steering needs rolling speed, softens at top speed, and flips in reverse.
	const float Steer = (Driver || bAutopilot) && !bWrecked ? SteerInput : 0.f;
	const float RollAlpha = FMath::Clamp(FMath::Abs(Forward) / 450.f, 0.f, 1.f);
	const float HighSpeedDamp = 1.f - 0.45f * FMath::Clamp(FMath::Abs(Forward) / MaxSpeed, 0.f, 1.f);
	float YawRate = Steer * MaxYawRate * RollAlpha * HighSpeedDamp * FMath::Sign(Forward);
	if (bHandbrake)
	{
		YawRate *= 1.6f;
	}

	// Velocity keeps its world direction while the body turns: that difference is the slide.
	Velocity = Fwd * Forward + Right * Lateral;
	const FRotator NewRotation(0.f, OldRotation.Yaw + YawRate * Dt, 0.f);

	FHitResult Hit;
	AddActorWorldOffset(Velocity * Dt, true, &Hit);
	if (Hit.bBlockingHit && BowlOver(Hit.GetActor(), Velocity))
	{
		// Straight on through them (they're off flying), a little slower.
		Velocity *= 0.85f;
		AddActorWorldOffset(Velocity * Dt * (1.f - Hit.Time), true, &Hit);
	}
	if (Hit.bBlockingHit && BreakThrough(Hit, Velocity))
	{
		// Through the bin, the hydrant, the lamp post...
		AddActorWorldOffset(Velocity * Dt * (1.f - Hit.Time), true, &Hit);
	}
	if (Hit.bBlockingHit)
	{
		// Bounce off walls and other cars, losing most of the speed (and taking a knock if it was a hard one).
		const FVector Normal = Hit.ImpactNormal.GetSafeNormal2D();
		const float Into = -FVector::DotProduct(Velocity, Normal);
		if (Into > CrashSpeed)
		{
			Crash(Hit, Into);
		}
		else if (FMath::Abs(FVector::DotProduct(Velocity, FVector::CrossProduct(FVector::UpVector, Normal))) > 350.f)
		{
			Scrape(Hit); // grinding along it
		}
		Velocity = (Velocity - 1.4f * FVector::DotProduct(Velocity, Normal) * Normal) * 0.5f;
	}
	SetActorRotation(NewRotation);

	const FVector NewFwd = NewRotation.Vector();
	const FVector NewRight = FVector::CrossProduct(FVector::UpVector, NewFwd);
	ForwardSpeed = FVector::DotProduct(Velocity, NewFwd);
	LateralSpeed = FVector::DotProduct(Velocity, NewRight);

	FollowGround();
}

bool AFTOCruiser::BowlOver(AActor* Victim, const FVector& Velocity)
{
	// Only people (anyone who can be knocked over), and only at speed: walls, cars and slow bumps still bounce.
	const UFTOKnockdownComponent* Knockdown = Victim ? Victim->FindComponentByClass<UFTOKnockdownComponent>() : nullptr;
	if (!Knockdown || Velocity.Size2D() < FTOImpact::MinRunOverSpeed)
	{
		return false;
	}
	// Pass through them for a moment (someone already sprawled in the road doesn't stop a car either).
	Collision->IgnoreActorWhenMoving(Victim, true);
	BowledOver.Emplace(Victim, GetWorld()->GetTimeSeconds() + 2.f);
	if (!Knockdown->IsDown())
	{
		if (HasAuthority())
		{
			FTOImpact::RunOver(Victim, GetActorLocation(), Velocity, GetController());
		}
		else
		{
			ServerBowlOver(Victim, Velocity);
		}
	}
	return true;
}

void AFTOCruiser::ServerBowlOver_Implementation(AActor* Victim, FVector_NetQuantize10 Velocity)
{
	// The driver's machine saw the hit; make sure it's plausible before sending anyone flying.
	if (Victim && FVector::DistSquared(Victim->GetActorLocation(), GetActorLocation()) < FMath::Square(900.f))
	{
		FTOImpact::RunOver(Victim, GetActorLocation(), FVector(Velocity).GetClampedToMaxSize(MaxSpeed * 1.2f), GetController());
	}
}

void AFTOCruiser::IgnoreBriefly(UPrimitiveComponent* Thing, float Until)
{
	Collision->IgnoreComponentWhenMoving(Thing, true);
	if (TPair<TWeakObjectPtr<UPrimitiveComponent>, float>* Already = BrokenThrough.FindByPredicate([Thing](const TPair<TWeakObjectPtr<UPrimitiveComponent>, float>& Entry) { return Entry.Key == Thing; }))
	{
		Already->Value = FMath::Max(Already->Value, Until);
	}
	else
	{
		BrokenThrough.Emplace(Thing, Until);
	}
}

bool AFTOCruiser::BreakThrough(const FHitResult& Hit, FVector& Velocity)
{
	UPrimitiveComponent* Thing = Hit.GetComponent();
	EFTOBreakKind Kind = AFTODestruction::KindOf(Thing);
	AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
	int32 Item = Hit.Item;
	// A building's wall (or a shop window in one, or what's stuck on its front), taken head on fast enough: straight
	// through it.
	EFTOPieceRole PieceRole = EFTOPieceRole::Trim;
	const bool bWall = Wreckage && Hit.Item != INDEX_NONE && Wreckage->GetStructurePiece(Thing, Hit.Item, PieceRole) &&
		(PieceRole == EFTOPieceRole::Wall || PieceRole == EFTOPieceRole::Glass || PieceRole == EFTOPieceRole::Trim);
	const float Into = -FVector::DotProduct(Velocity, Hit.ImpactNormal.GetSafeNormal2D());
	if (bWall)
	{
		if (Into < AFTODestruction::BreakThroughSpeed)
		{
			return false; // (it takes the knock in Crash)
		}
		Kind = EFTOBreakKind::Crumble;
		if (PieceRole != EFTOPieceRole::Wall)
		{
			// The pane's (or the trim's) wall goes with it (and it goes now, here, so the car isn't stopped by it).
			UInstancedStaticMeshComponent* Wall = nullptr;
			int32 WallItem = INDEX_NONE;
			if (!Wreckage->FindWallOf(Thing, Hit.Item, Wall, WallItem))
			{
				return false;
			}
			if (!HasAuthority())
			{
				Wreckage->BreakLocally(Thing, Hit.Item, Hit.ImpactPoint, Velocity * 0.9f);
			}
			IgnoreBriefly(Thing, GetWorld()->GetTimeSeconds() + 0.06f);
			Thing = Wall;
			Item = WallItem;
		}
	}
	else if (Wreckage && Hit.Item != INDEX_NONE && Wreckage->GetStructurePiece(Thing, Hit.Item, PieceRole) && PieceRole == EFTOPieceRole::Inside)
	{
		// The furniture of whatever room we've driven into: scattered, unless we're barely moving.
		if (Velocity.Size2D() < 500.f || !AFTODestruction::IsLoose(Thing))
		{
			return false;
		}
		Kind = EFTOBreakKind::KnockOff;
	}
	else if (!Wreckage || Kind == EFTOBreakKind::None || Kind == EFTOBreakKind::Shatter || Hit.Item == INDEX_NONE ||
		Velocity.Size2D() < AFTODestruction::BreakSpeed(Thing))
	{
		return false;
	}
	const FVector Push = Velocity * 0.9f;
	if (Kind == EFTOBreakKind::Crumble)
	{
		// A hole as wide as the car: this panel and whichever it overlaps either side, glass and all. (Where the
		// car crosses the wall, as wide as it is across it: an angled car cuts a wider hole.)
		const FVector Into2D = -Hit.ImpactNormal.GetSafeNormal2D();
		const FVector Heading = Velocity.GetSafeNormal2D();
		const float Square = FMath::Max(0.35f, FVector::DotProduct(Heading, Into2D));
		const FVector Car = GetActorLocation();
		const FVector Crossing = Car + Heading * (FVector::DotProduct(Hit.ImpactPoint - Car, Into2D) / Square);
		const FVector Extent = Collision->GetScaledBoxExtent();
		const float Across = FMath::Min((Extent.Y + Extent.X * FMath::Sqrt(1.f - Square * Square)) / Square, 350.f);
		TArray<TPair<UInstancedStaticMeshComponent*, int32>> InTheWay;
		Wreckage->WallsInTheWay(Thing, Item, Crossing, Across + 5.f, InTheWay);
		const float Until = GetWorld()->GetTimeSeconds() + 0.06f;
		for (const TPair<UInstancedStaticMeshComponent*, int32>& Piece : InTheWay)
		{
			if (HasAuthority())
			{
				Wreckage->Break(Piece.Key, Piece.Value, Hit.ImpactPoint, Push, GetController());
			}
			else
			{
				Wreckage->BreakLocally(Piece.Key, Piece.Value, Hit.ImpactPoint, Push);
				ServerBreakThrough(Piece.Key->GetFName(), Piece.Value, Hit.ImpactPoint, Push);
			}
			IgnoreBriefly(Piece.Key, Until);
		}
		// That's a wall: it costs the car (and the panels round the hole feel it too).
		Velocity *= 0.55f;
		Crash(Hit, Into * 0.55f);
		return true;
	}
	if (HasAuthority())
	{
		Wreckage->Break(Thing, Item, Hit.ImpactPoint, Push, GetController());
	}
	else
	{
		// Out of our way now; the server breaks it for everyone a moment later.
		Wreckage->BreakLocally(Thing, Item, Hit.ImpactPoint, Push);
		ServerBreakThrough(Thing->GetFName(), Item, Hit.ImpactPoint, Push);
	}
	// It's being tucked away: don't catch on it again meanwhile. (Briefly: this lets the car through every
	// instance of that mesh, so the next fence panel along should still stop it.)
	IgnoreBriefly(Thing, GetWorld()->GetTimeSeconds() + 0.06f);
	Velocity *= Kind == EFTOBreakKind::Topple ? 0.6f : 0.85f;
	return true;
}

void AFTOCruiser::ServerBreakThrough_Implementation(FName Component, int32 Instance, FVector_NetQuantize Hit, FVector_NetQuantize10 Push)
{
	// The driver's machine saw it; make sure it's plausible (right by the car) first.
	AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
	AFTOCityGenerator* City = nullptr;
	for (TActorIterator<AFTOCityGenerator> It(GetWorld()); It; ++It)
	{
		City = *It;
		break;
	}
	UInstancedStaticMeshComponent* Thing = City ? City->FindInstanced(Component) : nullptr;
	// (Checked against the piece itself: at speed and with lag, our copy of the car can be well behind the driver's.)
	if (Wreckage && Thing && Wreckage->IsPieceNear(Thing, Instance, Hit, 400.f) &&
		FVector::DistSquared(FVector(Hit), GetActorLocation()) < FMath::Square(2500.f))
	{
		Wreckage->Break(Thing, Instance, Hit, FVector(Push).GetClampedToMaxSize(MaxSpeed * 1.2f), GetController());
	}
}

void AFTOCruiser::Crash(const FHitResult& Hit, float Into)
{
	// One knock per bump (grinding along a wall doesn't keep crunching).
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextCrashTime)
	{
		return;
	}
	NextCrashTime = Now + 0.4f;
	if (HasAuthority())
	{
		ServerCrash_Implementation(Hit.GetActor(), Into, Hit.ImpactPoint);
	}
	else
	{
		ServerCrash(Hit.GetActor(), Into, Hit.ImpactPoint);
	}
}

void AFTOCruiser::Scrape(const FHitResult& Hit)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextScrapeTime)
	{
		return;
	}
	NextScrapeTime = Now + 0.3f;
	if (HasAuthority())
	{
		ServerScrape_Implementation(Hit.ImpactPoint);
	}
	else
	{
		ServerScrape(Hit.ImpactPoint);
	}
}

void AFTOCruiser::ServerScrape_Implementation(FVector_NetQuantize At)
{
	if (Damage && FVector::DistSquared(FVector(At), GetActorLocation()) < FMath::Square(900.f))
	{
		Damage->AddScrape(At);
	}
}

void AFTOCruiser::ServerCrash_Implementation(AActor* Other, float Into, FVector_NetQuantize At)
{
	// (The driver's machine measured it: keep it within what a cruiser can manage, and by the car.)
	Into = FMath::Min(Into, MaxSpeed * 1.2f);
	const float Knock = FMath::Min(80.f, 5.f + (Into - CrashSpeed) * CrashDamagePerSpeed);
	if (Into <= CrashSpeed || FVector::DistSquared(FVector(At), GetActorLocation()) > FMath::Square(900.f))
	{
		return;
	}
	UE_LOG(LogFTO, Log, TEXT("%s crashed into %s at %.0f km/h (%.0f damage)."), *GetName(), *GetNameSafe(Other), Into * 0.036f, Knock);
	Damage->ApplyDamage(Knock, At, GetController());
	// Whatever we ran into takes the same (another car), and a building's walls take a knock of their own.
	if (UFTOVehicleDamage* Theirs = Other && Other != this ? Other->FindComponentByClass<UFTOVehicleDamage>() : nullptr)
	{
		Theirs->ApplyDamage(Knock, At, GetController());
	}
	else if (Other && Other->IsA<AFTOCityGenerator>())
	{
		if (AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld()))
		{
			Wreckage->DamageAt(At, 130.f, (Into - 300.f) * 0.065f, GetController());
		}
	}
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		// A knock is a thud and a crunch; a big one is the whole car folding (with a metal groan after).
		GS->MulticastPlaySound(FTOAudio::Pick(Knock > 25.f ? TEXT("CarImpactHeavy") : TEXT("CarImpactLight")), At, FMath::Clamp(Knock / 30.f, 0.5f, 1.f));
		if (Knock > 25.f)
		{
			GS->MulticastPlaySound(AFTOGameState::Sounds().Crash, At, FMath::Clamp(Knock / 50.f, 0.5f, 1.f));
			GS->MulticastPlaySound(FTOAudio::Pick(TEXT("MetalCreak")), At, 0.6f);
		}
	}
}

void AFTOCruiser::TickMotorPool(float DeltaSeconds)
{
	// A write-off nobody's sat in for a while is towed back to the precinct lot and comes back good as new.
	if (!Damage || !Damage->IsWrecked() || Driver || Passenger)
	{
		AbandonedFor = 0.f;
		return;
	}
	AbandonedFor += DeltaSeconds;
	if (AbandonedFor >= MotorPoolSeconds)
	{
		// Not with a prisoner still in the back (they'd be towed off from under their escort).
		for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
		{
			if (It->GetRideVehicle() == this)
			{
				return;
			}
		}
		AbandonedFor = 0.f;
		StopDead();
		SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
		NetState.Location = GetActorLocation();
		NetState.Yaw = GetActorRotation().Yaw;
		Damage->Repair();
	}
}

void AFTOCruiser::FollowGround()
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOCruiserGround), false, this);
	const FVector Here = GetActorLocation();
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Here + FVector(0.f, 0.f, 150.f), Here - FVector(0.f, 0.f, 500.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		// Pop smoothly up and down curbs.
		const float TargetZ = Hit.ImpactPoint.Z + RideHeight;
		const float NewZ = FMath::FInterpTo(Here.Z, TargetZ, GetWorld()->GetDeltaSeconds(), 18.f);
		SetActorLocation(FVector(Here.X, Here.Y, NewZ));
	}
}

void AFTOCruiser::UpdateCosmetics(float DeltaSeconds)
{
	WheelSpin = FMath::Fmod(WheelSpin + ForwardSpeed * DeltaSeconds * WheelDegreesPerCm, 360.f);
	SteerVisual = FMath::FInterpTo(SteerVisual, SteerInput * 28.f, DeltaSeconds, 10.f);
	for (int32 i = 0; i < Wheels.Num(); ++i)
	{
		Wheels[i]->SetRelativeRotation(FRotator(-WheelSpin, i < 2 ? SteerVisual : 0.f, 0.f));
	}

	// Engine note rises with speed; the siren loop follows the replicated switch.
	const bool bEngineRunning = Driver != nullptr || FMath::Abs(ForwardSpeed) > 30.f;
	if (EngineAudio && EngineAudio->IsPlaying() != bEngineRunning)
	{
		bEngineRunning ? EngineAudio->Play() : EngineAudio->Stop();
	}
	if (EngineAudio)
	{
		EngineAudio->SetPitchMultiplier(0.7f + 1.5f * FMath::Clamp(FMath::Abs(ForwardSpeed) / MaxSpeed, 0.f, 1.f));
	}
	if (SirenAudio && SirenAudio->IsPlaying() != bSiren)
	{
		bSiren ? SirenAudio->Play() : SirenAudio->Stop();
	}

	// Wee-woo: alternate the lenses while the siren is on.
	if (RedMaterial && BlueMaterial)
	{
		const bool bRedPhase = FMath::Fmod(GetWorld()->GetTimeSeconds() * 3.f, 2.f) < 1.f;
		FTOArt::SetColor(RedMaterial, FLinearColor(1.f, 0.05f, 0.05f), bSiren && bRedPhase ? 12.f : 0.f);
		FTOArt::SetColor(BlueMaterial, FLinearColor(0.1f, 0.25f, 1.f), bSiren && !bRedPhase ? 12.f : 0.f);
	}
}

void AFTOCruiser::ServerSirenTick()
{
	// Offending cars ahead of a cruiser with its lights on pull over.
	const FVector Here = GetActorLocation();
	const FVector Fwd = GetActorForwardVector();
	for (TActorIterator<AFTOTrafficCar> It(GetWorld()); It; ++It)
	{
		AFTOTrafficCar* Car = *It;
		const FVector ToCar = Car->GetActorLocation() - Here;
		if (ToCar.SizeSquared2D() > FMath::Square(SirenReach))
		{
			continue;
		}
		if (FVector::DotProduct(ToCar.GetSafeNormal2D(), Fwd) > 0.7f)
		{
			Car->RequestPullOver();
		}
	}
}
