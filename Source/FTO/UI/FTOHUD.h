#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FTOHUD.generated.h"

class AFTOGameState;
class AFTOIncident;
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

	/** Max dispatch entries listed at once. */
	UPROPERTY(EditDefaultsOnly, Category="HUD")
	int32 MaxDispatchRows = 7;

protected:
	void DrawChaosMeter(const AFTOGameState* GS);
	void DrawShiftClock(const AFTOGameState* GS);
	void DrawDispatchBoard(const AFTOGameState* GS);
	void DrawIncidentMarkers(const AFTOGameState* GS);
	void DrawTeammateMarkers(const AFTOGameState* GS);
	void DrawOnSceneProgress(const AFTOGameState* GS);
	void DrawBriefing(const AFTOGameState* GS);
	void DrawLobby(const AFTOGameState* GS);
	void DrawShiftReport(const AFTOGameState* GS);
	void DrawInteractPrompt();
	void DrawCruiserPanel();
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
};
