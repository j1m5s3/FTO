#include "Core/FTOPlayerController.h"
#include "Core/FTOInputConfig.h"
#include "UI/FTOHUD.h"
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
