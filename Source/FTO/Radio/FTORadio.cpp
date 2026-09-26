#include "Radio/FTORadio.h"
#include "City/FTOTrafficCar.h"
#include "Core/FTOCharacter.h"
#include "Crime/FTOPerp.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundEffectSource.h"
#include "SourceEffects/SourceEffectBitCrusher.h"
#include "SourceEffects/SourceEffectFilter.h"
#include "SourceEffects/SourceEffectWaveShaper.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	/** How far from the caller "Suspect fleeing!" and "Officer down!" look for who they mean. */
	constexpr float CalloutReach = 6000.f;

	USoundBase* LoadSound(TStrongObjectPtr<USoundBase>& Cache, const TCHAR* Name)
	{
		if (!Cache)
		{
			Cache.Reset(LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/FTO/Audio/%s.%s"), Name, Name)));
		}
		return Cache.Get();
	}
}

FVector FFTOCalloutPing::GetLocation() const
{
	return IsValid(Follow) ? Follow->GetActorLocation() : FVector(Location);
}

FText FTORadio::CalloutLine(EFTOCallout Callout)
{
	switch (Callout)
	{
	case EFTOCallout::Backup:      return INVTEXT("Need backup!");
	case EFTOCallout::Fleeing:     return INVTEXT("Suspect fleeing!");
	case EFTOCallout::OfficerDown: return INVTEXT("Officer down!");
	case EFTOCallout::Copy:        return INVTEXT("10-4.");
	default:                       return FText::GetEmpty();
	}
}

FString FTORadio::PingLabel(EFTOCallout Callout)
{
	switch (Callout)
	{
	case EFTOCallout::Backup:      return TEXT("BACKUP");
	case EFTOCallout::Fleeing:     return TEXT("FLEEING");
	case EFTOCallout::OfficerDown: return TEXT("OFFICER DOWN");
	default:                       return FString();
	}
}

bool FTORadio::HasPing(EFTOCallout Callout)
{
	return Callout == EFTOCallout::Backup || Callout == EFTOCallout::Fleeing || Callout == EFTOCallout::OfficerDown;
}

FLinearColor FTORadio::CalloutColor(EFTOCallout Callout)
{
	switch (Callout)
	{
	case EFTOCallout::Backup:      return FLinearColor(1.f, 0.75f, 0.15f);
	case EFTOCallout::Fleeing:     return FLinearColor(1.f, 0.45f, 0.15f);
	case EFTOCallout::OfficerDown: return FLinearColor(1.f, 0.2f, 0.15f);
	default:                       return FLinearColor(0.5f, 1.f, 0.55f);
	}
}

USoundEffectSourcePresetChain* FTORadio::GetVoiceFilter()
{
	static TStrongObjectPtr<USoundEffectSourcePresetChain> Chain;
	if (Chain)
	{
		return Chain.Get();
	}
	// A walkie-talkie: only the middle of the voice gets through (band-pass round 1.6 kHz), at a gritty sample rate
	// and bit depth, driven a little hot.
	USourceEffectFilterPreset* Filter = NewObject<USourceEffectFilterPreset>(GetTransientPackage(), TEXT("FTORadioBandPass"));
	FSourceEffectFilterSettings FilterSettings;
	FilterSettings.FilterType = ESourceEffectFilterType::BandPass;
	FilterSettings.CutoffFrequency = 1600.f;
	FilterSettings.FilterQ = 0.9f;
	Filter->SetSettings(FilterSettings);

	USourceEffectBitCrusherPreset* Crusher = NewObject<USourceEffectBitCrusherPreset>(GetTransientPackage(), TEXT("FTORadioCrusher"));
	FSourceEffectBitCrusherBaseSettings CrusherSettings;
	CrusherSettings.SampleRate = 11025.f;
	CrusherSettings.BitDepth = 9.f;
	Crusher->SetSettings(CrusherSettings);

	USourceEffectWaveShaperPreset* Drive = NewObject<USourceEffectWaveShaperPreset>(GetTransientPackage(), TEXT("FTORadioDrive"));
	FSourceEffectWaveShaperSettings DriveSettings;
	DriveSettings.Amount = 3.f;
	DriveSettings.OutputGainDb = -2.f;
	Drive->SetSettings(DriveSettings);

	Chain.Reset(NewObject<USoundEffectSourcePresetChain>(GetTransientPackage(), TEXT("FTORadioVoice")));
	for (USoundEffectSourcePreset* Preset : TArray<USoundEffectSourcePreset*>{ Filter, Crusher, Drive })
	{
		FSourceEffectChainEntry Entry;
		Entry.Preset = Preset;
		Chain->Chain.Add(Entry);
	}
	// The presets are owned by the chain from here (kept alive through its Chain entries).
	return Chain.Get();
}

USoundBase* FTORadio::SquelchOpen()
{
	static TStrongObjectPtr<USoundBase> Sound;
	return LoadSound(Sound, TEXT("SW_SquelchOpen"));
}

USoundBase* FTORadio::SquelchClose()
{
	static TStrongObjectPtr<USoundBase> Sound;
	return LoadSound(Sound, TEXT("SW_SquelchClose"));
}

USoundBase* FTORadio::CalloutChirp()
{
	static TStrongObjectPtr<USoundBase> Sound;
	return LoadSound(Sound, TEXT("SW_Callout"));
}

FFTOCalloutPing FTORadio::ResolvePing(const APlayerState* Caller, EFTOCallout Callout, bool bAboutCaller)
{
	FFTOCalloutPing Ping;
	Ping.Callout = Callout;
	UWorld* World = Caller ? Caller->GetWorld() : nullptr;
	if (const AGameStateBase* GS = World ? World->GetGameState() : nullptr)
	{
		Ping.ServerTime = GS->GetServerWorldTimeSeconds();
	}
	APawn* Pawn = Caller ? Caller->GetPawn() : nullptr;
	if (!Pawn || !World)
	{
		return Ping; // announced, with nowhere to point
	}
	Ping.Location = Pawn->GetActorLocation();
	Ping.Follow = Pawn;
	if (bAboutCaller)
	{
		return Ping;
	}

	// Who they mean: the nearest one within reach.
	AActor* Best = nullptr;
	float BestDistSq = FMath::Square(CalloutReach);
	auto Consider = [&](AActor* Candidate)
	{
		const float DistSq = FVector::DistSquared(Candidate->GetActorLocation(), Pawn->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	};
	if (Callout == EFTOCallout::Fleeing)
	{
		for (TActorIterator<AFTOTrafficCar> It(World); It; ++It)
		{
			if (It->GetCarState() == EFTOCarState::Fleeing)
			{
				Consider(*It);
			}
		}
		for (TActorIterator<AFTOPerp> It(World); It; ++It)
		{
			if (It->IsFleeing())
			{
				Consider(*It);
			}
		}
	}
	else if (Callout == EFTOCallout::OfficerDown)
	{
		for (TActorIterator<AFTOCharacter> It(World); It; ++It)
		{
			if (It->IsDowned() && *It != Pawn)
			{
				Consider(*It);
			}
		}
	}
	if (Best)
	{
		Ping.Follow = Best;
		Ping.Location = Best->GetActorLocation();
	}
	else if (Callout == EFTOCallout::OfficerDown)
	{
		Ping.Follow = nullptr; // nobody down in sight: mark where the caller is (they're still on their feet)
	}
	return Ping;
}
