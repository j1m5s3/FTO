#include "Dev/FTOSmokeTest.h"
#include "City/FTOCityGenerator.h"
#include "City/FTOInteriorLife.h"
#include "City/FTOOccupant.h"
#include "City/FTOTrafficCar.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameMode.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Dev/FTOAnimDummy.h"
#include "Physics/FTOKnockdownComponent.h"
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
	AddStep(TEXT("go home"), 3.5f, [this]()
	{
		// Straight to the holding cells (the suspect catches up): booked, they let themselves into a cell and sit.
		AFTOCharacter* Officer = Cast<AFTOCharacter>(GetPawn());
		AFTOCityGenerator* City = GetCity();
		const FFTOBuilding* Precinct = City ? City->FindBuilding(EFTOBuildingType::Precinct) : nullptr;
		if (Officer && Precinct)
		{
			const FVector Into = Precinct->Room.GetRotation().GetForwardVector();
			Officer->TeleportTo(City->GetHoldingCellsLocation() - Into * 100.f + FVector(0.f, 0.f, 100.f), Into.Rotation());
		}
	});
	AddStep(TEXT("watch the cells"), 0.5f, [this]()
	{
		const AFTOCityGenerator* City = GetCity();
		if (const FFTOBuilding* Precinct = City ? City->FindBuilding(EFTOBuildingType::Precinct) : nullptr)
		{
			// From the far corner of the cell block, looking at the first cell.
			ViewFrom(Precinct->Room.TransformPosition(FVector(640.f, 1200.f, 240.f)), Precinct->Room.TransformPosition(FVector(1250.f, 640.f, 70.f)));
		}
		for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
		{
			UE_LOG(LogFTO, Display, TEXT("SMOKE: suspect %s (%s)."), It->IsJailed() ? TEXT("sat in a cell") :
				*StaticEnum<EFTOArresteeState>()->GetNameStringByValue(int64(It->GetArrestState())), *It->GetCrime().ToString());
		}
	});
	AddShot(TEXT("02c_booked"), 0.5f);
	AddStep(TEXT("back to the officer"), 0.5f, [this]()
	{
		if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
	});

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

	// Life indoors: who's in, a close look at people sat in a diner, trouble inside (a hold-up, a brawl), a crook
	// questioned, and a look through a shop window from the street.
	AddStep(TEXT("cells check"), 0.f, [this]()
	{
		for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
		{
			UE_LOG(LogFTO, Display, TEXT("SMOKE: in the cells: %s, %s, doing %s at %s."), *It->GetCrime().ToString(), It->IsJailed() ? TEXT("sat down") : TEXT("NOT sat"),
				*StaticEnum<EFTOAnimAction>()->GetNameStringByValue(int64(It->GetAnimAction())), *It->GetActorLocation().ToCompactString());
		}
	});
	AddStep(TEXT("census"), 0.f, [this]()
	{
		if (const AFTOInteriorLife* Life = AFTOInteriorLife::Get(GetWorld()))
		{
			UE_LOG(LogFTO, Display, TEXT("SMOKE: interiors: %d rooms awake, %d people inside."), Life->GetAwakeCount(), Life->GetOccupantCount());
		}
	});
	AddStep(TEXT("diner close-up"), 1.2f, [this]()
	{
		const AFTOCityGenerator* City = GetCity();
		if (const FFTOBuilding* B = City ? City->FindBuilding(EFTOBuildingType::Diner) : nullptr)
		{
			// Along the booths on the left wall, from the door end.
			ViewFrom(B->Room.TransformPosition(FVector(90.f, B->YMin + 330.f, 190.f)), B->Room.TransformPosition(FVector(420.f, B->YMin + 60.f, 70.f)));
		}
	});
	AddShot(TEXT("15a_diner_close"), 0.3f, false);
	AddStep(TEXT("stool close-up"), 1.2f, [this]()
	{
		const AFTOCityGenerator* City = GetCity();
		if (const FFTOBuilding* B = City ? City->FindBuilding(EFTOBuildingType::Diner) : nullptr)
		{
			ViewFrom(B->Room.TransformPosition(FVector(B->Depth - 420.f, -260.f, 170.f)), B->Room.TransformPosition(FVector(B->Depth - 180.f, 60.f, 80.f)));
		}
	});
	AddShot(TEXT("15b_diner_stools"), 0.3f, false);
	if (GetNetMode() != NM_Client)
	{
		for (const EFTOBuildingType Type : { EFTOBuildingType::Shop, EFTOBuildingType::Bar })
		{
			AddStep(TEXT("trouble inside"), 2.f, [this, Type]()
			{
				AFTOGameMode* GM = GetAuthGameMode();
				AFTOCityGenerator* City = GetCity();
				const int32 Index = City ? City->FindBuildingIndex(Type) : INDEX_NONE;
				const FFTOBuilding* B = City ? City->GetBuilding(Index) : nullptr;
				if (!GM || !B || B->CrimeSpots.IsEmpty())
				{
					return;
				}
				GM->FTOSkipBriefing();
				const TCHAR* Crime = Type == EFTOBuildingType::Shop ? TEXT("ArmedRobbery") : TEXT("BarFight");
				GM->GetCrimeDirector()->SpawnIncidentAt(Crime, B->CrimeSpots[0], Index, true);
				// Over the perp's shoulder, from the room side (away from the shop windows).
				const FVector Spot = B->CrimeSpots[0].GetLocation();
				const FVector Toward = B->CrimeSpots[0].GetRotation().GetForwardVector();
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward);
				const FVector Middle = B->GetCenter();
				const float Sign = FVector::DotProduct(Middle - Spot, Side) >= 0.f ? 1.f : -1.f;
				ViewFrom(Spot - Toward * 240.f + Side * Sign * 220.f + FVector(0.f, 0.f, 230.f), Spot + Toward * 80.f + FVector(0.f, 0.f, 90.f));
			});
			AddShot(Type == EFTOBuildingType::Shop ? TEXT("15c_holdup") : TEXT("15d_bar_fight"), 0.3f, false);
		}
		AddStep(TEXT("question a crook"), 1.5f, [this]()
		{
			AFTOCharacter* Officer = Cast<AFTOCharacter>(GetPawn());
			AFTOOccupant* Crook = nullptr;
			for (TActorIterator<AFTOOccupant> It(GetWorld()); It && !Crook; ++It)
			{
				Crook = It->GetRole() == EFTOOccupantRole::Crook ? *It : nullptr;
			}
			if (!Crook)
			{
				// None lying low nearby: plant one in the warehouse.
				AFTOInteriorLife* Life = AFTOInteriorLife::Get(GetWorld());
				const AFTOCityGenerator* City = GetCity();
				Crook = Life && City ? Life->PlantCrook(City->FindBuildingIndex(EFTOBuildingType::Warehouse)) : nullptr;
			}
			if (!Officer || !Crook)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: no crook about to question."));
				return;
			}
			const FVector Front = Crook->GetActorLocation() + Crook->GetActorForwardVector() * 140.f;
			Officer->TeleportTo(Front + FVector(0.f, 0.f, 6.f), (Crook->GetActorLocation() - Front).Rotation());
			// Along the aisle, over the officer's shoulder.
			ViewFrom(Front + Crook->GetActorForwardVector() * 260.f + FVector(0.f, 0.f, 170.f), Crook->GetActorLocation());
			Crook->Interact(Officer);
			UE_LOG(LogFTO, Display, TEXT("SMOKE: questioned a crook: %s."), IsValid(Crook) && !Crook->IsActorBeingDestroyed() ? TEXT("they kept schtum") : TEXT("they confessed"));
		});
		AddShot(TEXT("15e_questioned"), 0.3f);
	}
	AddStep(TEXT("shop window"), 0.f, [this]()
	{
		// From the pavement, through the shop window beside the door, to the middle of the shop.
		const AFTOCityGenerator* City = GetCity();
		const FFTOBuilding* B = City ? City->FindBuilding(EFTOBuildingType::Shop) : nullptr;
		if (!B)
		{
			return;
		}
		const FVector Out = -B->Room.GetRotation().GetForwardVector();
		const FVector Along = B->Room.GetRotation().GetRightVector();
		const FVector Street = B->DoorOutside + Along * 200.f + FVector(0.f, 0.f, 150.f);
		const FVector Inside = B->Room.TransformPosition(FVector(400.f, 200.f, 150.f));
		FHitResult Hit;
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Street, Inside, ECC_Visibility);
		UE_LOG(LogFTO, Display, TEXT("SMOKE: through a shop window: %s%s."), bBlocked ? TEXT("BLOCKED by ") : TEXT("clear"),
			bBlocked ? *GetNameSafe(Hit.GetComponent()) : TEXT(""));
		const FVector Lintel(0.f, 0.f, 180.f); // 3.3 m up: the wall over the window
		const bool bWalled = GetWorld()->LineTraceSingleByChannel(Hit, Street + Lintel, Inside + Lintel, ECC_Visibility);
		UE_LOG(LogFTO, Display, TEXT("SMOKE: through the wall above it: %s."), bWalled ? TEXT("blocked") : TEXT("CLEAR"));
	});

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

	// Ragdolls in the world (host only): drive into three citizens, then tackle one, then watch them get up.
	if (GetNetMode() != NM_Client)
	{
		AddStep(TEXT("bowling"), 1.6f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!TestCruiser || !Cop)
			{
				return;
			}
			// The tour's staged crimes have the city near boiling point by now: calm it down so the shift
			// doesn't end (report card over everything) mid-test.
			if (AFTOGameMode* GM = GetAuthGameMode())
			{
				GM->FTOAddChaos(-100.f);
			}
			if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
			{
				GS->SetShiftPhase(EFTOShiftPhase::OnDuty);
				ChaosBefore = GS->GetChaos();
			}
			const FVector Car = TestCruiser->GetActorLocation();
			const FVector Fwd = TestCruiser->GetActorForwardVector();
			const FVector Right = TestCruiser->GetActorRightVector();
			const float Ground = Car.Z - AFTOCruiser::RideHeight;
			Pins.Reset();
			for (TActorIterator<AFTOPedestrian> It(GetWorld()); It && Pins.Num() < 3; ++It)
			{
				if (!It->IsA<AFTOOccupant>())
				{
					const int32 k = Pins.Num();
					const FVector Spot = Car + Fwd * (1500.f + k * 220.f) + Right * (k - 1) * 70.f;
					It->TeleportAndHold(FVector(Spot.X, Spot.Y, Ground + AFTOPedestrian::HalfHeight));
					Pins.Add(*It);
				}
			}
			TestCruiser->Interact(Cop);
			TestCruiser->SetAutopilot(true, 1.f, 0.f);
			SetHUDVisible(true);
		});
		AddStep(TEXT("chase cam"), 0.3f, [this]()
		{
			// Just before the bumper meets the first of them: from behind and above the car.
			if (TestCruiser)
			{
				const FVector Car = TestCruiser->GetActorLocation();
				const FVector Fwd = TestCruiser->GetActorForwardVector();
				ViewFrom(Car - Fwd * 650.f + FVector(0.f, 0.f, 420.f), Car + Fwd * 900.f);
			}
		});
		AddShot(TEXT("16a_bowled_over"), 0.5f);
		AddStep(TEXT("bowling result"), 1.f, [this]()
		{
			int32 Down = 0;
			for (const TWeakObjectPtr<AFTOPedestrian>& Pin : Pins)
			{
				Down += Pin.IsValid() && Pin->GetKnockdown()->IsDown() ? 1 : 0;
			}
			const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
			UE_LOG(LogFTO, Display, TEXT("SMOKE: bowled over %d of %d citizens (chaos %+.1f, %d on the report card)."), Down, Pins.Num(),
				GS ? GS->GetChaos() - ChaosBefore : 0.f, GS ? GS->CiviliansBowledOver : 0);
			if (TestCruiser)
			{
				TestCruiser->SetAutopilot(false);
				TestCruiser->StopDead();
				TestCruiser->RequestExit();
			}
		});
		AddStep(TEXT("tackle"), 0.35f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop)
			{
				return;
			}
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(Cop, 0.f); }
			TackleTarget.Reset();
			for (TActorIterator<AFTOPedestrian> It(GetWorld()); It; ++It)
			{
				if (!It->IsA<AFTOOccupant>() && !It->GetKnockdown()->IsDown())
				{
					TackleTarget = *It;
					break;
				}
			}
			if (TackleTarget.IsValid() && TestCruiser)
			{
				// Face along the road beside the car, the citizen a few steps ahead; film from the side away from the car.
				const FVector Fwd = TestCruiser->GetActorForwardVector();
				const FVector Right = TestCruiser->GetActorRightVector();
				const float Side = FVector::DotProduct(Cop->GetActorLocation() - TestCruiser->GetActorLocation(), Right) >= 0.f ? 1.f : -1.f;
				Cop->SetActorRotation(Fwd.Rotation());
				const FVector Spot = Cop->GetActorLocation() + Fwd * 280.f;
				TackleTarget->TeleportAndHold(FVector(Spot.X, Spot.Y, Cop->GetActorLocation().Z - 96.f + AFTOPedestrian::HalfHeight));
				ViewFrom(Cop->GetActorLocation() + Fwd * 150.f + Right * Side * 560.f + FVector(0.f, 0.f, 120.f), Cop->GetActorLocation() + Fwd * 180.f);
			}
			Cop->TacklePressed();
		});
		AddShot(TEXT("16b_tackle"), 0.6f);
		AddStep(TEXT("tackle result"), 2.9f, [this]()
		{
			UE_LOG(LogFTO, Display, TEXT("SMOKE: tackle: %s."), TackleTarget.IsValid() && TackleTarget->GetKnockdown()->IsDown() ? TEXT("they went down") : TEXT("MISSED"));
		});
		AddStep(TEXT("dazed"), 0.4f, [this]()
		{
			SetHUDVisible(false);
			if (TackleTarget.IsValid())
			{
				const FVector Who = TackleTarget->GetActorLocation();
				ViewFrom(Who + TackleTarget->GetActorForwardVector() * 260.f + TackleTarget->GetActorRightVector() * 120.f + FVector(0.f, 0.f, 40.f), Who - FVector(0.f, 0.f, 50.f));
				UE_LOG(LogFTO, Display, TEXT("SMOKE: after the tackle they're %s."), TackleTarget->GetKnockdown()->IsDazed() ? TEXT("sat up, seeing stars") : TEXT("NOT dazed"));
			}
		});
		AddShot(TEXT("16c_dazed"), 0.3f, false);
		AddStep(TEXT("back to the officer"), 0.3f, [this]()
		{
			SetHUDVisible(true);
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		});
	}

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
