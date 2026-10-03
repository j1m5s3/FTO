#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FTOBotPilot.generated.h"

class AFTOCharacter;
class AFTOCruiser;
class AFTOGameState;
class AFTOIncident;
class APlayerController;

/**
 * Bot play, for play testing without a human at the keyboard: launch with -FTOBotPlay and the local officer plays the
 * shift by itself through the same inputs a player has (walking and sprinting, E, the conversation options, punches,
 * the tackle, weapons, driving a cruiser by its throttle and wheel), reading the same things a player would (the
 * dispatch board, descriptions, the bomb's label, what the drunk says). It plays full shifts, takes a screenshot
 * every so often into Saved/Screenshots/Bot, and logs what it's doing and how the shift's going ("BOT:" lines).
 *
 *   -FTOBotPlay                  play
 *   -FTOBotStyle=Careful         Careful (by the book), Reckless (arrests first, drives fast, overtime always) or
 *                                Explorer (wanders, gets kit, takes every call it passes)
 *   -FTOBotShifts=2              shifts to play (the host starts each one; New shift in between), then quit
 *   -FTOBotShots=20              seconds between screenshots (0: none)
 *   -FTOBotTag=name              prefix for the screenshots
 *   -FTOBotWaitFor=2             the host waits (up to a minute) for this many officers before starting
 * With a host and a client both bot-playing, it's a two-player session.
 */
UCLASS(NotPlaceable, Transient)
class FTO_API AFTOBotPilot : public AActor
{
	GENERATED_BODY()

public:
	AFTOBotPilot();
	virtual void Tick(float DeltaSeconds) override;
	static bool IsRequested();

protected:
	virtual void BeginPlay() override;

	AFTOCharacter* Me() const;
	AFTOCruiser* MyCar() const;
	APlayerController* PC() const;
	AFTOGameState* GS() const;
	FVector Here() const;

	/** A few times a second: what to do now. */
	void Think();
	/** The shift's over, or between phases. */
	void TickPhase();
	/** Every frame: walking (or driving) to the goal. */
	void Steer(float DeltaSeconds);
	/** An incident to deal with (the most pressing, nearest first, by style). */
	AFTOIncident* PickTarget() const;
	/** Work the target: walk up, talk, cuff, chase, tackle, fight... */
	void Work(AFTOIncident* Incident);
	/** Answer whatever conversation panel's open, as a player reading it would. */
	void HandleTalk();
	/** Book the suspects trailing after us, if it's time. */
	bool TakeThemIn();

	void GoTo(const FVector& Where, bool bRun, bool bAllowDrive = true);
	void Face(const FVector& Where);
	bool Close(const FVector& Where, float Distance) const;
	void Press();
	void Shot(const TCHAR* Why);
	void Say(const FString& Line);

	FString Style = TEXT("Careful");
	FString Tag;
	int32 ShiftsWanted = 1;
	int32 WaitFor = 1;
	float ShotEvery = 20.f;

	// Where we're going.
	FVector Goal = FVector::ZeroVector;
	bool bGoal = false;
	bool bRun = false;
	bool bMayDrive = true;
	TWeakObjectPtr<AFTOIncident> Target;
	float TargetSince = 0.f;

	// Getting unstuck.
	FVector LastSpot = FVector::ZeroVector;
	float StuckFor = 0.f;
	float EscapeUntil = 0.f;
	FVector EscapeDir = FVector::ZeroVector;
	/** Driving: the closest we've got to the goal, and when; no cruisers until this (after one got us nowhere). */
	float BestDriveDistance = TNumericLimits<float>::Max();
	float DriveProgressAt = 0.f;
	float NoDriveUntil = 0.f;

	// Pacing ourselves.
	float NextThink = 0.f;
	float NextPress = 0.f;
	float NextSwing = 0.f;
	float NextShot = 0.f;
	float NextStatus = 0.f;
	float StartedAt = 0.f;
	float PhaseSince = 0.f;
	uint8 LastPhase = 255;
	bool bVoted = false;
	bool bReported = false;
	int32 Shots = 0;
	/** People in a crowd or a search we've already questioned. */
	TSet<TWeakObjectPtr<AActor>> Questioned;
	/** Kit from the armory (Explorer). */
	bool bKitted = false;
};
