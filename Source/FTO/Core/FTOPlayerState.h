#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Radio/FTORadio.h"
#include "FTOPlayerState.generated.h"

class UVOIPTalker;

/** Per-officer replicated state. */
UCLASS()
class FTO_API AFTOPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server only. */
	void SetBadgeIndex(int32 NewIndex);

	UFUNCTION(BlueprintPure, Category="FTO")
	int32 GetBadgeIndex() const { return BadgeIndex; }

	/** Uniform colour for this officer's badge slot. */
	UFUNCTION(BlueprintPure, Category="FTO")
	FLinearColor GetOfficerColor() const;

	static FLinearColor ColorForBadge(int32 Index);
	/** "Blue", "Red"... what the squad calls this officer on the radio. */
	static FString CallsignForBadge(int32 Index);
	FString GetCallsign() const { return CallsignForBadge(BadgeIndex); }

	// ---- Radio (see FTORadio.h) ----
	/** Server: this officer's holding push-to-talk (everyone else hears the squelch and sees them on air). */
	void SetOnRadio(bool bOn);
	bool IsOnRadio() const { return bOnRadio; }
	/** Server: a quick callout. False while the last one's still cooling down. */
	bool MakeCallout(EFTOCallout Callout);
	/** This officer's latest callout (its ping lasts FTORadio::PingSeconds). */
	const FFTOCalloutPing& GetCallout() const { return Callout; }
	/** Seconds since that callout (by the server clock), or a large number if there's never been one. */
	float GetCalloutAge() const;
	/** This officer is the one playing on this machine. */
	bool IsLocalOfficer() const;
	/** Their voice plays through the radio filter here. */
	bool HasRadioVoice() const { return VoiceTalker != nullptr; }

protected:
	virtual void BeginPlay() override;
	/** Every machine, whenever we learn who this officer is (the server sets it, clients get it replicated). */
	virtual void OnSetUniqueId() override;

	UPROPERTY(ReplicatedUsing=OnRep_BadgeIndex, BlueprintReadOnly, Category="FTO")
	int32 BadgeIndex = 0;

	UFUNCTION()
	void OnRep_BadgeIndex();

	UPROPERTY(ReplicatedUsing=OnRep_OnRadio) bool bOnRadio = false;
	UPROPERTY(ReplicatedUsing=OnRep_Callout) FFTOCalloutPing Callout;

	UFUNCTION() void OnRep_OnRadio();
	UFUNCTION() void OnRep_Callout();
	/** Every machine with a screen: the squelch as a partner keys up or lets go (their own machine plays its own). */
	void PlaySquelch() const;
	/** Every machine with a screen: "Blue: Need backup!" and the chirp. */
	void AnnounceCallout() const;
	/** On other officers' machines, this officer's voice plays through the radio filter. */
	void SetUpVoice();

	UPROPERTY(Transient) TObjectPtr<UVOIPTalker> VoiceTalker;
	float LastCalloutTime = -100.f;
};
