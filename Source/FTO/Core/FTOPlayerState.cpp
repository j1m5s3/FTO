#include "Core/FTOPlayerState.h"
#include "Core/FTOCharacter.h"
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
