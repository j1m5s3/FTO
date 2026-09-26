#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FTOPlayerController.generated.h"

class UFTOInputConfig;
class UFTOMenuWidget;

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

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** Only the host (the listen server's own player, or solo) runs the shift. */
	bool IsHostPlayer() const;

	UPROPERTY(Transient)
	TObjectPtr<UFTOInputConfig> InputConfig;

	UPROPERTY(Transient)
	TObjectPtr<UFTOMenuWidget> Menu;
};
