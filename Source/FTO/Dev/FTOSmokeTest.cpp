#include "Dev/FTOSmokeTest.h"
#include "City/FTOCityGenerator.h"
#include "City/FTOTrafficCar.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameMode.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Dev/FTOAnimDummy.h"
#include "Crime/FTOCrimeDirector.h"
#include "Vehicles/FTOCruiser.h"
#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "FTO.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

AFTOSmokeTest::AFTOSmokeTest()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

bool AFTOSmokeTest::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("FTOSmokeTest"));
}

void AFTOSmokeTest::BeginPlay()
{
	Super::BeginPlay();
	BuildSteps();
}

// ------------------------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------------------------

APlayerController* AFTOSmokeTest::GetPC() const
{
	return GetWorld()->GetFirstPlayerController();
}

APawn* AFTOSmokeTest::GetPawn() const
{
	const APlayerController* PC = GetPC();
	return PC ? PC->GetPawn() : nullptr;
}

AFTOGameMode* AFTOSmokeTest::GetAuthGameMode() const
{
	return GetWorld()->GetAuthGameMode<AFTOGameMode>();
}

AFTOCityGenerator* AFTOSmokeTest::GetCity() const
{
	for (TActorIterator<AFTOCityGenerator> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
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

void AFTOSmokeTest::Shot(const TCHAR* Name, bool bShowUI)
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("SmokeTest"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	FString Tag;
	FParse::Value(FCommandLine::Get(), TEXT("FTOSmokeTag="), Tag);
	const FString Path = FPaths::Combine(Dir, (Tag.IsEmpty() ? FString() : Tag + TEXT("_")) + FString(Name) + TEXT(".png"));
	FScreenshotRequest::RequestScreenshot(Path, bShowUI, false);
	UE_LOG(LogFTO, Display, TEXT("SMOKE: screenshot %s"), *Path);
}

void AFTOSmokeTest::ViewFrom(const FVector& Location, const FVector& LookAt)
{
	APlayerController* PC = GetPC();
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

void AFTOSmokeTest::SetHUDVisible(bool bVisible)
{
	if (APlayerController* PC = GetPC())
	{
		if (AHUD* HUD = PC->GetHUD())
		{
			HUD->bShowHUD = bVisible;
		}
	}
}

void AFTOSmokeTest::AddStep(const TCHAR* Name, float Delay, TFunction<void()> Action)
{
	Steps.Add({ Name, Delay, MoveTemp(Action) });
}

void AFTOSmokeTest::AddShot(const TCHAR* Name, float Delay, bool bShowUI)
{
	const FString ShotName = Name;
	AddStep(Name, Delay, [this, ShotName, bShowUI]() { Shot(*ShotName, bShowUI); });
}

// ------------------------------------------------------------------------------------------
// The tour
// ------------------------------------------------------------------------------------------

void AFTOSmokeTest::BuildSteps()
{
	// Menu, then the lobby.
	AddStep(TEXT("open menu"), 0.8f, [this]()
	{
		if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetPC())) { PC->SetMenuVisible(true); }
	});
	AddShot(TEXT("00_menu"), 0.3f);
	AddStep(TEXT("close menu"), 0.5f, [this]()
	{
		if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetPC())) { PC->SetMenuVisible(false); }
	});
	AddShot(TEXT("01_roll_call"), 1.f);

	// Officer close-up, walking, and the whistle.
	AddStep(TEXT("officer close-up"), 1.5f, [this]()
	{
		if (APawn* Officer = GetPawn())
		{
			SetHUDVisible(false);
			const FVector Fwd = Officer->GetActorForwardVector();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);
			ViewFrom(Officer->GetActorLocation() + Fwd * 330.f - Right * 160.f + FVector(0.f, 0.f, 40.f), Officer->GetActorLocation() + FVector(0.f, 0.f, 10.f));
		}
	});
	AddShot(TEXT("01b_officer"), 0.5f, false);
	AddStep(TEXT("walk"), 0.8f, [this]()
	{
		if (APawn* Officer = GetPawn())
		{
			WalkDirection = Officer->GetActorForwardVector();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, WalkDirection);
			const FVector Mid = Officer->GetActorLocation() + WalkDirection * 380.f;
			ViewFrom(Mid + Right * 480.f + FVector(0.f, 0.f, 60.f), Mid + FVector(0.f, 0.f, 10.f));
			bWalkOfficer = true;
		}
	});
	AddShot(TEXT("01c_officer_walk"), 0.5f, false);
	AddStep(TEXT("whistle"), 1.2f, [this]()
	{
		bWalkOfficer = false;
		SetHUDVisible(true);
		if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		if (AFTOCharacter* Officer = Cast<AFTOCharacter>(GetPawn())) { Officer->BlowWhistle(); }
	});
	AddShot(TEXT("01d_whistle"), 0.5f);

	// Ragdoll: bowl over everyone nearby, then watch them get back up.
	AddStep(TEXT("knockdown"), 1.4f, [this]()
	{
		if (AFTOGameMode* GM = GetAuthGameMode()) { GM->FTOKnockdown(1800.f); }
		if (APawn* Officer = GetPawn())
		{
			ViewFrom(Officer->GetActorLocation() + FVector(-500.f, -500.f, 400.f), Officer->GetActorLocation());
		}
	});
	AddShot(TEXT("01e_knockdown"), 3.6f);
	AddShot(TEXT("01f_recovered"), 0.5f);

	// Every new clip in engine, on mannequins.
	AddStep(TEXT("anim gallery"), 1.5f, [this]()
	{
		SetHUDVisible(false);
		AFTOGameMode* GM = GetAuthGameMode();
		APawn* Officer = GetPawn();
		if (!GM || !Officer)
		{
			return;
		}
		GM->FTOAnimGallery();
		const FVector Fwd = Officer->GetActorForwardVector();
		ViewFrom(Officer->GetActorLocation() + FVector(0.f, 0.f, 220.f), Officer->GetActorLocation() + Fwd * 1050.f - FVector(0.f, 0.f, 20.f));
	});
	AddShot(TEXT("01g_anim_gallery"), 0.5f, false);
	AddStep(TEXT("clear gallery"), 0.5f, [this]()
	{
		SetHUDVisible(true);
		for (TActorIterator<AFTOAnimDummy> It(GetWorld()); It; ++It) { It->Destroy(); }
		if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
	});

	// Start the shift and stage a few incidents in view (server/standalone only).
	AddStep(TEXT("stage incidents"), 4.f, [this]()
	{
		AFTOGameMode* GM = GetAuthGameMode();
		APawn* Officer = GetPawn();
		if (!GM || !Officer)
		{
			return;
		}
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
	});
	AddShot(TEXT("02_on_duty"), 1.f);

	// Arrest: across town, catch a shoplifter, then bring them home.
	AddStep(TEXT("arrest"), 4.5f, [this]()
	{
		AFTOGameMode* GM = GetAuthGameMode();
		AFTOCharacter* Officer = Cast<AFTOCharacter>(GetPawn());
		AFTOCityGenerator* City = GetCity();
		if (!GM || !Officer || !City)
		{
			return;
		}
		const FFTOCityBlock* Far = nullptr;
		for (const FFTOCityBlock& Block : City->GetBlocks())
		{
			if (Block.District == EFTODistrict::Downtown && !Block.bPrecinct && !Block.bBank &&
				(!Far || FVector::DistSquared(Block.Center, City->GetPrecinctLocation()) > FVector::DistSquared(Far->Center, City->GetPrecinctLocation())))
			{
				Far = &Block;
			}
		}
		if (Far)
		{
			const FVector Spot = City->GetSidewalkCorner(Far->X, Far->Y, 0) + FVector(0.f, 0.f, 100.f);
			Officer->TeleportTo(Spot, FRotator(0.f, 45.f, 0.f));
			GM->GetCrimeDirector()->SpawnIncidentAt(TEXT("Shoplifting"), Spot + FVector(150.f, 0.f, -90.f), true);
		}
	});
	AddStep(TEXT("escort"), 1.5f, [this]()
	{
		// Walk a few steps so the cuffed suspect trots after us.
		if (APawn* Officer = GetPawn())
		{
			WalkDirection = Officer->GetActorForwardVector();
			bWalkOfficer = true;
		}
	});
	AddStep(TEXT("stop"), 0.6f, [this]() { bWalkOfficer = false; });
	AddShot(TEXT("02b_arrest"), 0.2f);
	AddStep(TEXT("go home"), 1.5f, [this]()
	{
		AFTOCharacter* Officer = Cast<AFTOCharacter>(GetPawn());
		AFTOCityGenerator* City = GetCity();
		if (Officer && City)
		{
			Officer->TeleportTo(City->GetPrecinctLocation() + FVector(0.f, 0.f, 100.f), FRotator(0.f, 180.f, 0.f));
		}
	});
	AddShot(TEXT("02c_booked"), 1.f);

	// City views.
	AddStep(TEXT("aerial"), 3.f, [this]()
	{
		if (AFTOCityGenerator* City = GetCity())
		{
			const FVector Extent = City->GetCityExtent();
			const FVector Center = City->GetActorLocation();
			ViewFrom(Center + FVector(-Extent.X * 1.1f, -Extent.Y * 0.6f, 14000.f), Center);
		}
	});
	AddShot(TEXT("03_city_aerial"), 1.f);
	AddStep(TEXT("street level"), 3.f, [this]()
	{
		if (AFTOCityGenerator* City = GetCity())
		{
			const int32 MidI = City->NumIntersectionsX() / 2;
			const int32 MidJ = City->NumIntersectionsY() / 2;
			ViewFrom(City->GetIntersection(MidI, 0) + FVector(0.f, -400.f, 350.f), City->GetIntersection(MidI, MidJ) + FVector(0.f, 0.f, 150.f));
		}
	});
	AddShot(TEXT("04_street_level"), 1.f);
	AddStep(TEXT("incident"), 3.f, [this]()
	{
		if (IncidentSpot.IsZero())
		{
			return;
		}
		// From the officer's side of the scene, raised so buildings don't get in the way.
		const APawn* Officer = GetPawn();
		const FVector Dir = Officer ? (IncidentSpot - Officer->GetActorLocation()).GetSafeNormal2D() : FVector::ForwardVector;
		ViewFrom(IncidentSpot - Dir * 750.f + FVector(0.f, 0.f, 420.f), IncidentSpot + FVector(0.f, 0.f, 100.f));
	});
	AddShot(TEXT("05_incident"), 2.f);

	// Win the shift: report card up, officers cheering.
	AddStep(TEXT("end shift"), 1.5f, [this]()
	{
		if (AFTOGameMode* GM = GetAuthGameMode()) { GM->FTOEndShift(true); }
		if (APawn* Officer = GetPawn())
		{
			const FVector Fwd = Officer->GetActorForwardVector();
			ViewFrom(Officer->GetActorLocation() + Fwd * 420.f + FVector(0.f, 0.f, 80.f), Officer->GetActorLocation() + FVector(0.f, 0.f, 40.f));
		}
	});
	AddShot(TEXT("06_shift_report"), 1.f);

	// Eye level on a busy downtown sidewalk.
	AddStep(TEXT("sidewalk"), 2.f, [this]()
	{
		SetHUDVisible(false);
		AFTOCityGenerator* City = GetCity();
		if (!City)
		{
			return;
		}
		for (const FFTOCityBlock& Block : City->GetBlocks())
		{
			if (Block.District == EFTODistrict::Downtown && !Block.bPrecinct && !Block.bBank)
			{
				ViewFrom(City->GetSidewalkCorner(Block.X, Block.Y, 0) + FVector(0.f, 0.f, 170.f), City->GetSidewalkCorner(Block.X, Block.Y, 1) + FVector(0.f, 0.f, 120.f));
				break;
			}
		}
	});
	AddShot(TEXT("07_sidewalk"), 1.f);

	// Side-on look at a passing car.
	AddStep(TEXT("traffic"), 0.05f, [this]()
	{
		for (TActorIterator<AFTOTrafficCar> It(GetWorld()); It; ++It)
		{
			const FVector CarLoc = It->GetActorLocation();
			ViewFrom(CarLoc + It->GetActorRightVector() * 700.f + It->GetActorForwardVector() * 250.f + FVector(0.f, 0.f, 150.f), CarLoc);
			break;
		}
	});
	AddShot(TEXT("08_traffic"), 1.f, false);

	// Drive: the host takes the nearest cruiser; a client asks the server for one and drives it itself.
	AddStep(TEXT("get in"), 2.5f, [this]()
	{
		SetHUDVisible(true);
		APlayerController* PC = GetPC();
		AFTOGameMode* GM = GetAuthGameMode();
		if (GM)
		{
			if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>()) { GS->SetShiftPhase(EFTOShiftPhase::OnDuty); }
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			AFTOCruiser* Nearest = nullptr;
			for (TActorIterator<AFTOCruiser> It(GetWorld()); It && Cop; ++It)
			{
				if (!Nearest || FVector::DistSquared(It->GetActorLocation(), Cop->GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), Cop->GetActorLocation()))
				{
					Nearest = *It;
				}
			}
			if (Nearest && Cop)
			{
				Nearest->Interact(Cop);
				Nearest->SetSiren(true);
				Nearest->SetAutopilot(true, 1.f, 0.35f);
				TestCruiser = Nearest;
			}
		}
		else if (AFTOPlayerController* FTOPC = Cast<AFTOPlayerController>(PC))
		{
			FTOPC->ServerEnterNearestCruiser();
		}
	});
	AddStep(TEXT("client drives"), 1.5f, [this]()
	{
		if (!GetAuthGameMode())
		{
			if (AFTOCruiser* Mine = Cast<AFTOCruiser>(GetPawn()))
			{
				Mine->SetAutopilot(true, 1.f, 0.2f);
				TestCruiser = Mine;
			}
		}
		Shot(TEXT("09_driving"));
	});
	AddStep(TEXT("get out"), 1.2f, [this]()
	{
		if (TestCruiser)
		{
			TestCruiser->SetAutopilot(false);
			TestCruiser->RequestExit();
		}
	});
	AddShot(TEXT("10_got_out"), 1.f);

	AddStep(TEXT("done"), 0.f, [this]()
	{
		if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		const float Elapsed = GetWorld()->GetRealTimeSeconds() - ReadyTime;
		UE_LOG(LogFTO, Display, TEXT("SMOKE: tour complete. Average %.1f fps over %.1f s."), FramesSinceReady / FMath::Max(0.01f, Elapsed), Elapsed);
		if (FParse::Param(FCommandLine::Get(), TEXT("FTOSmokeTestQuit")))
		{
			FPlatformMisc::RequestExit(false, TEXT("FTOSmokeTest"));
		}
	});
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
		if (APlayerController* PC = GetPC())
		{
			PC->ConsoleCommand(TEXT("r.MotionBlurQuality 0")); // crisp stills
		}
		UE_LOG(LogFTO, Display, TEXT("SMOKE: world ready, starting tour."));
	}

	++FramesSinceReady;

	if (bWalkOfficer)
	{
		if (APawn* Officer = GetPawn())
		{
			Officer->AddMovementInput(WalkDirection, 1.f);
		}
	}

	// One step per frame, and never while shaders are still compiling (the shot would be grey).
	if (Steps.IsValidIndex(NextStep) && Now >= NextStepTime && AreShadersReady())
	{
		const FStep& Step = Steps[NextStep];
		UE_LOG(LogFTO, Display, TEXT("SMOKE: %s"), *Step.Name);
		Step.Action();
		NextStepTime = Now + Step.Delay;
		++NextStep;
	}
}
