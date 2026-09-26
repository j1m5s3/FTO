#include "UI/FTOHUD.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerState.h"
#include "Crime/FTOIncident.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Core/FTOCharacter.h"
#include "Interaction/FTOInteractable.h"

namespace
{
	FLinearColor ChaosColor(float Alpha)
	{
		// Calm green -> nervous yellow -> panic red.
		return Alpha < 0.5f
			? FMath::Lerp(FLinearColor(0.2f, 0.9f, 0.3f), FLinearColor(1.f, 0.85f, 0.1f), Alpha * 2.f)
			: FMath::Lerp(FLinearColor(1.f, 0.85f, 0.1f), FLinearColor(1.f, 0.1f, 0.15f), (Alpha - 0.5f) * 2.f);
	}

	FString FormatClock(float Seconds)
	{
		const int32 Total = FMath::CeilToInt(FMath::Max(0.f, Seconds));
		return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
	}

	const TCHAR* ChaosMood(float Alpha)
	{
		if (Alpha < 0.2f) return TEXT("Suspiciously calm");
		if (Alpha < 0.4f) return TEXT("Business as usual");
		if (Alpha < 0.6f) return TEXT("Getting rowdy");
		if (Alpha < 0.8f) return TEXT("Mayhem brewing");
		return TEXT("TOTAL PANDEMONIUM");
	}
}

float AFTOHUD::UIScale() const
{
	return Canvas ? FMath::Clamp(Canvas->ClipY / 1080.f, 0.6f, 2.f) : 1.f;
}

void AFTOHUD::DrawHUD()
{
	Super::DrawHUD();

	const AFTOGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFTOGameState>() : nullptr;
	if (!GS || !Canvas)
	{
		return;
	}

	const float Delta = GetWorld()->GetDeltaSeconds();
	DisplayedChaos = FMath::FInterpTo(DisplayedChaos, GS->GetChaos(), Delta, 4.f);
	if (GS->GetChaos() > LastChaos + 1.f)
	{
		ChaosPulse = 1.f; // jolt the bar when chaos jumps
	}
	LastChaos = GS->GetChaos();
	ChaosPulse = FMath::Max(0.f, ChaosPulse - Delta * 2.f);

	switch (GS->GetShiftPhase())
	{
	case EFTOShiftPhase::Briefing:
		DrawChaosMeter(GS);
		DrawBriefing(GS);
		break;

	case EFTOShiftPhase::OnDuty:
		DrawIncidentMarkers(GS);
		DrawTeammateMarkers(GS);
		DrawChaosMeter(GS);
		DrawShiftClock(GS);
		DrawDispatchBoard(GS);
		DrawOnSceneProgress(GS);
		DrawInteractPrompt();
		break;

	default:
		DrawShiftReport(GS);
		break;
	}

	DrawToasts();
}

void AFTOHUD::DrawPanel(float X, float Y, float W, float H, const FLinearColor& Color)
{
	DrawRect(Color, X, Y, W, H);
}

void AFTOHUD::DrawCenteredText(const FString& Text, float CenterX, float Y, const FLinearColor& Color, UFont* Font, float Scale)
{
	float W = 0.f, H = 0.f;
	GetTextSize(Text, W, H, Font, Scale);
	DrawText(Text, Color, CenterX - W * 0.5f, Y, Font, Scale);
}

void AFTOHUD::DrawDiamond(const FVector2D& Center, float Size, const FLinearColor& Color)
{
	// Stack of shrinking rects reads as a chunky diamond without any textures.
	const int32 Steps = 6;
	const float RowH = Size / Steps;
	for (int32 i = 0; i < Steps; ++i)
	{
		// Rows widen from the tips towards the middle.
		const float Half = Size * (i + 0.5f) / Steps;
		DrawRect(Color, Center.X - Half, Center.Y - Size + i * RowH, Half * 2.f, RowH + 0.5f);
		DrawRect(Color, Center.X - Half, Center.Y + Size - (i + 1) * RowH, Half * 2.f, RowH + 0.5f);
	}
}

void AFTOHUD::DrawChaosMeter(const AFTOGameState* GS)
{
	const float S = UIScale();
	const float Alpha = DisplayedChaos / AFTOGameState::MaxChaos;
	const float Shake = (Alpha > 0.75f ? FMath::Sin(GetWorld()->GetTimeSeconds() * 40.f) * 3.f * S : 0.f) + ChaosPulse * 6.f * S;

	const float W = 600.f * S;
	const float H = 34.f * S;
	const float X = (Canvas->ClipX - W) * 0.5f + Shake;
	const float Y = 24.f * S;

	UFont* Font = GEngine->GetMediumFont();
	DrawPanel(X - 6.f * S, Y - 6.f * S, W + 12.f * S, H + 40.f * S);
	DrawRect(FLinearColor(0.1f, 0.1f, 0.1f, 0.9f), X, Y, W, H);
	DrawRect(ChaosColor(Alpha), X, Y, W * Alpha, H);

	// Tick marks every 25%.
	for (int32 i = 1; i < 4; ++i)
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X + W * i * 0.25f - 1.f, Y, 2.f, H);
	}

	DrawCenteredText(FString::Printf(TEXT("CITY CHAOS  %d%%"), FMath::RoundToInt(DisplayedChaos)), X + W * 0.5f, Y + 6.f * S, FLinearColor::White, Font, S);
	DrawCenteredText(ChaosMood(Alpha), X + W * 0.5f, Y + H + 6.f * S, ChaosColor(Alpha), GEngine->GetSmallFont(), S * 1.2f);
}

void AFTOHUD::DrawShiftClock(const AFTOGameState* GS)
{
	const float S = UIScale();
	UFont* Font = GEngine->GetLargeFont();
	const FString Clock = FString::Printf(TEXT("SHIFT  %s"), *FormatClock(GS->GetShiftTimeRemaining()));

	float W = 0.f, H = 0.f;
	GetTextSize(Clock, W, H, Font, S);
	const float X = Canvas->ClipX - W - 40.f * S;
	const float Y = 24.f * S;
	DrawPanel(X - 12.f * S, Y - 8.f * S, W + 24.f * S, H + 16.f * S);
	DrawText(Clock, FLinearColor::White, X, Y, Font, S);
}

void AFTOHUD::DrawDispatchBoard(const AFTOGameState* GS)
{
	const float S = UIScale();
	const APawn* Me = GetOwningPawn();

	TArray<const AFTOIncident*> Calls;
	for (const AFTOIncident* Incident : GS->GetIncidents())
	{
		if (Incident && Incident->IsActive() && Incident->IsKnownToDispatch())
		{
			Calls.Add(Incident);
		}
	}

	// Biggest and most urgent first.
	Calls.Sort([](const AFTOIncident& A, const AFTOIncident& B)
	{
		if (A.GetInfo().Tier != B.GetInfo().Tier) { return A.GetInfo().Tier > B.GetInfo().Tier; }
		return A.GetUrgency() > B.GetUrgency();
	});

	UFont* TitleFont = GEngine->GetMediumFont();
	UFont* SmallFont = GEngine->GetSmallFont();

	const float X = 24.f * S;
	float Y = 120.f * S;
	const float W = 420.f * S;
	const float RowH = 58.f * S;

	const int32 Rows = FMath::Min(Calls.Num(), MaxDispatchRows);
	DrawPanel(X - 8.f * S, Y - 40.f * S, W + 16.f * S, 44.f * S + FMath::Max(1, Rows) * RowH);
	DrawText(FString::Printf(TEXT("DISPATCH  (%d open)"), Calls.Num()), FLinearColor(0.6f, 0.8f, 1.f), X, Y - 32.f * S, TitleFont, S);

	if (Calls.Num() == 0)
	{
		DrawText(TEXT("All quiet... for now."), FLinearColor(0.7f, 0.7f, 0.7f), X, Y + 10.f * S, SmallFont, S * 1.2f);
		return;
	}

	for (int32 i = 0; i < Rows; ++i)
	{
		const AFTOIncident* Incident = Calls[i];
		const FFTOIncidentInfo Info = Incident->GetInfo();
		const FLinearColor TierColor = FTOCrime::TierColor(Info.Tier);

		DrawRect(TierColor, X, Y + 4.f * S, 8.f * S, RowH - 12.f * S);

		const float Distance = Me ? FVector::Dist2D(Me->GetActorLocation(), Incident->GetActorLocation()) / 100.f : 0.f;
		DrawText(Info.Title.ToString(), FLinearColor::White, X + 18.f * S, Y + 2.f * S, TitleFont, S * 0.9f);

		const FString Detail = FString::Printf(TEXT("%s  |  %d/%d officers  |  %dm%s"),
			*FTOCrime::TierName(Info.Tier).ToString(), Incident->GetOfficersOnScene(), Info.OfficersRequired,
			FMath::RoundToInt(Distance), Incident->WasWitnessed() ? TEXT("  |  SPOTTED") : TEXT(""));
		DrawText(Detail, FLinearColor(0.8f, 0.8f, 0.8f), X + 18.f * S, Y + 26.f * S, SmallFont, S * 1.1f);

		// Urgency fuse along the bottom of the row.
		const float Urgency = Incident->GetUrgency();
		DrawRect(FLinearColor(0.2f, 0.2f, 0.2f, 0.8f), X + 18.f * S, Y + RowH - 12.f * S, W - 26.f * S, 4.f * S);
		DrawRect(ChaosColor(Urgency), X + 18.f * S, Y + RowH - 12.f * S, (W - 26.f * S) * Urgency, 4.f * S);

		Y += RowH;
	}
}

bool AFTOHUD::ProjectToScreenEdge(const FVector& World, float Margin, FVector2D& OutScreen) const
{
	const APlayerController* PC = GetOwningPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return false;
	}

	const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
	const FVector CamFwd = PC->PlayerCameraManager->GetCameraRotation().Vector();
	const bool bBehind = FVector::DotProduct(World - CamLoc, CamFwd) < 0.f;

	FVector2D Screen;
	const bool bProjected = PC->ProjectWorldLocationToScreen(World, Screen, true);
	const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);

	if (bProjected && !bBehind &&
		Screen.X >= Margin && Screen.X <= Canvas->ClipX - Margin &&
		Screen.Y >= Margin && Screen.Y <= Canvas->ClipY - Margin)
	{
		OutScreen = Screen;
		return true;
	}

	// Off-screen: push along the direction from centre to the edge.
	FVector2D Dir = Screen - Center;
	if (bBehind)
	{
		Dir = -Dir;
	}
	if (Dir.IsNearlyZero())
	{
		Dir = FVector2D(0.f, 1.f);
	}
	const float HalfW = Canvas->ClipX * 0.5f - Margin;
	const float HalfH = Canvas->ClipY * 0.5f - Margin;
	const float Scale = FMath::Min(HalfW / FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(Dir.X)), HalfH / FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(Dir.Y)));
	OutScreen = Center + Dir * Scale;
	return false;
}

void AFTOHUD::DrawIncidentMarkers(const AFTOGameState* GS)
{
	const float S = UIScale();
	const APawn* Me = GetOwningPawn();
	UFont* Font = GEngine->GetSmallFont();
	const float Time = GetWorld()->GetTimeSeconds();

	for (const AFTOIncident* Incident : GS->GetIncidents())
	{
		if (!Incident || !Incident->IsActive() || !Incident->IsKnownToDispatch())
		{
			continue;
		}

		const FFTOIncidentInfo Info = Incident->GetInfo();
		FVector2D Screen;
		const bool bOnScreen = ProjectToScreenEdge(Incident->GetActorLocation() + FVector(0.f, 0.f, 300.f), 40.f * S, Screen);

		// Critical incidents pulse.
		const float Pulse = Info.Tier == EFTOCrimeTier::Critical ? 1.f + 0.25f * FMath::Sin(Time * 8.f) : 1.f;
		const float Size = (bOnScreen ? 14.f : 11.f) * S * Pulse;
		DrawDiamond(Screen, Size + 3.f * S, FLinearColor(0.f, 0.f, 0.f, 0.7f));
		DrawDiamond(Screen, Size, FTOCrime::TierColor(Info.Tier));

		if (Me)
		{
			const int32 Meters = FMath::RoundToInt(FVector::Dist2D(Me->GetActorLocation(), Incident->GetActorLocation()) / 100.f);
			const FString Label = bOnScreen ? FString::Printf(TEXT("%s  %dm"), *Info.Title.ToString(), Meters) : FString::Printf(TEXT("%dm"), Meters);
			DrawCenteredText(Label, Screen.X, Screen.Y + Size + 4.f * S, FLinearColor::White, Font, S);
		}
	}
}

void AFTOHUD::DrawTeammateMarkers(const AFTOGameState* GS)
{
	const float S = UIScale();
	const APawn* Me = GetOwningPawn();
	UFont* Font = GEngine->GetSmallFont();

	for (const APlayerState* PS : GS->PlayerArray)
	{
		const AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS);
		const APawn* Pawn = Officer ? Officer->GetPawn() : nullptr;
		if (!Pawn || Pawn == Me)
		{
			continue;
		}

		FVector2D Screen;
		ProjectToScreenEdge(Pawn->GetActorLocation() + FVector(0.f, 0.f, 160.f), 30.f * S, Screen);
		const FLinearColor Color = Officer->GetOfficerColor();
		DrawRect(FLinearColor::Black, Screen.X - 7.f * S, Screen.Y - 7.f * S, 14.f * S, 14.f * S);
		DrawRect(Color, Screen.X - 5.f * S, Screen.Y - 5.f * S, 10.f * S, 10.f * S);
		DrawCenteredText(Officer->GetPlayerName(), Screen.X, Screen.Y - 26.f * S, Color, Font, S);
	}
}

void AFTOHUD::DrawOnSceneProgress(const AFTOGameState* GS)
{
	const APawn* Me = GetOwningPawn();
	if (!Me)
	{
		return;
	}

	const AFTOIncident* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (const AFTOIncident* Incident : GS->GetIncidents())
	{
		if (!Incident || !Incident->IsActive())
		{
			continue;
		}
		const float DistSq = FVector::DistSquared2D(Me->GetActorLocation(), Incident->GetActorLocation());
		if (DistSq <= FMath::Square(Incident->SceneRadius) && DistSq < NearestDistSq)
		{
			Nearest = Incident;
			NearestDistSq = DistSq;
		}
	}
	if (!Nearest)
	{
		return;
	}

	const float S = UIScale();
	const FFTOIncidentInfo Info = Nearest->GetInfo();
	const float W = 520.f * S;
	const float H = 22.f * S;
	const float X = (Canvas->ClipX - W) * 0.5f;
	const float Y = Canvas->ClipY - 170.f * S;

	UFont* Font = GEngine->GetMediumFont();
	DrawPanel(X - 10.f * S, Y - 70.f * S, W + 20.f * S, H + 110.f * S);
	DrawCenteredText(Info.Title.ToString(), X + W * 0.5f, Y - 62.f * S, FTOCrime::TierColor(Info.Tier), Font, S * 1.1f);
	DrawCenteredText(Info.Description.ToString(), X + W * 0.5f, Y - 32.f * S, FLinearColor(0.85f, 0.85f, 0.85f), GEngine->GetSmallFont(), S * 1.1f);

	DrawRect(FLinearColor(0.1f, 0.1f, 0.1f, 0.9f), X, Y, W, H);
	DrawRect(FLinearColor(0.2f, 0.7f, 1.f), X, Y, W * Nearest->GetProgress(), H);

	const bool bUnderstaffed = Nearest->GetOfficersOnScene() < Info.OfficersRequired;
	const FString Status = bUnderstaffed
		? FString::Printf(TEXT("Need backup! %d/%d officers on scene"), Nearest->GetOfficersOnScene(), Info.OfficersRequired)
		: FString::Printf(TEXT("Handling it... %d/%d officers"), Nearest->GetOfficersOnScene(), Info.OfficersRequired);
	DrawCenteredText(Status, X + W * 0.5f, Y + H + 8.f * S, bUnderstaffed ? FLinearColor(1.f, 0.6f, 0.2f) : FLinearColor::White, GEngine->GetSmallFont(), S * 1.2f);
}

void AFTOHUD::DrawBriefing(const AFTOGameState* GS)
{
	const float S = UIScale();
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.35f;

	DrawPanel(CX - 380.f * S, CY - 30.f * S, 760.f * S, 170.f * S);
	DrawCenteredText(TEXT("ROLL CALL"), CX, CY - 20.f * S, FLinearColor(0.6f, 0.8f, 1.f), GEngine->GetLargeFont(), S * 1.5f);
	DrawCenteredText(TEXT("Keep the city's chaos under 100% until the end of the shift."), CX, CY + 30.f * S, FLinearColor::White, GEngine->GetMediumFont(), S);
	DrawCenteredText(FString::Printf(TEXT("On duty in %s"), *FormatClock(GS->GetBriefingTimeRemaining())), CX, CY + 70.f * S, FLinearColor(1.f, 0.85f, 0.2f), GEngine->GetLargeFont(), S);
	DrawCenteredText(TEXT("WASD move  |  Shift sprint  |  Space jump  |  E interact  |  stand at a scene to handle it"), CX, CY + 110.f * S, FLinearColor(0.7f, 0.7f, 0.7f), GEngine->GetSmallFont(), S * 1.1f);
}

void AFTOHUD::DrawShiftReport(const AFTOGameState* GS)
{
	const float S = UIScale();
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.25f;
	const bool bSurvived = GS->GetShiftPhase() == EFTOShiftPhase::Survived;

	static const TCHAR* WinHeadlines[] =
	{
		TEXT("LOCAL COPS ONLY MILDLY RESPONSIBLE FOR MAYHEM"),
		TEXT("CITY SURVIVES ANOTHER DAY, SOMEHOW"),
		TEXT("CRIME DOWN, DONUT SALES UP"),
	};
	static const TCHAR* LoseHeadlines[] =
	{
		TEXT("CITY DESCENDS INTO CHAOS; PRECINCT 'ON BREAK'"),
		TEXT("MAYOR HIDES UNDER DESK, CITES 'VIBES'"),
		TEXT("GOOSE NOW EFFECTIVELY IN CHARGE OF DOWNTOWN"),
	};
	const int32 Pick = FMath::Abs(GS->ShiftSeed) % 3;

	DrawPanel(CX - 460.f * S, CY - 30.f * S, 920.f * S, 400.f * S, FLinearColor(0.f, 0.f, 0.f, 0.75f));
	DrawCenteredText(bSurvived ? TEXT("SHIFT SURVIVED!") : TEXT("THE CITY FELL INTO CHAOS"), CX, CY - 20.f * S,
		bSurvived ? FLinearColor(0.3f, 1.f, 0.4f) : FLinearColor(1.f, 0.25f, 0.25f), GEngine->GetLargeFont(), S * 1.6f);
	DrawCenteredText(FString::Printf(TEXT("\"%s\""), bSurvived ? WinHeadlines[Pick] : LoseHeadlines[Pick]), CX, CY + 40.f * S,
		FLinearColor(1.f, 0.9f, 0.6f), GEngine->GetMediumFont(), S);

	const FString Lines[] =
	{
		FString::Printf(TEXT("Incidents handled:     %d"), GS->IncidentsResolved),
		FString::Printf(TEXT("Caught in the act:     %d"), GS->IncidentsWitnessed),
		FString::Printf(TEXT("Traffic stops:         %d"), GS->TrafficStops),
		FString::Printf(TEXT("Went cold / escalated: %d"), GS->IncidentsFailed),
		FString::Printf(TEXT("Peak chaos:            %d%%"), FMath::RoundToInt(GS->PeakChaos)),
	};
	float Y = CY + 100.f * S;
	for (const FString& Line : Lines)
	{
		DrawCenteredText(Line, CX, Y, FLinearColor::White, GEngine->GetMediumFont(), S * 1.1f);
		Y += 40.f * S;
	}
}

void AFTOHUD::AddToast(const FText& Message, const FLinearColor& Color)
{
	FToast& Toast = Toasts.AddDefaulted_GetRef();
	Toast.Text = Message.ToString();
	Toast.Color = Color;
	Toast.ExpireTime = GetWorld()->GetTimeSeconds() + 5.f;

	while (Toasts.Num() > 4)
	{
		Toasts.RemoveAt(0);
	}
}

void AFTOHUD::DrawToasts()
{
	const float Now = GetWorld()->GetTimeSeconds();
	Toasts.RemoveAll([Now](const FToast& T) { return T.ExpireTime <= Now; });

	const float S = UIScale();
	UFont* Font = GEngine->GetMediumFont();
	float Y = Canvas->ClipY * 0.62f;
	for (int32 i = Toasts.Num() - 1; i >= 0; --i)
	{
		const FToast& Toast = Toasts[i];
		const float Fade = FMath::Clamp(Toast.ExpireTime - Now, 0.f, 1.f);
		FLinearColor Color = Toast.Color;
		Color.A = Fade;

		float W = 0.f, H = 0.f;
		GetTextSize(Toast.Text, W, H, Font, S);
		DrawPanel(Canvas->ClipX * 0.5f - W * 0.5f - 12.f * S, Y - 4.f * S, W + 24.f * S, H + 8.f * S, FLinearColor(0.f, 0.f, 0.f, 0.6f * Fade));
		DrawCenteredText(Toast.Text, Canvas->ClipX * 0.5f, Y, Color, Font, S);
		Y -= H + 14.f * S;
	}
}

void AFTOHUD::DrawInteractPrompt()
{
	const AFTOCharacter* Officer = Cast<AFTOCharacter>(GetOwningPawn());
	AActor* Target = Officer ? Officer->GetFocusedInteractable() : nullptr;
	const IFTOInteractable* Interactable = Cast<IFTOInteractable>(Target);
	if (!Interactable)
	{
		return;
	}

	FVector2D Screen;
	if (!ProjectToScreenEdge(Interactable->GetInteractLocation() + FVector(0.f, 0.f, 180.f), 40.f, Screen))
	{
		return;
	}

	const float S = UIScale();
	UFont* Font = GEngine->GetMediumFont();
	const FString Text = FString::Printf(TEXT("[E]  %s"), *Interactable->GetInteractPrompt(Officer).ToString());
	float W = 0.f, H = 0.f;
	GetTextSize(Text, W, H, Font, S);
	DrawPanel(Screen.X - W * 0.5f - 10.f * S, Screen.Y - 4.f * S, W + 20.f * S, H + 8.f * S, FLinearColor(0.05f, 0.1f, 0.25f, 0.85f));
	DrawCenteredText(Text, Screen.X, Screen.Y, FLinearColor(1.f, 0.95f, 0.6f), Font, S);
}
