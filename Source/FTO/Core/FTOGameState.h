#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FTOGameState.generated.h"

/** Replicated, city-wide state every officer can see. */
UCLASS()
class FTO_API AFTOGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AFTOGameState();
};
