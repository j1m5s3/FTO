#pragma once

#include "CoreMinimal.h"
#include "FTORadio.generated.h"

class APlayerState;
class USoundBase;
class USoundEffectSourcePresetChain;
class UWorld;

/** Quick radio calls: no mic needed. The first three drop a ping on everyone's screen. */
UENUM(BlueprintType)
enum class EFTOCallout : uint8
{
	None,
	Backup,			// "Need backup!": pings the caller
	Fleeing,		// "Suspect fleeing!": pings the nearest runner (a getaway car, a perp on foot), else the caller
	OfficerDown,	// "Officer down!": pings the nearest downed partner, else the caller (also called automatically)
	Copy			// "10-4": just the acknowledgement
};

/** A callout as everyone sees it: what, where (following someone if it can), and when (server time). */
USTRUCT()
struct FFTOCalloutPing
{
	GENERATED_BODY()

	UPROPERTY() EFTOCallout Callout = EFTOCallout::None;
	UPROPERTY() FVector_NetQuantize Location = FVector::ZeroVector;
	/** The ping rides along with this actor while it's about (the caller, a downed partner, a getaway car). */
	UPROPERTY() TObjectPtr<AActor> Follow;
	/** Counts up with every call, so the same call twice in a row still arrives as a new one. */
	UPROPERTY() uint8 Serial = 0;
	UPROPERTY() float ServerTime = 0.f;

	FVector GetLocation() const;
};

/**
 * The squad radio. Push-to-talk voice (V, or down on the d-pad) goes out over the engine's VOIP with a radio filter
 * on every receiver (band-passed, crushed and a little overdriven) and a squelch at either end of each transmission.
 * Quick callouts (hold T, or up on the d-pad, then pick) need no mic: they announce "Officer Blue: need backup!" to
 * the squad with a chirp and drop a ping on everyone's HUD. Per-officer state lives on AFTOPlayerState.
 */
namespace FTORadio
{
	/** How long a ping stays up. */
	constexpr float PingSeconds = 20.f;
	/** Seconds between one officer's callouts. */
	constexpr float CalloutCooldown = 1.5f;

	FTO_API FText CalloutLine(EFTOCallout Callout);
	/** The ping's short label (BACKUP, FLEEING, OFFICER DOWN). */
	FTO_API FString PingLabel(EFTOCallout Callout);
	FTO_API bool HasPing(EFTOCallout Callout);
	FTO_API FLinearColor CalloutColor(EFTOCallout Callout);

	/** The shared effect chain every teammate's voice goes through. */
	FTO_API USoundEffectSourcePresetChain* GetVoiceFilter();

	/** Squelch on (a transmission starts), squelch off (it ends), and the callout chirp. */
	FTO_API USoundBase* SquelchOpen();
	FTO_API USoundBase* SquelchClose();
	FTO_API USoundBase* CalloutChirp();

	/** Server: where a callout from Caller should point (and what to follow). bAboutCaller pins it on the caller. */
	FTO_API FFTOCalloutPing ResolvePing(const APlayerState* Caller, EFTOCallout Callout, bool bAboutCaller = false);
}
