#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Core/FTOGameState.h"
#include "Crime/FTOCrimeTypes.h"
#include "Scoring/FTOScoring.h"
#include "FTOHUD.generated.h"

class AFTOGameState;
class AFTOIncident;
class AFTOPlayerState;
class UFont;

/**
 * Canvas-drawn HUD so the game is readable with zero UI assets:
 * chaos meter, shift clock, dispatch board, world markers, on-scene progress
 * and the end-of-shift report card. Replace piecemeal with UMG later.
 */
UCLASS()
class FTO_API AFTOHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** Short-lived message stacked above the prompt area. */
	void AddToast(const FText& Message, const FLinearColor& Color);

	/** One of our rounds landed on someone (a bad hit: a citizen or a partner). */
	void ShowHitMarker(bool bBadHit);

	/** "+250 ARREST! x2" rising from Where (bigger, and with a ding, when it's ours). */
	void AddScorePopup(const AFTOPlayerState* Officer, int32 Points, EFTOScore Event, const FVector& Where, int32 Combo);

	/** Max dispatch entries listed at once. */
	UPROPERTY(EditDefaultsOnly, Category="HUD")
	int32 MaxDispatchRows = 7;

protected:
	void DrawChaosMeter(const AFTOGameState* GS);
	void DrawShiftClock(const AFTOGameState* GS);
	/** A big banner across the top for the set piece and rush hour, as they start. */
	void DrawShiftBanner(const AFTOGameState* GS);
	void DrawDispatchBoard(const AFTOGameState* GS);
	void DrawIncidentMarkers(const AFTOGameState* GS);
	void DrawTeammateMarkers(const AFTOGameState* GS);
	void DrawOnSceneProgress(const AFTOGameState* GS);
	void DrawBriefing(const AFTOGameState* GS);
	void DrawLobby(const AFTOGameState* GS);
	void DrawShiftReport(const AFTOGameState* GS);
	void DrawInteractPrompt();
	/** Talking to someone: who they are and what the officer can say (1-4). */
	void DrawTalkPanel();
	void DrawCruiserPanel();
	void DrawEscortPanel();
	/** Crosshair, hit marker, the three weapon slots and their ammo, and the "you're down" banner. */
	void DrawWeaponPanel();
	/** Arrests in progress: the struggle meter (mash!), the cuffing bar, and a suspect on the run. */
	void DrawArrestPanel(const AFTOGameState* GS);

	/** Scoring (FTOHUDScore.cpp): popups in the world, our score and combo, and the end-of-shift scoreboard. */
	void DrawScorePopups();
	void DrawScoreTicker();
	void DrawScoreboard(const AFTOGameState* GS);
	/** The clock's run out: overtime or clock off? Everyone's vote, and the time left to decide. */
	void DrawOvertimeVote(const AFTOGameState* GS);

	struct FScorePopup
	{
		FString Text;
		FLinearColor Color;
		FVector Where = FVector::ZeroVector;
		float Start = 0.f;
		bool bMine = false;
	};
	TArray<FScorePopup> ScorePopups;
	/** When the scoreboard went up (it counts up from there); below zero while it isn't. */
	float ScoreboardShownTime = -1.f;
	float LastCountTick = 0.f;

	/** The squad radio (FTOHUDRadio.cpp): callout pings, who's on air, and the callout wheel. */
	void DrawRadio(const AFTOGameState* GS);
	void DrawRadioPings(const AFTOGameState* GS);
	void DrawRadioWheel();

	/** Local stingers: radio chatter, chimes, alarms, shift fanfares. */
	void UpdateAudioCues(const AFTOGameState* GS);
	void DrawToasts();

	/** Projects a world point, clamping to the screen edge when off-screen. Returns true if on-screen. */
	bool ProjectToScreenEdge(const FVector& World, float Margin, FVector2D& OutScreen) const;

	void DrawPanel(float X, float Y, float W, float H, const FLinearColor& Color = FLinearColor(0.f, 0.f, 0.f, 0.55f));
	void DrawCenteredText(const FString& Text, float CenterX, float Y, const FLinearColor& Color, UFont* Font, float Scale = 1.f);
	void DrawDiamond(const FVector2D& Center, float Size, const FLinearColor& Color);

	float UIScale() const;

	/** Smoothed chaos for a less jittery bar. */
	float DisplayedChaos = 0.f;
	float LastChaos = 0.f;
	float ChaosPulse = 0.f;

	struct FToast
	{
		FString Text;
		FLinearColor Color;
		float ExpireTime = 0.f;
	};
	TArray<FToast> Toasts;

	float HitMarkerUntil = 0.f;
	bool bHitMarkerBad = false;

	// Audio cue bookkeeping
	TMap<TWeakObjectPtr<const AFTOIncident>, EFTOIncidentState> SeenIncidentStates;
	EFTOShiftPhase LastPhase = EFTOShiftPhase::Lobby;
	bool bPhaseKnown = false;
	bool bAlarmArmed = true;
	/** When rush hour started on this machine (-1: not yet), and the last set piece heard about. */
	float RushHourSince = -1.f;
	FName LastSetPiece;
	float LastRadioTime = -10.f;
};
