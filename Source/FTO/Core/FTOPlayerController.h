#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FTOPlayerController.generated.h"

class UFTOInputConfig;

UCLASS()
class FTO_API AFTOPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Runtime-built input actions, shared with the possessed officer. */
	UFTOInputConfig* GetInputConfig();

protected:
	virtual void SetupInputComponent() override;

	UPROPERTY(Transient)
	TObjectPtr<UFTOInputConfig> InputConfig;
};
