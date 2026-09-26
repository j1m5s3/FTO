#include "Core/FTOPlayerController.h"
#include "Core/FTOInputConfig.h"
#include "Core/FTOPlayerState.h"
#include "InputActionValue.h"
#include "UI/FTOHUD.h"
#include "Dev/FTOSmokeTest.h"
#include "Core/FTOCharacter.h"
#include "Vehicles/FTOCruiser.h"
#include "Core/FTOGameMode.h"
#include "UI/FTOMenuWidget.h"
#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "Scoring/FTOScoring.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "IPAddress.h"
#include "Kismet/GameplayStatics.h"
#include "SocketSubsystem.h"

UFTOInputConfig* AFTOPlayerController::GetInputConfig()
{
	if (!InputConfig)
	{
		InputConfig = NewObject<UFTOInputConfig>(this, TEXT("FTOInputConfig"));
		InputConfig->Build();
	}
	return InputConfig;
}

void AFTOPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalPlayerController())
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	if (AFTOSmokeTest::IsRequested())
	{
		GetWorld()->SpawnActor<AFTOSmokeTest>(AFTOSmokeTest::StaticClass(), FTransform::Identity);
		return;
	}
#endif

	// Fresh solo launch: open on the menu so players see Host/Join straight away.
	if (GetNetMode() == NM_Standalone)
	{
		SetMenuVisible(true);
	}
}

void AFTOPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!IsLocalPlayerController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(GetInputConfig()->DefaultContext, 0);
	}

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(GetInputConfig()->Menu, ETriggerEvent::Started, this, &AFTOPlayerController::ToggleMenu);

		// The radio works on foot, at the wheel and in the lobby alike, so it lives here rather than on the pawn.
		UFTOInputConfig* Config = GetInputConfig();
		EIC->BindAction(Config->Radio, ETriggerEvent::Started, this, &AFTOPlayerController::RadioPressed);
		EIC->BindAction(Config->Radio, ETriggerEvent::Completed, this, &AFTOPlayerController::RadioReleased);
		EIC->BindAction(Config->RadioWheel, ETriggerEvent::Started, this, &AFTOPlayerController::WheelOpened);
		EIC->BindAction(Config->RadioWheel, ETriggerEvent::Completed, this, &AFTOPlayerController::WheelClosed);
		EIC->BindAction(Config->RadioAim, ETriggerEvent::Triggered, this, &AFTOPlayerController::WheelAimed);
		EIC->BindAction(Config->RadioAimStick, ETriggerEvent::Triggered, this, &AFTOPlayerController::WheelStick);
		for (int32 i = 0; i < Config->Callouts.Num(); ++i)
		{
			EIC->BindAction(Config->Callouts[i], ETriggerEvent::Started, this, &AFTOPlayerController::CalloutPicked, i);
		}
	}
}

// ------------------------------------------------------------------------------------------
// Radio
// ------------------------------------------------------------------------------------------

void AFTOPlayerController::RadioPressed()
{
	if (bTransmitting || IsMenuVisible())
	{
		return;
	}
	bTransmitting = true;
	StartTalking();
	UGameplayStatics::PlaySound2D(this, FTORadio::SquelchOpen(), 0.5f);
	ServerSetOnRadio(true);
}

void AFTOPlayerController::RadioReleased()
{
	if (!bTransmitting)
	{
		return;
	}
	bTransmitting = false;
	StopTalking();
	UGameplayStatics::PlaySound2D(this, FTORadio::SquelchClose(), 0.5f);
	ServerSetOnRadio(false);
}

void AFTOPlayerController::ServerSetOnRadio_Implementation(bool bOn)
{
	if (AFTOPlayerState* PS = GetPlayerState<AFTOPlayerState>())
	{
		PS->SetOnRadio(bOn);
	}
}

void AFTOPlayerController::WheelOpened()
{
	if (bWheelOpen || IsMenuVisible())
	{
		return;
	}
	bWheelOpen = true;
	WheelAim = FVector2D::ZeroVector;
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(GetInputConfig()->WheelContext, 100);
	}
}

void AFTOPlayerController::WheelClosed()
{
	if (!bWheelOpen)
	{
		return;
	}
	// Let go while pointing at a callout to send it (let go in the middle to call nothing).
	const EFTOCallout Choice = GetWheelChoice();
	bWheelOpen = false;
	WheelAim = FVector2D::ZeroVector;
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->RemoveMappingContext(GetInputConfig()->WheelContext);
	}
	SendCallout(Choice);
}

void AFTOPlayerController::WheelAimed(const FInputActionValue& Value)
{
	// The mouse nudges a cursor round the wheel.
	WheelAim += Value.Get<FVector2D>() / 60.f;
	if (WheelAim.SizeSquared() > 1.f)
	{
		WheelAim.Normalize();
	}
}

void AFTOPlayerController::WheelStick(const FInputActionValue& Value)
{
	// The stick points straight at a slice (and stays on it when let go).
	const FVector2D Input = Value.Get<FVector2D>();
	if (Input.SizeSquared() > 0.25f)
	{
		WheelAim = Input.GetSafeNormal();
	}
}

void AFTOPlayerController::CalloutPicked(int32 Index)
{
	static const EFTOCallout ByKey[] = { EFTOCallout::Backup, EFTOCallout::Fleeing, EFTOCallout::OfficerDown, EFTOCallout::Copy };
	if (bWheelOpen && Index >= 0 && Index < UE_ARRAY_COUNT(ByKey))
	{
		SendCallout(ByKey[Index]);
		// Sent: the wheel closes (pointing at nothing, so that sends nothing more; letting go of T then does nothing).
		WheelAim = FVector2D::ZeroVector;
		WheelClosed();
	}
}

EFTOCallout AFTOPlayerController::GetWheelChoice() const
{
	if (!bWheelOpen || WheelAim.SizeSquared() < FMath::Square(0.4f))
	{
		return EFTOCallout::None;
	}
	if (FMath::Abs(WheelAim.Y) >= FMath::Abs(WheelAim.X))
	{
		return WheelAim.Y > 0.f ? EFTOCallout::Backup : EFTOCallout::OfficerDown;
	}
	return WheelAim.X > 0.f ? EFTOCallout::Fleeing : EFTOCallout::Copy;
}

void AFTOPlayerController::SendCallout(EFTOCallout Callout)
{
	if (Callout != EFTOCallout::None)
	{
		ServerCallout(Callout);
	}
}

void AFTOPlayerController::ServerCallout_Implementation(EFTOCallout Callout)
{
	AFTOPlayerState* PS = GetPlayerState<AFTOPlayerState>();
	if (PS && !PS->MakeCallout(Callout))
	{
		ClientToast(INVTEXT("Radio's busy, give it a second."), FLinearColor(0.7f, 0.7f, 0.7f));
	}
}

void AFTOPlayerController::FTOCallout(const FString& Name)
{
	const UEnum* Enum = StaticEnum<EFTOCallout>();
	const int64 Value = Enum->GetValueByNameString(Name);
	if (Value != INDEX_NONE)
	{
		SendCallout(static_cast<EFTOCallout>(Value));
	}
}

// ------------------------------------------------------------------------------------------
// Menu and session flow
// ------------------------------------------------------------------------------------------

bool AFTOPlayerController::IsMenuVisible() const
{
	return Menu && Menu->IsInViewport();
}

void AFTOPlayerController::ToggleMenu()
{
	SetMenuVisible(!IsMenuVisible());
}

void AFTOPlayerController::SetMenuVisible(bool bVisible)
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (bVisible)
	{
		// Opening the menu mid-transmission (or with the wheel up) lets go of the radio.
		RadioReleased();
		if (bWheelOpen)
		{
			WheelAim = FVector2D::ZeroVector;
			WheelClosed();
		}
		if (!Menu)
		{
			Menu = CreateWidget<UFTOMenuWidget>(this, UFTOMenuWidget::StaticClass());
		}
		if (Menu && !Menu->IsInViewport())
		{
			Menu->AddToViewport(10);
		}
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		SetShowMouseCursor(true);
	}
	else
	{
		if (Menu)
		{
			Menu->RemoveFromParent();
		}
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
	}
}

bool AFTOPlayerController::IsHostPlayer() const
{
	// On the server, a local controller is the host's own player (or the solo player).
	return HasAuthority() && IsLocalController();
}

void AFTOPlayerController::ServerStartShift_Implementation()
{
	if (!IsHostPlayer())
	{
		return;
	}
	if (AFTOGameMode* GM = GetWorld()->GetAuthGameMode<AFTOGameMode>())
	{
		GM->StartShift();
	}
}

void AFTOPlayerController::ServerNewShift_Implementation()
{
	if (!IsHostPlayer())
	{
		return;
	}
	if (AFTOGameMode* GM = GetWorld()->GetAuthGameMode<AFTOGameMode>())
	{
		GM->NewShift();
	}
}

void AFTOPlayerController::HostOnline()
{
	if (GetNetMode() != NM_Standalone)
	{
		return;
	}
	const FString Map = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	UGameplayStatics::OpenLevel(this, FName(*Map), true, TEXT("listen"));
}

void AFTOPlayerController::JoinGame(const FString& Address)
{
	FString Target = Address;
	if (!Target.Contains(TEXT(":")))
	{
		Target += TEXT(":7777");
	}
	ClientTravel(Target, TRAVEL_Absolute);
}

FString AFTOPlayerController::GetLocalAddress()
{
	bool bCanBindAll = false;
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Sockets)
	{
		return TEXT("?");
	}
	const TSharedRef<FInternetAddr> Address = Sockets->GetLocalHostAddr(*GLog, bCanBindAll);
	return Address->IsValid() ? Address->ToString(false) : TEXT("?");
}

void AFTOPlayerController::ClientToast_Implementation(const FText& Message, FLinearColor Color)
{
	if (AFTOHUD* FTOHud = GetHUD<AFTOHUD>())
	{
		FTOHud->AddToast(Message, Color);
	}
}

void AFTOPlayerController::ClientHitMarker_Implementation(bool bBadHit)
{
	if (AFTOHUD* FTOHud = GetHUD<AFTOHUD>())
	{
		FTOHud->ShowHitMarker(bBadHit);
	}
}

void AFTOPlayerController::FTODrive()
{
	ServerEnterNearestCruiser();
}

void AFTOPlayerController::FTORide()
{
	ServerRideAlong();
}

void AFTOPlayerController::ServerRideAlong_Implementation()
{
	AFTOCharacter* Officer = GetPawn<AFTOCharacter>();
	if (!Officer || Officer->GetCurrentVehicle())
	{
		return;
	}

	AFTOCruiser* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max(); // dev command: any distance
	for (TActorIterator<AFTOCruiser> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), Officer->GetActorLocation());
		if (It->HasDriver() && !It->GetPassenger() && DistSq < NearestDistSq)
		{
			Nearest = *It;
			NearestDistSq = DistSq;
		}
	}
	if (Nearest)
	{
		Nearest->Interact(Officer);
	}
}

void AFTOPlayerController::ServerEnterNearestCruiser_Implementation()
{
	AFTOCharacter* Officer = GetPawn<AFTOCharacter>();
	if (!Officer)
	{
		return;
	}

	AFTOCruiser* Nearest = nullptr;
	float NearestDistSq = FMath::Square(3000.f);
	for (TActorIterator<AFTOCruiser> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), Officer->GetActorLocation());
		if (!It->HasDriver() && DistSq < NearestDistSq)
		{
			Nearest = *It;
			NearestDistSq = DistSq;
		}
	}
	if (Nearest)
	{
		Nearest->Interact(Officer);
	}
}

void AFTOPlayerController::ShowDebrief()
{
	if (!IsLocalPlayerController())
	{
		return;
	}
	// Mid-callout or transmission: let go.
	RadioReleased();
	if (bWheelOpen)
	{
		WheelAim = FVector2D::ZeroVector;
		WheelClosed();
	}
	TArray<FTransform> Spots;
	FTransform Eye;
	if (!FTOScoring::DebriefSpots(GetWorld(), 4, Spots, Eye))
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	if (ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Eye, Params))
	{
		SetViewTargetWithBlend(Camera, 1.2f, VTBlend_EaseInOut, 2.f);
	}
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
}