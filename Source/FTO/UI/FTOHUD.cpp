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
#include "Vehicles/FTOCruiser.h"
#include "City/FTOCityGenerator.h"
#include "Crime/FTOArrestee.h"
#include "Crime/FTOPerp.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

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

	UpdateAudioCues(GS);

	switch (GS->GetShiftPhase())
	{
	case EFTOShiftPhase::Lobby:
		DrawTeammateMarkers(GS);
		DrawLobby(GS);
		DrawInteractPrompt();
		DrawCruiserPanel();
		DrawWeaponPanel();
		break;

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
		DrawCruiserPanel();
		DrawEscortPanel();
		DrawWeaponPanel();
		DrawArrestPanel(GS);
		DrawScoreTicker();
		break;

	default:
		DrawShiftReport(GS);
		break;
	}

	DrawScorePopups();
	DrawRadio(GS);
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

		// Critical incidents (and suspects on the run) pulse.
		const bool bRunning = Incident->IsFootChase() && !Incident->IsSubdued();
		const float Pulse = Info.Tier == EFTOCrimeTier::Critical || bRunning ? 1.f + 0.25f * FMath::Sin(Time * 8.f) : 1.f;
		const float Size = (bOnScreen ? 14.f : 11.f) * S * Pulse;
		DrawDiamond(Screen, Size + 3.f * S, FLinearColor(0.f, 0.f, 0.f, 0.7f));
		DrawDiamond(Screen, Size, FTOCrime::TierColor(Info.Tier));

		if (Me)
		{
			const int32 Meters = FMath::RoundToInt(FVector::Dist2D(Me->GetActorLocation(), Incident->GetActorLocation()) / 100.f);
			const FString Title = bRunning ? Info.Title.ToString() + TEXT(" (on the run)") : Info.Title.ToString();
			const FString Label = bOnScreen ? FString::Printf(TEXT("%s  %dm"), *Title, Meters) : FString::Printf(TEXT("%dm"), Meters);
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

		// A partner who's been shot down needs helping up: flash their marker red.
		const AFTOCharacter* Partner = Cast<AFTOCharacter>(Pawn);
		const bool bDown = Partner && Partner->IsDowned();
		FVector2D Screen;
		ProjectToScreenEdge((bDown ? Partner->GetInteractLocation() + FVector(0.f, 0.f, 80.f) : Pawn->GetActorLocation() + FVector(0.f, 0.f, 160.f)), 30.f * S, Screen);
		const bool bFlash = bDown && FMath::Fmod(GetWorld()->GetTimeSeconds() * 3.f, 1.f) < 0.5f;
		const FLinearColor Color = bFlash ? FLinearColor(1.f, 0.15f, 0.1f) : Officer->GetOfficerColor();
		DrawRect(FLinearColor::Black, Screen.X - 7.f * S, Screen.Y - 7.f * S, 14.f * S, 14.f * S);
		DrawRect(Color, Screen.X - 5.f * S, Screen.Y - 5.f * S, 10.f * S, 10.f * S);
		DrawCenteredText(bDown ? FString::Printf(TEXT("%s  HELP!"), *Officer->GetPlayerName()) : Officer->GetPlayerName(), Screen.X, Screen.Y - 26.f * S, Color, Font, S);
	}
}

void AFTOHUD::ShowHitMarker(bool bBadHit)
{
	HitMarkerUntil = GetWorld()->GetTimeSeconds() + 0.25f;
	bHitMarkerBad = bBadHit;
}

void AFTOHUD::DrawWeaponPanel()
{
	const AFTOCharacter* Me = Cast<AFTOCharacter>(GetOwningPawn());
	if (!Me || Me->GetCurrentVehicle())
	{
		return;
	}
	const float S = UIScale();
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const float Now = GetWorld()->GetTimeSeconds();
	const EFTOWeapon Drawn = Me->GetDrawnWeapon();

	// Crosshair while a weapon is up: a dot and four ticks, and an X when a round lands on someone.
	if (Drawn != EFTOWeapon::None && Me->GetAimPose() != EFTOAimPose::None)
	{
		const FLinearColor Cross(1.f, 1.f, 1.f, 0.9f);
		const float Gap = 7.f * S;
		const float Len = 8.f * S;
		const float T = 2.f * S;
		DrawRect(Cross, CX - T * 0.5f, CY - T * 0.5f, T, T);
		DrawRect(Cross, CX - Gap - Len, CY - T * 0.5f, Len, T);
		DrawRect(Cross, CX + Gap, CY - T * 0.5f, Len, T);
		DrawRect(Cross, CX - T * 0.5f, CY - Gap - Len, T, Len);
		DrawRect(Cross, CX - T * 0.5f, CY + Gap, T, Len);
	}
	if (Now < HitMarkerUntil)
	{
		const FLinearColor Mark = bHitMarkerBad ? FLinearColor(1.f, 0.2f, 0.15f) : FLinearColor::White;
		const float In = 6.f * S;
		const float Out = 14.f * S;
		for (const FVector2D Corner : { FVector2D(1.f, 1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, -1.f) })
		{
			DrawLine(CX + Corner.X * In, CY + Corner.Y * In, CX + Corner.X * Out, CY + Corner.Y * Out, Mark, 2.5f * S);
		}
	}

	// The three slots, bottom right: what's in each and its ammo, the one in hand lit up.
	UFont* Font = GEngine->GetSmallFont();
	const float W = 150.f * S;
	const float H = 46.f * S;
	const float Gap = 8.f * S;
	const float X0 = Canvas->ClipX - (W + Gap) * FTOWeapons::MaxSlots - 12.f * S;
	const float Y0 = Canvas->ClipY - H - 18.f * S;
	for (int32 Slot = 0; Slot < FTOWeapons::MaxSlots; ++Slot)
	{
		const EFTOWeapon Weapon = Me->GetWeaponInSlot(Slot);
		const bool bInHand = Slot == Me->GetDrawnSlot() && Weapon != EFTOWeapon::None;
		const float X = X0 + Slot * (W + Gap);
		DrawPanel(X, Y0, W, H, bInHand ? FLinearColor(0.1f, 0.25f, 0.55f, 0.85f) : FLinearColor(0.f, 0.f, 0.f, 0.5f));
		const FLinearColor Ink = Weapon == EFTOWeapon::None ? FLinearColor(0.5f, 0.5f, 0.5f) : FLinearColor::White;
		DrawText(FString::Printf(TEXT("%d  %s"), Slot + 1, *FTOWeapons::DisplayName(Weapon).ToString().ToUpper()), Ink, X + 8.f * S, Y0 + 5.f * S, Font, S * 1.05f);
		if (Weapon != EFTOWeapon::None)
		{
			const FString Ammo = bInHand && Me->IsReloading() ? TEXT("RELOADING...") : FString::Printf(TEXT("%d / %d"), Me->GetClip(Slot), Me->GetSpare(Slot));
			const FLinearColor AmmoInk = Me->GetClip(Slot) == 0 && !Me->IsReloading() ? FLinearColor(1.f, 0.4f, 0.3f) : FLinearColor(0.75f, 0.85f, 1.f);
			DrawText(Ammo, AmmoInk, X + 8.f * S, Y0 + 25.f * S, Font, S);
		}
	}
	DrawText(TEXT("1-3 / wheel: weapons   RMB: raise   LMB: fire   R: reload"), FLinearColor(0.7f, 0.7f, 0.7f, 0.8f), X0, Y0 - 16.f * S, Font, S * 0.85f);

	// Shot down: a banner until a partner comes.
	if (Me->IsDowned())
	{
		const FString Line = TEXT("YOU'RE DOWN!  Hang on: a partner can help you up.");
		float TW = 0.f, TH = 0.f;
		GetTextSize(Line, TW, TH, GEngine->GetMediumFont(), S * 1.2f);
		DrawPanel(CX - TW * 0.5f - 16.f * S, CY + 80.f * S, TW + 32.f * S, TH + 16.f * S, FLinearColor(0.4f, 0.f, 0.f, 0.75f));
		DrawCenteredText(Line, CX, CY + 88.f * S, FLinearColor(1.f, 0.85f, 0.8f), GEngine->GetMediumFont(), S * 1.2f);
	}
}

void AFTOHUD::DrawOnSceneProgress(const AFTOGameState* GS)
{
	const APawn* Me = GetOwningPawn();
	const AFTOCharacter* MeOnFoot = Cast<AFTOCharacter>(Me);
	if (!Me || (MeOnFoot && MeOnFoot->IsInSyncedAction()))
	{
		return; // busy wrestling or cuffing: the arrest panel has the floor
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
		if (DistSq <= FMath::Square(Incident->GetSceneRadius()) && DistSq < NearestDistSq)
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

	// Calls are handled by being there; crimes end with the cuffs (talk them down, or chance an arrest right away).
	const bool bUnderstaffed = Nearest->GetOfficersOnScene() < Info.OfficersRequired;
	FString Status;
	FLinearColor StatusColor = FLinearColor::White;
	const AFTOPerp* Perp = Nearest->GetPerp();
	if (Perp && Perp->GetArrestState() == EFTOPerpArrest::Struggling)
	{
		Status = TEXT("They're fighting your partner: pile in and help! [E]");
		StatusColor = FLinearColor(1.f, 0.6f, 0.2f);
	}
	else if (Perp && Perp->GetArrestState() == EFTOPerpArrest::Cuffing)
	{
		Status = TEXT("Cuffing...");
		StatusColor = FLinearColor(0.6f, 0.85f, 1.f);
	}
	else if (Nearest->IsSubdued())
	{
		Status = TEXT("They've given up: cuff them! [E]");
		StatusColor = FLinearColor(0.6f, 0.85f, 1.f);
	}
	else if (Nearest->IsFootChase())
	{
		Status = TEXT("They're getting away! Sprint (Shift) and tackle (F)");
		StatusColor = FLinearColor(1.f, 0.6f, 0.2f);
	}
	else if (bUnderstaffed)
	{
		Status = FString::Printf(TEXT("Need backup! %d/%d officers on scene%s"), Nearest->GetOfficersOnScene(), Info.OfficersRequired,
			Info.bArrest ? TEXT("  |  [E] arrest them now") : TEXT(""));
		StatusColor = FLinearColor(1.f, 0.6f, 0.2f);
	}
	else
	{
		Status = Info.bArrest
			? FString::Printf(TEXT("Talking them down... %d/%d officers  |  [E] arrest them now"), Nearest->GetOfficersOnScene(), Info.OfficersRequired)
			: FString::Printf(TEXT("Handling it... %d/%d officers"), Nearest->GetOfficersOnScene(), Info.OfficersRequired);
	}
	DrawCenteredText(Status, X + W * 0.5f, Y + H + 8.f * S, StatusColor, GEngine->GetSmallFont(), S * 1.2f);
}

void AFTOHUD::DrawArrestPanel(const AFTOGameState* GS)
{
	const APawn* MyPawn = GetOwningPawn();
	if (!MyPawn)
	{
		return;
	}
	const float S = UIScale();
	const float CX = Canvas->ClipX * 0.5f;
	const float Time = GetWorld()->GetTimeSeconds();
	UFont* Medium = GEngine->GetMediumFont();

	// In the middle of an arrest: wrestling (the meter, and keep mashing) or putting the cuffs on.
	const AFTOCharacter* Me = Cast<AFTOCharacter>(MyPawn);
	if (const AFTOPerp* Perp = Me ? Cast<AFTOPerp>(Me->GetSyncedPartner()) : nullptr)
	{
		const bool bStruggle = Me->GetSyncedAction() == EFTOAnimAction::Struggle;
		const float W = 460.f * S;
		const float H = 24.f * S;
		const float X = CX - W * 0.5f;
		const float Y = Canvas->ClipY - 150.f * S;
		DrawPanel(X - 12.f * S, Y - 50.f * S, W + 24.f * S, H + 88.f * S, bStruggle ? FLinearColor(0.25f, 0.05f, 0.f, 0.7f) : FLinearColor(0.02f, 0.08f, 0.2f, 0.7f));
		DrawCenteredText(bStruggle ? TEXT("THEY'RE FIGHTING BACK!") : TEXT("CUFFING..."), CX, Y - 44.f * S,
			bStruggle ? FLinearColor(1.f, 0.6f, 0.25f) : FLinearColor(0.6f, 0.85f, 1.f), Medium, S * 1.3f);
		DrawRect(FLinearColor(0.1f, 0.1f, 0.1f, 0.9f), X, Y, W, H);
		if (bStruggle)
		{
			const float Meter = Perp->GetStruggleMeter();
			DrawRect(FMath::Lerp(FLinearColor(1.f, 0.25f, 0.15f), FLinearColor(0.3f, 1.f, 0.4f), Meter), X, Y, W * Meter, H);
			const bool bFlash = FMath::Fmod(Time * 6.f, 1.f) < 0.5f;
			DrawCenteredText(TEXT("MASH [E] TO WRESTLE THEM DOWN!"), CX, Y + H + 8.f * S, bFlash ? FLinearColor::White : FLinearColor(1.f, 0.9f, 0.4f), Medium, S * 1.1f);
		}
		else
		{
			DrawRect(FLinearColor(0.3f, 0.6f, 1.f), X, Y, W * Perp->GetCuffProgress(), H);
			DrawCenteredText(TEXT("You have the right to remain silly."), CX, Y + H + 8.f * S, FLinearColor(0.8f, 0.8f, 0.8f), GEngine->GetSmallFont(), S * 1.2f);
		}
		return;
	}

	// A suspect on the run nearby (on foot, or at the wheel: a bumper stops them too).
	for (const AFTOIncident* Incident : GS->GetIncidents())
	{
		const AFTOPerp* Runner = Incident && Incident->IsActive() ? Incident->GetPerp() : nullptr;
		if (!Runner || !Runner->IsFleeing())
		{
			continue;
		}
		const float Distance = FVector::Dist2D(MyPawn->GetActorLocation(), Runner->GetActorLocation());
		if (Distance > 6000.f)
		{
			continue;
		}
		const FString Line = FString::Printf(TEXT("SUSPECT ON THE RUN!  %dm  |  Sprint (Shift) and tackle (F)"), FMath::RoundToInt(Distance / 100.f));
		const float Y = 150.f * S;
		float TW = 0.f, TH = 0.f;
		GetTextSize(Line, TW, TH, Medium, S * 1.1f);
		const bool bFlash = FMath::Fmod(Time * 3.f, 1.f) < 0.5f;
		DrawPanel(CX - TW * 0.5f - 14.f * S, Y - 6.f * S, TW + 28.f * S, TH + 12.f * S, bFlash ? FLinearColor(0.45f, 0.08f, 0.f, 0.8f) : FLinearColor(0.25f, 0.04f, 0.f, 0.8f));
		DrawCenteredText(Line, CX, Y, FLinearColor(1.f, 0.8f, 0.5f), Medium, S * 1.1f);
		break;
	}
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
	DrawCenteredText(TEXT("WASD move  |  Shift sprint  |  Space jump  |  E interact, arrest, drive  |  F tackle  |  stand at a scene to handle it"), CX, CY + 110.f * S, FLinearColor(0.7f, 0.7f, 0.7f), GEngine->GetSmallFont(), S * 1.1f);
}

void AFTOHUD::DrawShiftReport(const AFTOGameState* GS)
{
	DrawScoreboard(GS); // FTOHUDScore.cpp
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

void AFTOHUD::DrawCruiserPanel()
{
	// At the wheel, or riding shotgun.
	const AFTOCruiser* Cruiser = Cast<AFTOCruiser>(GetOwningPawn());
	const bool bDriving = Cruiser != nullptr;
	if (const AFTOCharacter* Officer = bDriving ? nullptr : Cast<AFTOCharacter>(GetOwningPawn()))
	{
		Cruiser = Cast<AFTOCruiser>(Officer->GetCurrentVehicle());
	}
	if (!Cruiser)
	{
		return;
	}

	const float S = UIScale();
	const float CX = Canvas->ClipX * 0.5f;
	const float Y = Canvas->ClipY - 120.f * S;
	const int32 Kmh = FMath::RoundToInt(FMath::Abs(Cruiser->GetSpeed()) * 0.036f);

	DrawPanel(CX - 300.f * S, Y - 10.f * S, 600.f * S, 100.f * S);
	DrawCenteredText(FString::Printf(TEXT("%d km/h"), Kmh), CX, Y, FLinearColor::White, GEngine->GetLargeFont(), S * 1.4f);

	if (Cruiser->IsSirenOn())
	{
		const bool bRed = FMath::Fmod(GetWorld()->GetTimeSeconds() * 3.f, 2.f) < 1.f;
		DrawCenteredText(TEXT("SIREN"), CX + 160.f * S, Y + 8.f * S, bRed ? FLinearColor(1.f, 0.2f, 0.2f) : FLinearColor(0.3f, 0.5f, 1.f), GEngine->GetMediumFont(), S);
	}

	DrawCenteredText(bDriving
		? TEXT("W/S drive  |  A/D steer  |  Space handbrake  |  Q siren  |  H horn  |  C camera  |  E get out")
		: TEXT("Riding shotgun  |  Mouse to look  |  Q lights and siren  |  C camera  |  E get out"),
		CX, Y + 55.f * S, FLinearColor(0.75f, 0.75f, 0.75f), GEngine->GetSmallFont(), S * 1.1f);
}

void AFTOHUD::DrawLobby(const AFTOGameState* GS)
{
	const float S = UIScale();
	const float CX = Canvas->ClipX * 0.5f;
	const float Y = 24.f * S;
	const bool bIsHost = GetNetMode() != NM_Client;

	const int32 Officers = GS->PlayerArray.Num();
	const float PanelH = (110.f + 26.f * Officers) * S;
	DrawPanel(CX - 330.f * S, Y - 8.f * S, 660.f * S, PanelH);
	DrawCenteredText(TEXT("PRECINCT LOBBY"), CX, Y, FLinearColor(0.6f, 0.8f, 1.f), GEngine->GetLargeFont(), S * 1.2f);
	DrawCenteredText(bIsHost
		? TEXT("Press Esc and choose Start shift when everyone's here")
		: TEXT("Waiting for the host to start the shift"),
		CX, Y + 40.f * S, FLinearColor(1.f, 0.85f, 0.35f), GEngine->GetMediumFont(), S);
	DrawCenteredText(TEXT("Stretch your legs, or grab a cruiser (E)"), CX, Y + 66.f * S, FLinearColor(0.75f, 0.75f, 0.75f), GEngine->GetSmallFont(), S * 1.1f);

	float RowY = Y + 92.f * S;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		const AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS);
		const FLinearColor Color = Officer ? Officer->GetOfficerColor() : FLinearColor::White;
		DrawRect(Color, CX - 120.f * S, RowY + 4.f * S, 14.f * S, 14.f * S);
		DrawText(PS ? PS->GetPlayerName() : FString(), FLinearColor::White, CX - 96.f * S, RowY, GEngine->GetMediumFont(), S);
		RowY += 26.f * S;
	}
}

void AFTOHUD::UpdateAudioCues(const AFTOGameState* GS)
{
	const FFTOSoundSet& Sounds = AFTOGameState::Sounds();
	auto Play = [this](USoundBase* Sound, float Volume)
	{
		if (Sound)
		{
			UGameplayStatics::PlaySound2D(this, Sound, Volume);
		}
	};

	// Shift stingers.
	const EFTOShiftPhase Phase = GS->GetShiftPhase();
	if (bPhaseKnown && Phase != LastPhase)
	{
		switch (Phase)
		{
		case EFTOShiftPhase::Briefing: Play(Sounds.Bugle, 0.8f); break;
		case EFTOShiftPhase::Survived: Play(Sounds.Fanfare, 0.8f); break;
		case EFTOShiftPhase::Overrun:  Play(Sounds.Womp, 0.8f); break;
		default: break;
		}
	}
	LastPhase = Phase;
	bPhaseKnown = true;

	// Dispatch chatter for new calls; chimes and stings as incidents end.
	const float Now = GetWorld()->GetRealTimeSeconds();
	for (const AFTOIncident* Incident : GS->GetIncidents())
	{
		if (!Incident)
		{
			continue;
		}
		const EFTOIncidentState State = Incident->GetState();
		const EFTOIncidentState* Previous = SeenIncidentStates.Find(Incident);
		const bool bWasKnown = Previous && (*Previous == EFTOIncidentState::Reported || *Previous == EFTOIncidentState::Responding);
		if (Incident->IsKnownToDispatch() && !bWasKnown && Now - LastRadioTime > 0.6f)
		{
			Play(Sounds.Radio, 0.55f);
			LastRadioTime = Now;
		}
		if (Previous && *Previous != State)
		{
			if (State == EFTOIncidentState::Resolved)
			{
				Play(Sounds.Chime, 0.7f);
			}
			else if (State == EFTOIncidentState::Failed)
			{
				Play(Sounds.Fail, 0.6f);
			}
		}
		SeenIncidentStates.Add(Incident, State);
	}
	for (auto It = SeenIncidentStates.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			It.RemoveCurrent();
		}
	}

	// Chaos alarm, re-armed once things calm down a bit.
	if (Phase == EFTOShiftPhase::OnDuty)
	{
		if (bAlarmArmed && GS->GetChaos() >= 75.f)
		{
			Play(Sounds.Alarm, 0.7f);
			bAlarmArmed = false;
		}
		else if (!bAlarmArmed && GS->GetChaos() < 65.f)
		{
			bAlarmArmed = true;
		}
	}
}

void AFTOHUD::DrawEscortPanel()
{
	// Who am I? On foot the pawn is the officer; driving, it's the cruiser's driver.
	const AFTOCharacter* Me = Cast<AFTOCharacter>(GetOwningPawn());
	if (!Me)
	{
		if (const AFTOCruiser* Cruiser = Cast<AFTOCruiser>(GetOwningPawn()))
		{
			Me = Cruiser->GetDriver();
		}
	}
	if (!Me)
	{
		return;
	}

	int32 Count = 0;
	FString Crimes;
	for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
	{
		const EFTOArresteeState State = It->GetArrestState();
		if (It->GetEscort() == Me && (State == EFTOArresteeState::Escorted || State == EFTOArresteeState::InCruiser))
		{
			Crimes += (Count++ ? TEXT(", ") : TEXT("")) + It->GetCrime().ToString();
		}
	}
	if (Count == 0)
	{
		return;
	}

	// Point the way home.
	const AFTOCityGenerator* City = nullptr;
	for (TActorIterator<AFTOCityGenerator> It(GetWorld()); It; ++It)
	{
		City = *It;
		break;
	}
	const float S = UIScale();
	if (City)
	{
		// The precinct's front door from outside; the holding cells once in.
		const FFTOBuilding* Precinct = City->FindBuilding(EFTOBuildingType::Precinct);
		const bool bInside = Precinct && Precinct->Contains(Me->GetActorLocation() - FVector(0.f, 0.f, 96.f), 50.f);
		const FVector Goal = bInside ? City->GetHoldingCellsLocation() + FVector(0.f, 0.f, 160.f)
			: Precinct ? Precinct->DoorOutside + FVector(0.f, 0.f, 350.f) : City->GetPrecinctLocation() + FVector(0.f, 0.f, 600.f);

		FVector2D Screen;
		const bool bOnScreen = ProjectToScreenEdge(Goal, 40.f * S, Screen);
		const float Size = 16.f * S * (1.f + 0.15f * FMath::Sin(GetWorld()->GetTimeSeconds() * 6.f));
		DrawDiamond(Screen, Size + 3.f * S, FLinearColor::Black);
		DrawDiamond(Screen, Size, FLinearColor(0.3f, 0.6f, 1.f));
		const int32 Meters = FMath::RoundToInt(FVector::Dist2D(GetOwningPawn()->GetActorLocation(), Goal) / 100.f);
		DrawCenteredText(bOnScreen ? FString::Printf(TEXT("%s  %dm"), bInside ? TEXT("HOLDING CELLS") : TEXT("PRECINCT"), Meters) : FString::Printf(TEXT("%dm"), Meters),
			Screen.X, Screen.Y + Size + 4.f * S, FLinearColor(0.6f, 0.8f, 1.f), GEngine->GetSmallFont(), S * 1.1f);
	}

	const float CX = Canvas->ClipX * 0.5f;
	const float Y = 110.f * S;
	const FString Line = FString::Printf(TEXT("Escorting %d suspect%s (%s): walk them into the holding cells"), Count, Count > 1 ? TEXT("s") : TEXT(""), *Crimes);
	float W = 0.f, H = 0.f;
	GetTextSize(Line, W, H, GEngine->GetMediumFont(), S);
	DrawPanel(CX - W * 0.5f - 12.f * S, Y - 6.f * S, W + 24.f * S, H + 12.f * S, FLinearColor(0.02f, 0.08f, 0.2f, 0.75f));
	DrawCenteredText(Line, CX, Y, FLinearColor(0.7f, 0.85f, 1.f), GEngine->GetMediumFont(), S);
}
