#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Radio/FTORadio.h"
#include "Scoring/FTOScoring.h"
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
	/** Server: a quick callout. False while the last one's still cooling down. bAutomatic (the radio calling in
	 *  that we're down) is about us and skips the cooldown. */
	bool MakeCallout(EFTOCallout Callout, bool bAutomatic = false);
	/** This officer's latest callout (its ping lasts FTORadio::PingSeconds). */
	const FFTOCalloutPing& GetCallout() const { return Callout; }
	/** Seconds since that callout (by the server clock), or a large number if there's never been one. */
	float GetCalloutAge() const;
	/** This officer is the one playing on this machine. */
	bool IsLocalOfficer() const;
	/** Their voice plays through the radio filter here. */
	bool HasRadioVoice() const { return VoiceTalker != nullptr; }

	// ---- Scoring (see FTOScoring.h) ----
	const FFTOOfficerStats& GetStats() const { return Stats; }
	int32 GetShiftScore() const { return Stats.Score; }
	/**
	 * Server: Event is worth BasePoints (negative for penalties). Good work within FTOScoring::ComboWindow of the last
	 * builds the combo (and its multiplier); a penalty breaks it. Everyone sees the popup at Where. Returns the points.
	 */
	int32 AddScore(EFTOScore Event, int32 BasePoints, const FVector& Where);
	/** The combo building right now (1 = none), by the server's clock. */
	int32 GetCombo() const;
	/** Server time the combo last grew (it lapses FTOScoring::ComboWindow later). */
	float GetComboTime() const { return ComboTime; }

	/** Every machine: the "+250 ARREST! x2" popup. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastScorePopup(int32 Points, EFTOScore Event, FVector_NetQuantize Where, uint8 InCombo);

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

	UPROPERTY(Replicated) FFTOOfficerStats Stats;
	UPROPERTY(Replicated) uint8 Combo = 1;
	/** Server time of the last award that built the combo. */
	UPROPERTY(Replicated) float ComboTime = -100.f;
};
