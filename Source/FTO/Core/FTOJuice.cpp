#include "Core/FTOJuice.h"
#include "Core/FTOGameState.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

bool UFTOShakeModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	Super::ModifyCamera(DeltaTime, InOutPOV);
	// (Real time: a shake in slow motion still shakes.)
	const float Real = FApp::GetDeltaTime();
	Time += Real;
	Trauma = FMath::Max(0.f, Trauma - 1.4f * Real);
	if (Trauma <= 0.f)
	{
		return false;
	}
	const float Shake = Trauma * Trauma;
	auto Noise = [this](float Seed) { return FMath::PerlinNoise1D(Time * 22.f + Seed); };
	InOutPOV.Location += FVector(Noise(0.f), Noise(13.f), Noise(27.f)) * 14.f * Shake;
	InOutPOV.Rotation += FRotator(Noise(41.f) * 4.f, Noise(53.f) * 4.f, Noise(67.f) * 6.f) * Shake;
	return false;
}

namespace
{
	UFTOShakeModifier* ShakerOf(APlayerController* PC)
	{
		if (!PC || !PC->PlayerCameraManager)
		{
			return nullptr;
		}
		UFTOShakeModifier* Shaker = Cast<UFTOShakeModifier>(PC->PlayerCameraManager->FindCameraModifierByClass(UFTOShakeModifier::StaticClass()));
		if (!Shaker)
		{
			Shaker = Cast<UFTOShakeModifier>(PC->PlayerCameraManager->AddNewCameraModifier(UFTOShakeModifier::StaticClass()));
		}
		return Shaker;
	}

	/** Server: when the last slow motion started, per world. */
	TMap<TWeakObjectPtr<const UWorld>, float> LastSlowMoTimes;
}

void FTOJuice::ShakeAt(const UWorld* World, const FVector& At, float Amount, float Radius)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer || Radius <= 0.f)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController() || !PC->PlayerCameraManager)
		{
			continue;
		}
		const float Distance = FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), At);
		const float Falloff = 1.f - FMath::Clamp(Distance / Radius, 0.f, 1.f);
		if (Falloff > 0.f)
		{
			if (UFTOShakeModifier* Shaker = ShakerOf(PC))
			{
				Shaker->AddTrauma(Amount * Falloff);
			}
		}
	}
}

bool FTOJuice::ShakeFor(const USoundBase* Sound, float& OutAmount, float& OutRadius)
{
	if (!Sound)
	{
		return false;
	}
	const FFTOSoundSet& Sounds = AFTOGameState::Sounds();
	auto InFamily = [&Sounds, Sound](const TCHAR* Family)
	{
		const FFTOSoundFamily* Takes = Sounds.Families.Find(Family);
		return Takes && Takes->Sounds.Contains(Sound);
	};
	if (InFamily(TEXT("Explosion")) || Sound == Sounds.Collapse)  { OutAmount = 0.9f; OutRadius = 4500.f; return true; }
	if (InFamily(TEXT("WallBreak")))                               { OutAmount = 0.6f; OutRadius = 2500.f; return true; }
	if (InFamily(TEXT("Crash")) || InFamily(TEXT("CarImpactHeavy")) || Sound == Sounds.Crash) { OutAmount = 0.5f; OutRadius = 1800.f; return true; }
	if (InFamily(TEXT("CarImpactLight")))                          { OutAmount = 0.2f; OutRadius = 1000.f; return true; }
	if (InFamily(TEXT("BodyFall")) || Sound == Sounds.Bonk)        { OutAmount = 0.25f; OutRadius = 900.f; return true; }
	if (InFamily(TEXT("Punch")) || InFamily(TEXT("Kick")))         { OutAmount = 0.18f; OutRadius = 500.f; return true; }
	if (InFamily(TEXT("ShotShotgun")) || InFamily(TEXT("ShotRifle"))) { OutAmount = 0.12f; OutRadius = 500.f; return true; }
	return false;
}

float FTOJuice::GetTrauma(const UWorld* World)
{
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const UFTOShakeModifier* Shaker = PC && PC->PlayerCameraManager ? Cast<UFTOShakeModifier>(PC->PlayerCameraManager->FindCameraModifierByClass(UFTOShakeModifier::StaticClass())) : nullptr;
	return Shaker ? Shaker->GetTrauma() : 0.f;
}

void FTOJuice::SlowMo(UWorld* World, float Scale, float RealSeconds)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	float& Last = LastSlowMoTimes.FindOrAdd(World, -100.f);
	if (Now - Last < 6.f || UGameplayStatics::GetGlobalTimeDilation(World) < 1.f)
	{
		return; // (not one after another: it'd drag)
	}
	Last = Now;
	UGameplayStatics::SetGlobalTimeDilation(World, Scale);
	// The timer runs on game time, slowed down with everything else.
	FTimerHandle Back;
	World->GetTimerManager().SetTimer(Back, FTimerDelegate::CreateWeakLambda(World, [World]()
	{
		UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
	}), FMath::Max(0.01f, RealSeconds * Scale), false);
}

float FTOJuice::LastSlowMo(const UWorld* World)
{
	const float* Last = LastSlowMoTimes.Find(World);
	return Last ? *Last : -100.f;
}
