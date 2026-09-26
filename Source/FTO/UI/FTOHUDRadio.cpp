// The squad radio on the HUD: callout pings in the world, who's on air, and the callout wheel.
#include "UI/FTOHUD.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOPlayerController.h"
#include "Core/FTOPlayerState.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/Pawn.h"
#include "Radio/FTORadio.h"

namespace
{
	/** The wheel's slices, clockwise from the top, with the key that picks each one outright. */
	struct FWheelSlice
	{
		EFTOCallout Callout;
		FVector2D Direction; // screen space (+Y down)
		const TCHAR* Key;
	};
	const FWheelSlice WheelSlices[] =
	{
		{ EFTOCallout::Backup,      FVector2D(0.f, -1.f), TEXT("1") },
		{ EFTOCallout::Fleeing,     FVector2D(1.f, 0.f),  TEXT("2") },
		{ EFTOCallout::OfficerDown, FVector2D(0.f, 1.f),  TEXT("3") },
		{ EFTOCallout::Copy,        FVector2D(-1.f, 0.f), TEXT("4") },
	};

	/** Is this ping still worth showing? (An officer-down ping ends once they're back up.) */
	bool IsPingLive(const AFTOPlayerState* Officer)
	{
		const FFTOCalloutPing& Ping = Officer->GetCallout();
		if (!FTORadio::HasPing(Ping.Callout) || Officer->GetCalloutAge() > FTORadio::PingSeconds)
		{
			return false;
		}
		if (Ping.Callout == EFTOCallout::OfficerDown)
		{
			const AFTOCharacter* Downed = Cast<AFTOCharacter>(Ping.Follow);
			return !Downed || Downed->IsDowned();
		}
		return true;
	}
}

void AFTOHUD::DrawRadio(const AFTOGameState* GS)
{
	const EFTOShiftPhase Phase = GS->GetShiftPhase();
	if (Phase == EFTOShiftPhase::Lobby || Phase == EFTOShiftPhase::OnDuty)
	{
		DrawRadioPings(GS);
	}

	// Who's on air: a flashing strip on the left for each officer keying the radio (ourselves included).
	const float S = UIScale();
	const float Time = GetWorld()->GetTimeSeconds();
	UFont* Font = GEngine->GetMediumFont();
	const AFTOPlayerController* MyPC = Cast<AFTOPlayerController>(GetOwningPlayerController());
	const APlayerState* MyState = MyPC ? MyPC->PlayerState : nullptr;
	float Y = Canvas->ClipY * 0.68f;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		const AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS);
		const bool bMe = Officer && Officer == MyState;
		const bool bOnAir = Officer && (bMe ? MyPC->IsTransmitting() : Officer->IsOnRadio());
		if (!bOnAir)
		{
			continue;
		}
		const FString Line = bMe ? TEXT("TRANSMITTING...") : FString::Printf(TEXT("%s on the radio"), *Officer->GetCallsign());
		float W = 0.f, H = 0.f;
		GetTextSize(Line, W, H, Font, S);
		const float X = 24.f * S;
		const float Blink = 0.6f + 0.4f * FMath::Abs(FMath::Sin(Time * 5.f));
		DrawPanel(X - 8.f * S, Y - 4.f * S, W + 46.f * S, H + 8.f * S, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		// A little speaker grille in the officer's colour, then their name.
		FLinearColor Badge = Officer->GetOfficerColor();
		Badge.A = Blink;
		for (int32 Bar = 0; Bar < 3; ++Bar)
		{
			const float BarH = (6.f + 5.f * Bar) * S * (0.6f + 0.4f * FMath::Abs(FMath::Sin(Time * 9.f + Bar)));
			DrawRect(Badge, X + Bar * 7.f * S, Y + H * 0.5f - BarH * 0.5f, 4.f * S, BarH);
		}
		DrawText(Line, FLinearColor(1.f, 1.f, 1.f, Blink), X + 28.f * S, Y, Font, S);
		Y += H + 14.f * S;
	}

	if (MyPC && MyPC->IsRadioWheelOpen())
	{
		DrawRadioWheel();
	}
}

void AFTOHUD::DrawRadioPings(const AFTOGameState* GS)
{
	const float S = UIScale();
	const APawn* Me = GetOwningPawn();
	UFont* Font = GEngine->GetSmallFont();
	const float Time = GetWorld()->GetTimeSeconds();

	for (const APlayerState* PS : GS->PlayerArray)
	{
		const AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS);
		if (!Officer || !IsPingLive(Officer))
		{
			continue;
		}
		const FFTOCalloutPing& Ping = Officer->GetCallout();
		const FVector Where = Ping.GetLocation();
		// Following our own pawn (we called backup, or we're the one down): nothing to point at.
		if (Me && Ping.Follow == Me)
		{
			continue;
		}

		FVector2D Screen;
		const bool bOnScreen = ProjectToScreenEdge(Where + FVector(0.f, 0.f, 260.f), 44.f * S, Screen);
		const float Age = Officer->GetCalloutAge();
		// Big and bouncy when it lands, then a steady pulse that fades in the last few seconds.
		const float Pop = 1.f + 0.8f * FMath::Max(0.f, 1.f - Age * 2.f);
		const float Pulse = 1.f + 0.2f * FMath::Sin(Time * 7.f);
		const float Fade = FMath::Clamp((FTORadio::PingSeconds - Age) / 3.f, 0.f, 1.f);
		const float Size = 12.f * S * Pop * Pulse;
		FLinearColor Color = FTORadio::CalloutColor(Ping.Callout);
		Color.A = Fade;

		// A ring of four ticks round a diamond in the caller's colour.
		FLinearColor Outline(0.f, 0.f, 0.f, 0.7f * Fade);
		DrawDiamond(Screen, Size + 3.f * S, Outline);
		DrawDiamond(Screen, Size, Color);
		FLinearColor Badge = Officer->GetOfficerColor();
		Badge.A = Fade;
		DrawDiamond(Screen, Size * 0.45f, Badge);
		const float Ring = Size * 1.9f;
		for (const FVector2D& Tick : { FVector2D(1.f, 0.f), FVector2D(-1.f, 0.f), FVector2D(0.f, 1.f), FVector2D(0.f, -1.f) })
		{
			DrawRect(Color, Screen.X + Tick.X * Ring - 3.f * S, Screen.Y + Tick.Y * Ring - 3.f * S, 6.f * S, 6.f * S);
		}

		FString Label = FTORadio::PingLabel(Ping.Callout);
		if (Me)
		{
			const int32 Meters = FMath::RoundToInt(FVector::Dist2D(Me->GetActorLocation(), Where) / 100.f);
			Label = bOnScreen ? FString::Printf(TEXT("%s (%s)  %dm"), *Label, *Officer->GetCallsign(), Meters) : FString::Printf(TEXT("%s  %dm"), *Label, Meters);
		}
		FLinearColor Text = FLinearColor::White;
		Text.A = Fade;
		DrawCenteredText(Label, Screen.X, Screen.Y + Ring + 6.f * S, Text, Font, S * 1.1f);
	}
}

void AFTOHUD::DrawRadioWheel()
{
	const AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetOwningPlayerController());
	if (!PC)
	{
		return;
	}
	const float S = UIScale();
	// A little above the middle, clear of the messages that stack up from lower down.
	const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.4f);
	const float Reach = 130.f * S;
	UFont* Font = GEngine->GetMediumFont();
	const EFTOCallout Choice = PC->GetWheelChoice();

	DrawPanel(Center.X - Reach - 120.f * S, Center.Y - Reach - 50.f * S, (Reach + 120.f * S) * 2.f, (Reach + 50.f * S) * 2.f, FLinearColor(0.f, 0.f, 0.f, 0.35f));
	DrawCenteredText(TEXT("RADIO"), Center.X, Center.Y - 14.f * S, FLinearColor(0.6f, 0.85f, 1.f), GEngine->GetLargeFont(), S);

	for (const FWheelSlice& Slice : WheelSlices)
	{
		const bool bPicked = Slice.Callout == Choice;
		const FString Line = FString::Printf(TEXT("[%s] %s"), Slice.Key, *FTORadio::CalloutLine(Slice.Callout).ToString());
		const float Scale = S * (bPicked ? 1.25f : 1.f);
		float W = 0.f, H = 0.f;
		GetTextSize(Line, W, H, Font, Scale);
		const FVector2D At = Center + Slice.Direction * Reach;
		const FLinearColor Color = FTORadio::CalloutColor(Slice.Callout);
		DrawPanel(At.X - W * 0.5f - 10.f * S, At.Y - H * 0.5f - 6.f * S, W + 20.f * S, H + 12.f * S,
			bPicked ? FLinearColor(Color.R * 0.5f, Color.G * 0.5f, Color.B * 0.5f, 0.9f) : FLinearColor(0.f, 0.f, 0.f, 0.7f));
		DrawCenteredText(Line, At.X, At.Y - H * 0.5f, bPicked ? FLinearColor::White : Color, Font, Scale);
	}

	// Where the mouse or stick points (screen up is aim +Y).
	const FVector2D Aim = PC->GetWheelAim();
	const FVector2D Dot = Center + FVector2D(Aim.X, -Aim.Y) * Reach * 0.6f;
	DrawRect(FLinearColor::White, Dot.X - 4.f * S, Dot.Y - 4.f * S, 8.f * S, 8.f * S);
	DrawCenteredText(TEXT("Point and let go of T, or press 1-4"), Center.X, Center.Y + Reach + 26.f * S, FLinearColor(0.8f, 0.8f, 0.8f), GEngine->GetSmallFont(), S * 1.1f);
}
