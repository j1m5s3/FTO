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

	/** Pops a short message on this player's screen (citizen chatter, tips, results). */
	UFUNCTION(Client, Reliable)
	void ClientToast(const FText& Message, FLinearColor Color);

protected:
	virtual void SetupInputComponent() override;

	UPROPERTY(Transient)
	TObjectPtr<UFTOInputConfig> InputConfig;
};
