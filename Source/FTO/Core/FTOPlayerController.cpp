#include "Core/FTOPlayerController.h"
#include "Core/FTOInputConfig.h"
#include "UI/FTOHUD.h"
#include "Dev/FTOSmokeTest.h"
#include "Core/FTOCharacter.h"
#include "Vehicles/FTOCruiser.h"
#include "Core/FTOGameMode.h"
#include "UI/FTOMenuWidget.h"
#include "EngineUtils.h"
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
