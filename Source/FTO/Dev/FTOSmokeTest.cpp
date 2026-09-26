#include "Dev/FTOSmokeTest.h"
#include "City/FTOCityGenerator.h"
#include "Core/FTOGameMode.h"
#include "Core/FTOGameState.h"
#include "Crime/FTOCrimeDirector.h"
#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "FTO.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
	// Seconds to wait after each step before running the next one.
	const float StepDelays[] = { 1.f, 1.5f, 0.5f, 0.8f, 0.5f, 0.5f, 4.f, 1.f, 3.f, 1.f, 3.f, 1.f, 3.f, 2.f, 1.5f, 1.f, 0.f };
}

AFTOSmokeTest::AFTOSmokeTest()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

bool AFTOSmokeTest::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("FTOSmokeTest"));
}

bool AFTOSmokeTest::AreShadersReady() const
{
#if WITH_EDITOR
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
	{
		return false;
	}
#endif
	return true;
}

void AFTOSmokeTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Now = GetWorld()->GetRealTimeSeconds();

	// Wait for shaders to finish (first launches compile a lot), then settle for a moment.
	if (ReadyTime < 0.f)
	{
		if (!AreShadersReady())
		{
			StableSince = -1.f;
			return;
		}
		if (StableSince < 0.f)
		{
			StableSince = Now;
			return;
		}
		if (Now - StableSince < 3.f)
		{
			return;
		}
		ReadyTime = Now;
		NextStepTime = Now + 2.f;
		UE_LOG(LogFTO, Display, TEXT("SMOKE: world ready, starting tour."));
	}

	if (bWalkOfficer)
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (APawn* Officer = PC->GetPawn())
			{
				Officer->AddMovementInput(WalkDirection, 1.f);
			}
		}
	}

	// One step per frame, and never while shaders are still compiling (the shot would be grey).
	if (NextStep < int32(UE_ARRAY_COUNT(StepDelays)) && Now >= NextStepTime && AreShadersReady())
	{
		RunStep(NextStep);
		NextStepTime = Now + StepDelays[NextStep];
		++NextStep;
	}
}

void AFTOSmokeTest::Shot(const TCHAR* Name, bool bShowUI)
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("SmokeTest"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	// -FTOSmokeTag=client keeps screenshots from several instances apart.
	FString Tag;
	FParse::Value(FCommandLine::Get(), TEXT("FTOSmokeTag="), Tag);
	const FString Path = FPaths::Combine(Dir, (Tag.IsEmpty() ? FString() : Tag + TEXT("_")) + FString(Name) + TEXT(".png"));
	FScreenshotRequest::RequestScreenshot(Path, bShowUI, false);
	UE_LOG(LogFTO, Display, TEXT("SMOKE: screenshot %s"), *Path);
}

void AFTOSmokeTest::ViewFrom(const FVector& Location, const FVector& LookAt)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	if (!Camera)
	{
		Camera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
	}
	Camera->SetActorLocationAndRotation(Location, (LookAt - Location).Rotation());
	PC->SetViewTargetWithBlend(Camera, 0.f);
}

void AFTOSmokeTest::RunStep(int32 Step)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World->GetFirstPlayerController();
	APawn* Officer = PC ? PC->GetPawn() : nullptr;
	AFTOGameMode* GM = World->GetAuthGameMode<AFTOGameMode>();
	AFTOCityGenerator* City = nullptr;
	for (TActorIterator<AFTOCityGenerator> It(World); It; ++It)
	{
		City = *It;
		break;
	}

	UE_LOG(LogFTO, Display, TEXT("SMOKE: step %d"), Step);

	switch (Step)
	{
	case 0:
		Shot(TEXT("01_roll_call"));
		break;

	case 1:
		// Close-up of our officer from the front-left.
		if (Officer)
		{
			if (PC && PC->GetHUD())
			{
				PC->GetHUD()->bShowHUD = false;
			}
			const FVector Fwd = Officer->GetActorForwardVector();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);
			ViewFrom(Officer->GetActorLocation() + Fwd * 330.f - Right * 160.f + FVector(0.f, 0.f, 40.f), Officer->GetActorLocation() + FVector(0.f, 0.f, 10.f));
		}
		break;

	case 2:
		Shot(TEXT("01b_officer"), false);
		break;

	case 3:
		// Walk forward past a side-on camera to check locomotion.
		if (Officer)
		{
			WalkDirection = Officer->GetActorForwardVector();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, WalkDirection);
			const FVector Mid = Officer->GetActorLocation() + WalkDirection * 380.f;
			ViewFrom(Mid + Right * 480.f + FVector(0.f, 0.f, 60.f), Mid + FVector(0.f, 0.f, 10.f));
			bWalkOfficer = true;
		}
		break;

	case 4:
		Shot(TEXT("01c_officer_walk"), false);
		break;

	case 5:
		bWalkOfficer = false;
		if (PC && Officer)
		{
			PC->SetViewTargetWithBlend(Officer, 0.f);
			if (PC->GetHUD())
			{
				PC->GetHUD()->bShowHUD = true;
			}
		}
		break;

	case 6:
		// Skip the briefing and stage a few incidents in view (server/standalone only).
		if (GM && Officer)
		{
			GM->FTOSkipBriefing();
			const FVector Fwd = Officer->GetActorForwardVector();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);
			IncidentSpot = Officer->GetActorLocation() + Fwd * 1400.f;
			GM->GetCrimeDirector()->SpawnIncidentAt(TEXT("BarFight"), IncidentSpot, true);
			GM->GetCrimeDirector()->SpawnIncidentAt(TEXT("CatInTree"), Officer->GetActorLocation() + Fwd * 900.f + Right * 900.f, true);
			GM->GetCrimeDirector()->SpawnIncident(TEXT("BankHeist"), true);
			GM->GetCrimeDirector()->SpawnIncident(TEXT("DomesticDispute"), true);
			GM->GetCrimeDirector()->SpawnIncident(TEXT("Shoplifting"), true);
			GM->FTOAddChaos(35.f);
		}
		break;

	case 7:
		Shot(TEXT("02_on_duty"));
		break;

	case 8:
		if (City)
		{
			const FVector Extent = City->GetCityExtent();
			const FVector Center = City->GetActorLocation();
			ViewFrom(Center + FVector(-Extent.X * 1.1f, -Extent.Y * 0.6f, 14000.f), Center + FVector(0.f, 0.f, 0.f));
		}
		break;

	case 9:
		Shot(TEXT("03_city_aerial"));
		break;

	case 10:
		if (City)
		{
			// Street level, looking down an avenue near the middle of town.
			const int32 MidI = City->NumIntersectionsX() / 2;
			const int32 MidJ = City->NumIntersectionsY() / 2;
			const FVector From = City->GetIntersection(MidI, 0) + FVector(0.f, -400.f, 350.f);
			const FVector To = City->GetIntersection(MidI, MidJ) + FVector(0.f, 0.f, 150.f);
			ViewFrom(From, To);
		}
		break;

	case 11:
		Shot(TEXT("04_street_level"));
		break;

	case 12:
		if (!IncidentSpot.IsZero())
		{
			ViewFrom(IncidentSpot + FVector(-900.f, -600.f, 500.f), IncidentSpot + FVector(0.f, 0.f, 150.f));
		}
		break;

	case 13:
		Shot(TEXT("05_incident"));
		break;

	case 14:
		// Win the shift: report card up, officers cheering.
		if (GM)
		{
			GM->FTOEndShift(true);
		}
		if (Officer)
		{
			const FVector Fwd = Officer->GetActorForwardVector();
			ViewFrom(Officer->GetActorLocation() + Fwd * 420.f + FVector(0.f, 0.f, 80.f), Officer->GetActorLocation() + FVector(0.f, 0.f, 40.f));
		}
		break;

	case 15:
		Shot(TEXT("06_shift_report"));
		break;

	case 16:
		if (PC && Officer)
		{
			PC->SetViewTargetWithBlend(Officer, 0.f);
		}
		UE_LOG(LogFTO, Display, TEXT("SMOKE: tour complete."));
		if (FParse::Param(FCommandLine::Get(), TEXT("FTOSmokeTestQuit")))
		{
			FPlatformMisc::RequestExit(false, TEXT("FTOSmokeTest"));
		}
		break;

	default:
		break;
	}
}
