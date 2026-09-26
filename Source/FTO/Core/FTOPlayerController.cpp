#include "Core/FTOPlayerController.h"
#include "Core/FTOInputConfig.h"
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
