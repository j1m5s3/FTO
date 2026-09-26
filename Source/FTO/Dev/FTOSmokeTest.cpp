#include "Dev/FTOSmokeTest.h"
#include "City/FTOCityGenerator.h"
#include "City/FTOTrafficCar.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameMode.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Dev/FTOAnimDummy.h"
#include "Crime/FTOArrestee.h"
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

	// Inside every kind of building: from just in the door, looking into the room (room space is X into the
	// room from the door, Y across it).
	struct FInside
	{
		EFTOBuildingType Type;
		const TCHAR* Shot;
		FVector Eye;
		FVector Look;
	};
	static const FInside Insides[] =
	{
		{ EFTOBuildingType::Shop,      TEXT("12a_shop"),        FVector(60.f, -150.f, 240.f), FVector(700.f, 100.f, 60.f) },
		{ EFTOBuildingType::Diner,     TEXT("12b_diner"),       FVector(60.f, -150.f, 240.f), FVector(800.f, 100.f, 60.f) },
		{ EFTOBuildingType::Bar,       TEXT("12c_bar"),         FVector(60.f, -150.f, 240.f), FVector(700.f, 250.f, 60.f) },
		{ EFTOBuildingType::Office,    TEXT("12d_office"),      FVector(60.f, -150.f, 240.f), FVector(700.f, 100.f, 60.f) },
		{ EFTOBuildingType::Home,      TEXT("12e_home"),        FVector(40.f, 100.f, 230.f),  FVector(500.f, -150.f, 60.f) },
		{ EFTOBuildingType::Warehouse, TEXT("12f_warehouse"),   FVector(80.f, 0.f, 350.f),    FVector(1200.f, 0.f, 100.f) },
		{ EFTOBuildingType::Bank,      TEXT("12g_bank"),        FVector(60.f, -300.f, 260.f), FVector(1300.f, 0.f, 120.f) },
		{ EFTOBuildingType::Bank,      TEXT("12h_vault"),       FVector(1700.f, -500.f, 250.f), FVector(2500.f, 300.f, 100.f) },
		{ EFTOBuildingType::Precinct,  TEXT("13a_lobby"),       FVector(60.f, -400.f, 250.f), FVector(320.f, 150.f, 100.f) },
		{ EFTOBuildingType::Precinct,  TEXT("13b_armory"),      FVector(700.f, -250.f, 250.f), FVector(1340.f, 50.f, 120.f) },
		{ EFTOBuildingType::Precinct,  TEXT("13c_cells"),       FVector(650.f, 700.f, 250.f), FVector(1250.f, 900.f, 100.f) },
		{ EFTOBuildingType::Precinct,  TEXT("13d_briefing"),    FVector(620.f, -700.f, 260.f), FVector(1340.f, -900.f, 120.f) },
	};
	for (const FInside& Inside : Insides)
	{
		AddStep(TEXT("go inside"), 1.2f, [this, Inside]()
		{
			const AFTOCityGenerator* City = GetCity();
			if (const FFTOBuilding* B = City ? City->FindBuilding(Inside.Type) : nullptr)
			{
				ViewFrom(B->Room.TransformPosition(Inside.Eye), B->Room.TransformPosition(Inside.Look));
			}
		});
		AddShot(Inside.Shot, 0.3f, false);
	}

	// Walk in through front doors: the doorways must really be open. (Server only: a client can't teleport itself.)
	for (const EFTOBuildingType Type : { EFTOBuildingType::Diner, EFTOBuildingType::Precinct, EFTOBuildingType::Home })
	{
		if (GetNetMode() == NM_Client)
		{
			break;
		}
		AddStep(TEXT("to the door"), 2.6f, [this, Type]()
		{
			AFTOCityGenerator* City = GetCity();
			APawn* Officer = GetPawn();
			const FFTOBuilding* B = City ? City->FindBuilding(Type) : nullptr;
			if (B && Officer)
			{
				const FVector In = B->Room.GetRotation().GetForwardVector();
				Officer->TeleportTo(B->DoorOutside + FVector(0.f, 0.f, 100.f), In.Rotation());
				WalkDirection = In;
				bWalkOfficer = true;
				ViewFrom(B->Room.TransformPosition(FVector(500.f, 250.f, 260.f)), B->Room.TransformPosition(FVector(0.f, 0.f, 80.f)));
			}
		});
		AddStep(TEXT("check inside"), 0.3f, [this, Type]()
		{
			bWalkOfficer = false;
			AFTOCityGenerator* City = GetCity();
			const FFTOBuilding* B = City ? City->FindBuilding(Type) : nullptr;
			if (const APawn* Officer = GetPawn(); B && Officer)
			{
				const float Inside = B->Room.InverseTransformPosition(Officer->GetActorLocation()).X;
				UE_LOG(LogFTO, Display, TEXT("SMOKE: walked into the %s: %s (%.0f cm in)."), *StaticEnum<EFTOBuildingType>()->GetNameStringByValue(int64(Type)),
					Inside > 100.f ? TEXT("yes") : TEXT("NO"), Inside);
			}
		});
		if (Type == EFTOBuildingType::Diner)
		{
			AddShot(TEXT("14_walked_in"), 0.3f, false);
		}
	}

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

	// Close on a car coming our way: someone's at the wheel behind the glass.
	AddStep(TEXT("traffic driver"), 0.05f, [this]()
	{
		for (TActorIterator<AFTOTrafficCar> It(GetWorld()); It; ++It)
		{
			const FVector CarLoc = It->GetActorLocation();
			ViewFrom(CarLoc + It->GetActorForwardVector() * 430.f - It->GetActorRightVector() * 250.f + FVector(0.f, 0.f, 80.f), CarLoc + FVector(0.f, 0.f, 70.f));
			break;
		}
	});
	AddShot(TEXT("08b_traffic_driver"), 1.f, false);

	// Drive: the host takes a cruiser out on patrol with a suspect in the back; a client asks the
	// server for a cruiser and drives it itself.
	AddStep(TEXT("get in"), 2.5f, [this]()
	{
		SetHUDVisible(true);
		APlayerController* PC = GetPC();
		AFTOGameMode* GM = GetAuthGameMode();
		if (GM)
		{
			if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>()) { GS->SetShiftPhase(EFTOShiftPhase::OnDuty); }
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			AFTOCityGenerator* City = GetCity();
			AFTOCruiser* Nearest = nullptr;
			for (TActorIterator<AFTOCruiser> It(GetWorld()); It && Cop; ++It)
			{
				if (!Nearest || FVector::DistSquared(It->GetActorLocation(), Cop->GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), Cop->GetActorLocation()))
				{
					Nearest = *It;
				}
			}
			if (Nearest && Cop && City)
			{
				// Across town (so the suspect isn't booked the moment they sit down), on a street that
				// runs back toward the middle rather than off the edge of the map.
				const int32 NX = City->NumIntersectionsX();
				const int32 NY = City->NumIntersectionsY();
				FIntPoint Far(1, 1);
				for (int32 I = 1; I < NX - 1; ++I)
				{
					for (int32 J = 1; J < NY - 1; ++J)
					{
						if (FVector::DistSquared2D(City->GetIntersection(I, J), City->GetPrecinctLocation()) >
							FVector::DistSquared2D(City->GetIntersection(Far.X, Far.Y), City->GetPrecinctLocation()))
						{
							Far = FIntPoint(I, J);
						}
					}
				}
				const FRotator Heading(0.f, Far.X < NX / 2 ? 0.f : 180.f, 0.f);
				const FVector Fwd = Heading.Vector();
				const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);
				const FVector Street = City->GetIntersection(Far.X, Far.Y) + Fwd * 700.f + Right * City->GetRoadWidth() * 0.25f;
				Nearest->SetActorLocationAndRotation(Street + FVector(0.f, 0.f, AFTOCruiser::RideHeight), Heading);
				Cop->TeleportTo(Street + Right * 260.f + FVector(0.f, 0.f, 100.f), Heading);
				if (AFTOArrestee* Suspect = GetWorld()->SpawnActor<AFTOArrestee>(AFTOArrestee::StaticClass(), Street + Right * 260.f - Fwd * 200.f + FVector(0.f, 0.f, 92.f), Heading))
				{
					Suspect->Init(Cop, 4.f, INVTEXT("Loitering with intent to loiter"));
				}
				Nearest->Interact(Cop);
				Nearest->SetSiren(true);
				Nearest->SetAutopilot(true, 0.7f, 0.f);
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
	AddStep(TEXT("seat view"), 1.2f, [this]()
	{
		if (TestCruiser) { TestCruiser->SetInteriorView(true); }
	});
	AddShot(TEXT("09b_interior"), 0.5f);
	AddStep(TEXT("chase view"), 0.5f, [this]()
	{
		if (TestCruiser) { TestCruiser->SetInteriorView(false); }
	});

	// A second officer hops in beside the driver (a stand-in, as there's only one player here).
	AddStep(TEXT("ride shotgun"), 1.5f, [this]()
	{
		if (!GetAuthGameMode() || !TestCruiser)
		{
			return;
		}
		TestCruiser->SetAutopilot(true, 0.f, 0.f);
		TestCruiser->StopDead(); // hold still for the photo
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TestPassenger = GetWorld()->SpawnActor<AFTOCharacter>(AFTOCharacter::StaticClass(), TestCruiser->GetActorLocation() + TestCruiser->GetActorRightVector() * 250.f, TestCruiser->GetActorRotation(), Params);
		if (TestPassenger)
		{
			TestCruiser->Interact(TestPassenger);
		}
	});
	AddStep(TEXT("look inside"), 1.f, [this]()
	{
		if (!GetAuthGameMode() || !TestCruiser)
		{
			return;
		}
		SetHUDVisible(false);
		const FVector Car = TestCruiser->GetActorLocation();
		ViewFrom(Car + TestCruiser->GetActorRightVector() * 430.f + TestCruiser->GetActorForwardVector() * 120.f + FVector(0.f, 0.f, 110.f), Car + FVector(0.f, 0.f, 70.f));
	});
	AddShot(TEXT("09c_shotgun"), 0.5f, false);

	// Leave from the seat view: the driver's hidden head must come back once they're out.
	AddStep(TEXT("seat view again"), 0.4f, [this]()
	{
		if (TestCruiser) { TestCruiser->SetInteriorView(true); }
	});
	AddStep(TEXT("get out"), 1.2f, [this]()
	{
		SetHUDVisible(true);
		if (TestCruiser)
		{
			TestCruiser->SetAutopilot(false);
			if (TestPassenger)
			{
				TestCruiser->LetOut(TestPassenger);
				TestPassenger->Destroy();
			}
			TestCruiser->RequestExit();
		}
		if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
	});
	AddStep(TEXT("check head"), 0.f, [this]()
	{
		if (const AFTOCharacter* Me = Cast<AFTOCharacter>(GetPawn()))
		{
			UE_LOG(LogFTO, Display, TEXT("SMOKE: officer's head %s after getting out."),
				Me->GetMesh()->IsBoneHiddenByName(TEXT("head")) ? TEXT("STILL HIDDEN") : TEXT("visible"));
		}
	});
	AddShot(TEXT("10_got_out"), 1.f);

	// Two players (-FTOSmokeRideAlong): the host parks in a cruiser and waits; the client (whose tour
	// runs ~20 s behind) hops in beside them, looks around from both cameras, and gets out again.
	if (FParse::Param(FCommandLine::Get(), TEXT("FTOSmokeRideAlong")))
	{
		if (GetNetMode() != NM_Client)
		{
			AddStep(TEXT("wait for a rider"), 22.f, [this]()
			{
				AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
				AFTOCityGenerator* City = GetCity();
				AFTOCruiser* Parked = nullptr;
				for (TActorIterator<AFTOCruiser> It(GetWorld()); It && City; ++It)
				{
					if (!It->HasDriver() && (!Parked ||
						FVector::DistSquared(It->GetActorLocation(), City->GetPrecinctLocation()) < FVector::DistSquared(Parked->GetActorLocation(), City->GetPrecinctLocation())))
					{
						Parked = *It;
					}
				}
				if (Cop && Parked)
				{
					Parked->Interact(Cop);
					Parked->SetInteriorView(false);
					TestCruiser = Parked;
					SetHUDVisible(false);
					ViewFrom(Parked->GetActorLocation() + Parked->GetActorRightVector() * 430.f + Parked->GetActorForwardVector() * 120.f + FVector(0.f, 0.f, 110.f),
						Parked->GetActorLocation() + FVector(0.f, 0.f, 70.f));
				}
			});
			AddShot(TEXT("11_host_rider"), 2.f, false);
			AddShot(TEXT("11b_host_rider"), 1.f, false);
		}
		else
		{
			AddStep(TEXT("ride along"), 3.f, [this]()
			{
				if (AFTOPlayerController* FTOPC = Cast<AFTOPlayerController>(GetPC()))
				{
					FTOPC->bPreferInteriorView = false; // start from the chase camera
					FTOPC->ServerRideAlong();
				}
			});
			AddShot(TEXT("11_riding"), 0.5f);
			AddStep(TEXT("seat view"), 1.f, [this]()
			{
				if (AFTOCharacter* Me = Cast<AFTOCharacter>(GetPawn())) { Me->ToggleSeatView(); }
			});
			AddShot(TEXT("11b_seat_view"), 0.5f);
			AddStep(TEXT("look at the driver"), 0.6f, [this]()
			{
				if (APlayerController* PC = GetPC()) { PC->SetControlRotation(PC->GetControlRotation() + FRotator(-5.f, -75.f, 0.f)); }
			});
			AddShot(TEXT("11b2_seat_view_driver"), 0.5f);
			AddStep(TEXT("chase view"), 0.5f, [this]()
			{
				if (AFTOCharacter* Me = Cast<AFTOCharacter>(GetPawn())) { Me->ToggleSeatView(); }
			});
			AddStep(TEXT("hop out"), 1.5f, [this]()
			{
				if (AFTOCharacter* Me = Cast<AFTOCharacter>(GetPawn())) { Me->LeaveVehicle(); }
			});
			AddShot(TEXT("11c_out"), 0.5f);
		}
	}

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
