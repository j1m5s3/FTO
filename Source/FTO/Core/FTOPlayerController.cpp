#include "Core/FTOPlayerController.h"
#include "Core/FTOInputConfig.h"
#include "UI/FTOHUD.h"
#include "Dev/FTOSmokeTest.h"
#include "Core/FTOCharacter.h"
#include "Vehicles/FTOCruiser.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"

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

#if !UE_BUILD_SHIPPING
	if (IsLocalPlayerController() && AFTOSmokeTest::IsRequested())
	{
		GetWorld()->SpawnActor<AFTOSmokeTest>(AFTOSmokeTest::StaticClass(), FTransform::Identity);
	}
#endif
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
}

void AFTOPlayerController::ClientToast_Implementation(const FText& Message, FLinearColor Color)
{
	if (AFTOHUD* FTOHud = GetHUD<AFTOHUD>())
	{
		FTOHud->AddToast(Message, Color);
	}
}

void AFTOPlayerController::FTODrive()
{
	ServerEnterNearestCruiser();
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
