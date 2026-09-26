#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/FTOGameState.h"
#include "Radio/FTORadio.h"
#include "FTOPlayerController.generated.h"

class UFTOInputConfig;
class UFTOMenuWidget;
struct FInputActionValue;

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

	/** One of this player's rounds just hit someone (a suspect, or someone it shouldn't have). */
	UFUNCTION(Client, Unreliable)
	void ClientHitMarker(bool bBadHit);

	// ---- Menu / session flow ----
	void SetMenuVisible(bool bVisible);
	void ToggleMenu();
	bool IsMenuVisible() const;

	/** Host only: begin the shift from the lobby. */
	UFUNCTION(Server, Reliable)
	void ServerStartShift();

	/** Host only: reload with a fresh city and seed; everyone comes along. */
	UFUNCTION(Server, Reliable)
	void ServerNewShift();

	/** Reopen this city as a listen server so friends can join. */
	void HostOnline();

	/** Travel to a host by IP (port 7777 if none given). */
	void JoinGame(const FString& Address);

	/** This machine's LAN address, for telling friends where to join. */
	static FString GetLocalAddress();

	/** Dev: jump into the nearest cruiser (also handy for testing clients driving). */
	UFUNCTION(Exec) void FTODrive();

	UFUNCTION(Server, Reliable)
	void ServerEnterNearestCruiser();

	/** Dev: ride shotgun in the nearest cruiser that has a driver. */
	UFUNCTION(Exec) void FTORide();

	UFUNCTION(Server, Reliable)
	void ServerRideAlong();

	/** In vehicles, look out from the seat instead of the chase camera (C toggles; local only). */
	bool bPreferInteriorView = false;

	// ---- Radio (see FTORadio.h) ----
	/** Local: holding push-to-talk. */
	bool IsTransmitting() const { return bTransmitting; }
	bool IsRadioWheelOpen() const { return bWheelOpen; }
	/** Which callout the wheel points at (None in the middle). Up backup, right fleeing, down officer down, left 10-4. */
	EFTOCallout GetWheelChoice() const;
	/** Where on the wheel the stick or mouse points (unit circle). */
	FVector2D GetWheelAim() const { return WheelAim; }

	/** End of shift: vote to go into overtime or clock off (Y / N). Also a console command: FTOVote Overtime|ClockOff. */
	UFUNCTION(Exec) void FTOVote(const FString& Name);
	UFUNCTION(Server, Reliable)
	void ServerShiftVote(EFTOShiftVote Vote);
	void VotePressed(EFTOShiftVote Vote);

	/** Local: the shift's over: watch the squad line up outside the precinct (and stand still for the photo). */
	void ShowDebrief();
	/** Server -> this player: the line-up's ready, watch it (see ShowDebrief). */
	UFUNCTION(Client, Reliable)
	void ClientShowDebrief();
	/** Local: back on duty (a debug restart): the camera and controls come back. */
	void EndDebrief();

	/** Make a callout by name: Backup, Fleeing, OfficerDown or Copy. */
	UFUNCTION(Exec) void FTOCallout(const FString& Name);

	UFUNCTION(Server, Reliable)
	void ServerCallout(EFTOCallout Callout);

	// Radio input (public for the smoke test).
	void RadioPressed();
	void RadioReleased();
	void WheelOpened();
	void WheelClosed();
	void WheelAimed(const FInputActionValue& Value);
	void WheelStick(const FInputActionValue& Value);
	void CalloutPicked(int32 Index);

protected:
	void SendCallout(EFTOCallout Callout);

	UFUNCTION(Server, Reliable)
	void ServerSetOnRadio(bool bOn);

	bool bTransmitting = false;
	bool bWheelOpen = false;
	/** Looking at the end-of-shift line-up. */
	UPROPERTY(Transient) TObjectPtr<AActor> DebriefCamera;
	FVector2D WheelAim = FVector2D::ZeroVector;

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** Only the host (the listen server's own player, or solo) runs the shift. */
	bool IsHostPlayer() const;

	UPROPERTY(Transient)
	TObjectPtr<UFTOInputConfig> InputConfig;

	UPROPERTY(Transient)
	TObjectPtr<UFTOMenuWidget> Menu;
};
