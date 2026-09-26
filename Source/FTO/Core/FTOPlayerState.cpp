#include "Core/FTOPlayerState.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Net/VoiceConfig.h"
#include "UI/FTOHUD.h"

void AFTOPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOPlayerState, BadgeIndex);
	DOREPLIFETIME(AFTOPlayerState, bOnRadio);
	DOREPLIFETIME(AFTOPlayerState, Callout);
	DOREPLIFETIME(AFTOPlayerState, Stats);
	DOREPLIFETIME(AFTOPlayerState, ShiftVote);
	DOREPLIFETIME(AFTOPlayerState, Combo);
	DOREPLIFETIME(AFTOPlayerState, ComboTime);
}

void AFTOPlayerState::SetBadgeIndex(int32 NewIndex)
{
	BadgeIndex = NewIndex;
	OnRep_BadgeIndex();
}

FLinearColor AFTOPlayerState::GetOfficerColor() const
{
	return ColorForBadge(BadgeIndex);
}

FLinearColor AFTOPlayerState::ColorForBadge(int32 Index)
{
	static const FLinearColor Colors[] =
	{
		FLinearColor(0.10f, 0.35f, 1.00f), // blue
		FLinearColor(1.00f, 0.25f, 0.25f), // red
		FLinearColor(0.20f, 0.85f, 0.35f), // green
		FLinearColor(1.00f, 0.80f, 0.10f), // yellow
	};
	return Colors[FMath::Abs(Index) % UE_ARRAY_COUNT(Colors)];
}

FString AFTOPlayerState::CallsignForBadge(int32 Index)
{
	static const TCHAR* Names[] = { TEXT("Blue"), TEXT("Red"), TEXT("Green"), TEXT("Yellow") };
	return Names[FMath::Abs(Index) % UE_ARRAY_COUNT(Names)];
}

void AFTOPlayerState::OnRep_BadgeIndex()
{
	if (AFTOCharacter* Officer = GetPawn<AFTOCharacter>())
	{
		Officer->RefreshOfficerColor();
	}
}

// ------------------------------------------------------------------------------------------
// Radio
// ------------------------------------------------------------------------------------------

bool AFTOPlayerState::IsLocalOfficer() const
{
	const APlayerController* PC = GetPlayerController();
	return PC && PC->IsLocalController();
}

void AFTOPlayerState::BeginPlay()
{
	Super::BeginPlay();
	SetUpVoice();
}

void AFTOPlayerState::OnSetUniqueId()
{
	Super::OnSetUniqueId();
	SetUpVoice();
}

void AFTOPlayerState::SetUpVoice()
{
	// Every teammate but ourselves, on a machine that plays sound, once we know who they are.
	if (VoiceTalker || GetNetMode() == NM_DedicatedServer || !GetUniqueId().IsValid() || IsLocalOfficer())
	{
		return;
	}
	VoiceTalker = UVOIPTalker::CreateTalkerForPlayer(this);
	if (VoiceTalker)
	{
		// Not attached to anyone, so it's heard the same wherever they are: it's a radio.
		VoiceTalker->Settings.SourceEffectChain = FTORadio::GetVoiceFilter();
	}
}

void AFTOPlayerState::SetOnRadio(bool bOn)
{
	check(HasAuthority());
	if (bOnRadio != bOn)
	{
		bOnRadio = bOn;
		ForceNetUpdate();
		OnRep_OnRadio(); // the listen server's own screen
	}
}

void AFTOPlayerState::OnRep_OnRadio()
{
	// Our talker can be knocked off the engine's list (a rejoin under the same ID before the old one is collected):
	// put it back before they speak.
	if (VoiceTalker && GetUniqueId().IsValid() && UVOIPStatics::GetVOIPTalkerForPlayer(GetUniqueId()) != VoiceTalker)
	{
		VoiceTalker->RegisterWithPlayerState(this);
	}
	PlaySquelch();
}

void AFTOPlayerState::PlaySquelch() const
{
	if (GetNetMode() == NM_DedicatedServer || IsLocalOfficer())
	{
		return; // our own push-to-talk plays its squelch the moment the key goes down
	}
	UGameplayStatics::PlaySound2D(this, bOnRadio ? FTORadio::SquelchOpen() : FTORadio::SquelchClose(), 0.7f);
}

bool AFTOPlayerState::MakeCallout(EFTOCallout NewCallout, bool bAutomatic)
{
	check(HasAuthority());
	// The radio calling it in for us (we've gone down) isn't held up by the cooldown, and is about us.
	const float Now = GetWorld()->GetTimeSeconds();
	if (NewCallout == EFTOCallout::None || NewCallout > EFTOCallout::Copy || (!bAutomatic && Now - LastCalloutTime < FTORadio::CalloutCooldown))
	{
		return false;
	}
	LastCalloutTime = Now;
	const uint8 Serial = Callout.Serial + 1;
	Callout = FTORadio::ResolvePing(this, NewCallout, bAutomatic);
	Callout.Serial = Serial;
	ForceNetUpdate();
	OnRep_Callout(); // the listen server's own screen
	return true;
}

float AFTOPlayerState::GetCalloutAge() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (Callout.Callout == EFTOCallout::None || !GS)
	{
		return TNumericLimits<float>::Max();
	}
	return GS->GetServerWorldTimeSeconds() - Callout.ServerTime;
}

void AFTOPlayerState::OnRep_Callout()
{
	AnnounceCallout();
}

void AFTOPlayerState::AnnounceCallout() const
{
	if (GetNetMode() == NM_DedicatedServer || Callout.Callout == EFTOCallout::None)
	{
		return;
	}
	// A late joiner catching up on an old call doesn't need to hear it.
	if (GetCalloutAge() > 3.f)
	{
		return;
	}
	APlayerController* Local = GetWorld()->GetFirstPlayerController();
	if (AFTOHUD* HUD = Local ? Local->GetHUD<AFTOHUD>() : nullptr)
	{
		const FString Line = FString::Printf(TEXT("[RADIO] %s: %s"), IsLocalOfficer() ? TEXT("You") : *GetCallsign(), *FTORadio::CalloutLine(Callout.Callout).ToString());
		HUD->AddToast(FText::FromString(Line), FTORadio::CalloutColor(Callout.Callout));
	}
	UGameplayStatics::PlaySound2D(this, FTORadio::CalloutChirp(), 0.8f);
}

// ------------------------------------------------------------------------------------------
// Scoring
// ------------------------------------------------------------------------------------------

int32 AFTOPlayerState::GetCombo() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const float Now = GS ? GS->GetServerWorldTimeSeconds() : 0.f;
	return Now - ComboTime <= FTOScoring::ComboWindow ? Combo : 1;
}

int32 AFTOPlayerState::AddScore(EFTOScore Event, int32 BasePoints, const FVector& Where)
{
	check(HasAuthority());
	// Only the shift itself counts (not the lobby, and not after the whistle; an arrest finished during the vote does).
	const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
	if (!GS || (GS->GetShiftPhase() != EFTOShiftPhase::OnDuty && GS->GetShiftPhase() != EFTOShiftPhase::OvertimeVote) || BasePoints == 0)
	{
		return 0;
	}

	int32 Points = BasePoints;
	if (FTOScoring::IsPenalty(Event))
	{
		Combo = 1; // that's the streak over
		ComboTime = -100.f;
	}
	else
	{
		// Quick work builds the combo; the bonuses that come with an arrest ride on the same step.
		const float Now = GS->GetServerWorldTimeSeconds();
		const bool bFollowUp = Event == EFTOScore::CaughtInAct || Event == EFTOScore::Assist;
		if (!bFollowUp)
		{
			Combo = Now - ComboTime <= FTOScoring::ComboWindow ? FMath::Min<int32>(Combo + 1, FTOScoring::MaxCombo) : 1;
			ComboTime = Now;
		}
		Points = FMath::RoundToInt(BasePoints * FTOScoring::ComboMultiplier(GetCombo()));
		Stats.BestCombo = FMath::Max<int32>(Stats.BestCombo, GetCombo());
	}

	Stats.Score += Points;
	switch (Event)
	{
	case EFTOScore::Arrest:       ++Stats.Arrests; break;
	case EFTOScore::Bust:         ++Stats.Busts; break;
	case EFTOScore::CallHandled:  ++Stats.CallsHandled; break;
	case EFTOScore::CaughtInAct:  ++Stats.CaughtInAct; break;
	case EFTOScore::Booked:       ++Stats.Booked; break;
	case EFTOScore::Ticket:       ++Stats.Tickets; break;
	case EFTOScore::Revive:       ++Stats.Revives; break;
	case EFTOScore::Collateral:   ++Stats.Collateral; break;
	case EFTOScore::FriendlyFire: ++Stats.FriendlyFire; break;
	default: break;
	}
	ForceNetUpdate();
	MulticastScorePopup(Points, Event, Where, uint8(FTOScoring::IsPenalty(Event) ? 1 : GetCombo()));
	return Points;
}

void AFTOPlayerState::MulticastScorePopup_Implementation(int32 Points, EFTOScore Event, FVector_NetQuantize Where, uint8 InCombo)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	APlayerController* Local = GetWorld()->GetFirstPlayerController();
	if (AFTOHUD* HUD = Local ? Local->GetHUD<AFTOHUD>() : nullptr)
	{
		HUD->AddScorePopup(this, Points, Event, Where, InCombo);
	}
}

void AFTOPlayerState::SetShiftVote(EFTOShiftVote Vote)
{
	check(HasAuthority());
	ShiftVote = Vote;
	ForceNetUpdate();
}