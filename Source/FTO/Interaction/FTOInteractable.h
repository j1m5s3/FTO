#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "FTOInteractable.generated.h"

class AFTOCharacter;

UINTERFACE(MinimalAPI, BlueprintType)
class UFTOInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything an officer can press Interact on: cars to pull over, citizens to question, etc.
 * CanInteract/GetInteractPrompt run on clients for the prompt; Interact runs on the server.
 */
class FTO_API IFTOInteractable
{
	GENERATED_BODY()

public:
	virtual bool CanInteract(const AFTOCharacter* Officer) const = 0;
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const = 0;
	/** Server only. */
	virtual void Interact(AFTOCharacter* Officer) = 0;
	/** Where the prompt floats and distance is measured from. */
	virtual FVector GetInteractLocation() const = 0;
	virtual float GetInteractRange() const { return 350.f; }
};
