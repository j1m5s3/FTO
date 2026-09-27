#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "FTOTalkable.generated.h"

class AFTOCharacter;

UINTERFACE(MinimalAPI)
class UFTOTalkable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Someone an officer can have a conversation with: pressing E on them opens a little menu of things to say (ask
 * what they've seen, pass the time of day, search them, arrest them, let them go), picked with 1-4 or the d-pad.
 * The officer's AFTOCharacter runs the conversation (who with, walking away ends it); the person says what the
 * options are and what happens.
 */
class FTO_API IFTOTalkable
{
	GENERATED_BODY()

public:
	/** Who they are, for the top of the conversation panel (every machine). */
	virtual FText GetTalkTitle() const = 0;

	/** What the officer can say right now, up to four lines (every machine, from replicated state). */
	virtual void GetTalkOptions(const AFTOCharacter* Officer, TArray<FText>& OutOptions) const = 0;

	/** Server: the officer said option Index. False ends the conversation (a goodbye, an arrest, a bolt). */
	virtual bool TalkChoice(AFTOCharacter* Officer, int32 Index) = 0;

	/** Server: the conversation's over (they said goodbye, walked off, or were called away). */
	virtual void TalkEnded(AFTOCharacter* Officer) {}
};
