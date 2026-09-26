// Scoring on the HUD: "+250 ARREST! x2" popups in the world, our score and combo on duty, and the end-of-shift
// scoreboard that counts up while the squad dances (or slumps) outside the precinct.
#include "UI/FTOHUD.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Core/FTOPlayerState.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Kismet/GameplayStatics.h"
#include "Scoring/FTOScoring.h"

namespace
{
	constexpr float PopupSeconds = 1.8f;
	/** The scoreboard: rows start counting after a beat, the grade stamps down once they're done. */
	constexpr float CountStart = 0.8f;
	constexpr float CountSeconds = 2.4f;
	constexpr float GradeAt = CountStart + CountSeconds + 0.4f;

	FString WithCommas(int32 Value)
	{
		return FText::AsNumber(Value).ToString();
	}

	FLinearColor GradeColor(const FString& Grade)
	{
		if (Grade == TEXT("S")) return FLinearColor(1.f, 0.8f, 0.15f);
		if (Grade == TEXT("A")) return FLinearColor(0.35f, 1.f, 0.45f);
		if (Grade == TEXT("B")) return FLinearColor(0.45f, 0.8f, 1.f);
		if (Grade == TEXT("C")) return FLinearColor(0.9f, 0.9f, 0.9f);
		if (Grade == TEXT("D")) return FLinearColor(1.f, 0.6f, 0.3f);
		return FLinearColor(1.f, 0.25f, 0.25f);
	}
}

void AFTOHUD::AddScorePopup(const AFTOPlayerState* Officer, int32 Points, EFTOScore Event, const FVector& Where, int32 Combo)
{
	const APlayerController* PC = GetOwningPlayerController();
	FScorePopup& Popup = ScorePopups.AddDefaulted_GetRef();
	Popup.bMine = Officer && PC && Officer == PC->PlayerState;
	Popup.Where = Where;
	Popup.Start = GetWorld()->GetTimeSeconds();
	Popup.Text = FString::Printf(TEXT("%s%d %s"), Points >= 0 ? TEXT("+") : TEXT(""), Points, *FTOScoring::Label(Event));
	if (Combo > 1)
	{
		Popup.Text += FString::Printf(TEXT(" x%s"), *FString::SanitizeFloat(FTOScoring::ComboMultiplier(Combo)));
	}
	// Ours in gold (red when it's a penalty); a partner's in their badge colour.
	Popup.Color = Points < 0 ? FLinearColor(1.f, 0.3f, 0.25f) : (Popup.bMine ? FLinearColor(1.f, 0.85f, 0.2f) : (Officer ? Officer->GetOfficerColor() : FLinearColor::White));
	if (Popup.bMine)
	{
		UGameplayStatics::PlaySound2D(this, Points >= 0 ? AFTOGameState::Sounds().Chime : AFTOGameState::Sounds().Fail, Points >= 0 ? 0.35f : 0.5f, Points >= 0 ? 1.f + 0.08f * (Combo - 1) : 1.f);
	}
	while (ScorePopups.Num() > 12)
	{
		ScorePopups.RemoveAt(0);
	}
}

void AFTOHUD::DrawScorePopups()
{
	const float Now = GetWorld()->GetTimeSeconds();
	ScorePopups.RemoveAll([Now](const FScorePopup& P) { return Now - P.Start > PopupSeconds; });
	const float S = UIScale();
	UFont* Font = GEngine->GetLargeFont();
	for (const FScorePopup& Popup : ScorePopups)
	{
		const float T = (Now - Popup.Start) / PopupSeconds;
		// Rise as it goes, pop in big, then settle and fade.
		FVector2D Screen;
		const APlayerController* PC = GetOwningPlayerController();
		if (!PC || !PC->ProjectWorldLocationToScreen(Popup.Where + FVector(0.f, 0.f, 140.f * T), Screen, true))
		{
			continue;
		}
		const float Pop = T < 0.12f ? FMath::Lerp(0.4f, 1.35f, T / 0.12f) : FMath::Lerp(1.35f, 1.f, FMath::Min(1.f, (T - 0.12f) / 0.2f));
		const float Scale = S * Pop * (Popup.bMine ? 1.9f : 1.3f);
		FLinearColor Color = Popup.Color;
		Color.A = FMath::Clamp((1.f - T) / 0.3f, 0.f, 1.f);
		float W = 0.f, H = 0.f;
		GetTextSize(Popup.Text, W, H, Font, Scale);
		// A dark drop shadow keeps it readable over anything.
		DrawText(Popup.Text, FLinearColor(0.f, 0.f, 0.f, Color.A * 0.8f), Screen.X - W * 0.5f + 2.f * S, Screen.Y - H * 0.5f + 2.f * S, Font, Scale);
		DrawText(Popup.Text, Color, Screen.X - W * 0.5f, Screen.Y - H * 0.5f, Font, Scale);
	}
}

void AFTOHUD::DrawScoreTicker()
{
	ScoreboardShownTime = -1.f; // on duty (again): the next scoreboard counts up from scratch
	if (AFTOPlayerController* FTOPC = Cast<AFTOPlayerController>(GetOwningPlayerController()))
	{
		FTOPC->EndDebrief();
	}
	const APlayerController* PC = GetOwningPlayerController();
	const AFTOPlayerState* Me = PC ? PC->GetPlayerState<AFTOPlayerState>() : nullptr;
	if (!Me)
	{
		return;
	}
	// Under the shift clock: our score, and the combo with the time left to keep it going.
	const float S = UIScale();
	UFont* Font = GEngine->GetMediumFont();
	const FString Line = FString::Printf(TEXT("SCORE  %s"), *WithCommas(Me->GetShiftScore()));
	float W = 0.f, H = 0.f;
	GetTextSize(Line, W, H, Font, S * 1.2f);
	const float X = Canvas->ClipX - W - 40.f * S;
	const float Y = 84.f * S;
	DrawPanel(X - 12.f * S, Y - 6.f * S, W + 24.f * S, H + 12.f * S);
	DrawText(Line, FLinearColor(1.f, 0.85f, 0.2f), X, Y, Font, S * 1.2f);

	const int32 Combo = Me->GetCombo();
	if (Combo > 1)
	{
		const FString ComboLine = FString::Printf(TEXT("COMBO x%s"), *FString::SanitizeFloat(FTOScoring::ComboMultiplier(Combo)));
		float CW = 0.f, CH = 0.f;
		GetTextSize(ComboLine, CW, CH, Font, S);
		const float CX = Canvas->ClipX - CW - 40.f * S;
		const float CY = Y + H + 14.f * S;
		const float Wobble = 1.f + 0.06f * FMath::Sin(GetWorld()->GetTimeSeconds() * 12.f);
		DrawPanel(CX - 12.f * S, CY - 4.f * S, CW + 24.f * S, CH + 14.f * S, FLinearColor(0.3f, 0.12f, 0.f, 0.7f));
		DrawText(ComboLine, FLinearColor(1.f, 0.55f, 0.15f), CX, CY, Font, S * Wobble);
		const AGameStateBase* GS = GetWorld()->GetGameState();
		const float Left = GS ? 1.f - FMath::Clamp((GS->GetServerWorldTimeSeconds() - Me->GetComboTime()) / FTOScoring::ComboWindow, 0.f, 1.f) : 0.f;
		DrawRect(FLinearColor(1.f, 0.55f, 0.15f), CX, CY + CH + 4.f * S, CW * Left, 3.f * S);
	}
}

void AFTOHUD::DrawScoreboard(const AFTOGameState* GS)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (ScoreboardShownTime < 0.f)
	{
		ScoreboardShownTime = Now;
		// Swing round to the squad lined up outside the precinct.
		if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetOwningPlayerController()))
		{
			PC->ShowDebrief();
		}
	}
	const float T = Now - ScoreboardShownTime;
	const float Count = FMath::Clamp((T - CountStart) / CountSeconds, 0.f, 1.f);
	const float Eased = 1.f - FMath::Pow(1.f - Count, 3.f);
	if (Count > 0.f && Count < 1.f && Now - LastCountTick > 0.07f)
	{
		LastCountTick = Now;
		UGameplayStatics::PlaySound2D(this, AFTOGameState::Sounds().Click, 0.25f, 1.f + Count);
	}

	const float S = UIScale();
	const float CX = Canvas->ClipX * 0.5f;
	const float Top = 24.f * S;
	const bool bSurvived = GS->GetShiftPhase() == EFTOShiftPhase::Survived;
	UFont* Large = GEngine->GetLargeFont();
	UFont* Medium = GEngine->GetMediumFont();
	UFont* Small = GEngine->GetSmallFont();

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

	// The squad, best first.
	TArray<const AFTOPlayerState*> Squad;
	int32 TeamScore = 0;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS))
		{
			Squad.Add(Officer);
			TeamScore += Officer->GetShiftScore();
		}
	}
	Squad.Sort([](const AFTOPlayerState& A, const AFTOPlayerState& B) { return A.GetShiftScore() > B.GetShiftScore(); });

	const float W = 1120.f * S;
	const float RowH = 72.f * S;
	const float H = (140.f + FMath::Max(3, Squad.Num()) * 72.f + 44.f) * S;
	const float X = CX - W * 0.5f;
	DrawPanel(X, Top, W, H, FLinearColor(0.f, 0.f, 0.f, 0.72f));
	DrawCenteredText(bSurvived ? TEXT("SHIFT SURVIVED!") : TEXT("THE CITY FELL INTO CHAOS"), CX, Top + 10.f * S,
		bSurvived ? FLinearColor(0.3f, 1.f, 0.4f) : FLinearColor(1.f, 0.25f, 0.25f), Large, S * 2.2f);
	DrawCenteredText(FString::Printf(TEXT("\"%s\""), bSurvived ? WinHeadlines[Pick] : LoseHeadlines[Pick]), CX, Top + 70.f * S,
		FLinearColor(1.f, 0.9f, 0.6f), Medium, S * 1.3f);

	// Left: the squad total and the grade, stamped down once the rows have counted up.
	const float LeftW = 250.f * S;
	const float BodyTop = Top + 118.f * S;
	DrawCenteredText(TEXT("SQUAD SCORE"), X + LeftW * 0.5f, BodyTop, FLinearColor(0.7f, 0.8f, 1.f), Small, S * 1.2f);
	DrawCenteredText(WithCommas(FMath::RoundToInt(TeamScore * Eased)), X + LeftW * 0.5f, BodyTop + 22.f * S, FLinearColor::White, Large, S * 1.8f);
	if (T >= GradeAt)
	{
		const FString Grade = FTOScoring::Grade(TeamScore, Squad.Num(), bSurvived, GS->PeakChaos);
		const float Stamp = FMath::Lerp(2.2f, 1.f, FMath::Clamp((T - GradeAt) / 0.18f, 0.f, 1.f));
		const float GY = BodyTop + 84.f * S;
		DrawCenteredText(TEXT("GRADE"), X + LeftW * 0.5f, GY, FLinearColor(0.7f, 0.8f, 1.f), Small, S * 1.2f);
		DrawCenteredText(Grade, X + LeftW * 0.5f, GY + 18.f * S, GradeColor(Grade), Large, S * 7.f * Stamp);
	}

	// Right: a row per officer.
	const float RowX = X + LeftW + 10.f * S;
	const float RowW = W - LeftW - 30.f * S;
	float Y = BodyTop;
	for (int32 Rank = 0; Rank < Squad.Num(); ++Rank)
	{
		const AFTOPlayerState* Officer = Squad[Rank];
		const FFTOOfficerStats& Stats = Officer->GetStats();
		const bool bMe = GetOwningPlayerController() && Officer == GetOwningPlayerController()->PlayerState;
		DrawRect(bMe ? FLinearColor(1.f, 1.f, 1.f, 0.08f) : FLinearColor(1.f, 1.f, 1.f, 0.03f), RowX, Y, RowW, RowH - 6.f * S);
		DrawRect(Officer->GetOfficerColor(), RowX, Y, 8.f * S, RowH - 6.f * S);

		DrawText(FString::Printf(TEXT("%d. %s"), Rank + 1, *Officer->GetCallsign()), Officer->GetOfficerColor(), RowX + 18.f * S, Y + 2.f * S, Medium, S * 1.4f);
		DrawText(Officer->GetPlayerName(), FLinearColor(0.75f, 0.75f, 0.75f), RowX + 18.f * S, Y + 32.f * S, Small, S * 1.1f);

		TArray<FString> Bits;
		auto Add = [&Bits](int32 N, const TCHAR* One, const TCHAR* Many) { if (N > 0) { Bits.Add(FString::Printf(TEXT("%d %s"), N, N == 1 ? One : Many)); } };
		Add(Stats.Arrests, TEXT("arrest"), TEXT("arrests"));
		Add(Stats.Busts, TEXT("bust"), TEXT("busts"));
		Add(Stats.Booked, TEXT("booked"), TEXT("booked"));
		Add(Stats.Tickets, TEXT("ticket"), TEXT("tickets"));
		Add(Stats.CallsHandled, TEXT("call"), TEXT("calls"));
		Add(Stats.Revives, TEXT("revive"), TEXT("revives"));
		Add(Stats.Collateral + Stats.FriendlyFire, TEXT("oops"), TEXT("oopses"));
		DrawText(Bits.IsEmpty() ? TEXT("Mostly admired the scenery") : FString::Join(Bits, TEXT("  |  ")), FLinearColor(0.85f, 0.85f, 0.85f), RowX + 190.f * S, Y + 6.f * S, Small, S * 1.1f);
		const TArray<FString> Awards = FTOScoring::AwardsFor(Officer, Squad);
		if (!Awards.IsEmpty() && Count >= 1.f)
		{
			DrawText(FString::Join(Awards, TEXT("  *  ")), FLinearColor(1.f, 0.8f, 0.3f), RowX + 190.f * S, Y + 32.f * S, Small, S * 1.1f);
		}

		const FString Points = WithCommas(FMath::RoundToInt(Stats.Score * Eased));
		float PW = 0.f, PH = 0.f;
		GetTextSize(Points, PW, PH, Large, S * 1.6f);
		DrawText(Points, bMe ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor::White, RowX + RowW - PW - 16.f * S, Y + (RowH - 6.f * S - PH) * 0.5f, Large, S * 1.6f);
		Y += RowH;
	}

	// Along the bottom: how the city fared.
	const FString Footer = FString::Printf(TEXT("Handled %d  |  Caught in the act %d  |  Traffic stops %d  |  Booked %d  |  Went cold %d  |  Citizens bowled over %d  |  Peak chaos %d%%"),
		GS->IncidentsResolved, GS->IncidentsWitnessed, GS->TrafficStops, GS->SuspectsBooked, GS->IncidentsFailed, GS->CiviliansBowledOver, FMath::RoundToInt(GS->PeakChaos));
	DrawCenteredText(Footer, CX, Top + H - 32.f * S, FLinearColor(0.75f, 0.75f, 0.75f), Small, S * 1.1f);
}

void AFTOHUD::DrawOvertimeVote(const AFTOGameState* GS)
{
	const float S = UIScale();
	const float CX = Canvas->ClipX * 0.5f;
	const float Top = Canvas->ClipY * 0.2f;
	const float W = 760.f * S;
	const float H = 300.f * S;
	UFont* Large = GEngine->GetLargeFont();
	UFont* Medium = GEngine->GetMediumFont();
	UFont* Small = GEngine->GetSmallFont();
	const APlayerController* PC = GetOwningPlayerController();
	const float Time = GetWorld()->GetTimeSeconds();

	DrawPanel(CX - W * 0.5f, Top, W, H, FLinearColor(0.f, 0.f, 0.f, 0.78f));
	const float Flash = 0.75f + 0.25f * FMath::Sin(Time * 6.f);
	DrawCenteredText(TEXT("END OF SHIFT!"), CX, Top + 12.f * S, FLinearColor(1.f, 0.8f * Flash, 0.2f), Large, S * 2.f);
	DrawCenteredText(TEXT("The city's still standing. Keep going?"), CX, Top + 66.f * S, FLinearColor::White, Medium, S * 1.2f);

	// The time left to decide.
	const float Left = GS->GetVoteTimeRemaining();
	const float BarW = W - 80.f * S;
	DrawRect(FLinearColor(0.15f, 0.15f, 0.15f, 0.9f), CX - BarW * 0.5f, Top + 104.f * S, BarW, 8.f * S);
	DrawRect(FLinearColor(1.f, 0.75f, 0.2f), CX - BarW * 0.5f, Top + 104.f * S, BarW * FMath::Clamp(Left / FMath::Max(1.f, GS->GetVoteDuration()), 0.f, 1.f), 8.f * S);

	// The two choices, ours lit up.
	const AFTOPlayerState* Me = PC ? PC->GetPlayerState<AFTOPlayerState>() : nullptr;
	const EFTOShiftVote Mine = Me ? Me->GetShiftVote() : EFTOShiftVote::None;
	const int32 Offer = FMath::RoundToInt(GS->GetOvertimeOffer());
	const FString OvertimeText = FString::Printf(TEXT("[Y] OVERTIME  +%d:%02d"), Offer / 60, Offer % 60);
	struct FChoice { EFTOShiftVote Vote; const TCHAR* Text; FLinearColor Color; float X; };
	const FChoice Choices[] =
	{
		{ EFTOShiftVote::Overtime, *OvertimeText, FLinearColor(1.f, 0.7f, 0.2f), CX - W * 0.25f },
		{ EFTOShiftVote::ClockOff, TEXT("[N] CLOCK OFF"), FLinearColor(0.4f, 1.f, 0.5f), CX + W * 0.25f },
	};
	for (const FChoice& Choice : Choices)
	{
		const bool bPicked = Mine == Choice.Vote;
		float TW = 0.f, TH = 0.f;
		GetTextSize(Choice.Text, TW, TH, Medium, S * 1.3f);
		DrawPanel(Choice.X - TW * 0.5f - 16.f * S, Top + 128.f * S, TW + 32.f * S, TH + 16.f * S,
			bPicked ? FLinearColor(Choice.Color.R * 0.5f, Choice.Color.G * 0.5f, Choice.Color.B * 0.5f, 0.95f) : FLinearColor(1.f, 1.f, 1.f, 0.06f));
		DrawCenteredText(Choice.Text, Choice.X, Top + 136.f * S, bPicked ? FLinearColor::White : Choice.Color, Medium, S * 1.3f);
	}

	// Everyone's say so far.
	TArray<FString> Says;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS))
		{
			const TCHAR* Say = Officer->GetShiftVote() == EFTOShiftVote::Overtime ? TEXT("overtime") : (Officer->GetShiftVote() == EFTOShiftVote::ClockOff ? TEXT("clock off") : TEXT("..."));
			Says.Add(FString::Printf(TEXT("%s: %s"), *Officer->GetCallsign(), Say));
		}
	}
	DrawCenteredText(FString::Join(Says, TEXT("    ")), CX, Top + 200.f * S, FLinearColor(0.85f, 0.85f, 0.85f), Medium, S * 1.1f);
	DrawCenteredText(FString::Printf(TEXT("Most votes wins; the host breaks a tie (and decides if nobody votes).  %d s"), FMath::CeilToInt(Left)),
		CX, Top + 250.f * S, FLinearColor(0.65f, 0.65f, 0.65f), Small, S * 1.1f);
}