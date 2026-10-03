#include "Dev/FTOSmokeTest.h"
#include "Combat/FTOFighting.h"
#include "City/FTOLift.h"
#include "Audio/FTOFootsteps.h"
#include "City/FTOCityGenerator.h"
#include "City/FTOInteriorLife.h"
#include "City/FTOOccupant.h"
#include "City/FTOTrafficCar.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameMode.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Core/FTOPlayerState.h"
#include "Dev/FTOAnimDummy.h"
#include "InputActionValue.h"
#include "Radio/FTORadio.h"
#include "Scoring/FTOScoring.h"
#include "Sound/SoundEffectSource.h"
#include "Physics/FTOKnockdownComponent.h"
#include "Physics/FTODebris.h"
#include "Physics/FTODestruction.h"
#include "Physics/FTOImpact.h"
#include "Physics/FTOVehicleDamage.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/FTOArmoryRack.h"
#include "Weapons/FTOBallistics.h"
#include "Crime/FTOArrestee.h"
#include "Crime/FTOBomb.h"
#include "Crime/FTOCrimeDirector.h"
#include "Crime/FTOCrimeExtra.h"
#include "Crime/FTOGraffitiTag.h"
#include "Crime/FTOIncident.h"
#include "Crime/FTOPerp.h"
#include "Vehicles/FTOCruiser.h"
#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
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

AFTOPerp* AFTOSmokeTest::StagePerp(FName Crime, float Ahead)
{
	const APawn* Cop = GetPawn();
	AFTOGameMode* GM = GetAuthGameMode();
	if (!Cop || !GM)
	{
		return nullptr;
	}
	const FVector Fwd = Cop->GetActorForwardVector().GetSafeNormal2D();
	const FVector Feet = Cop->GetActorLocation() - FVector(0.f, 0.f, 96.f) + Fwd * Ahead;
	const AFTOIncident* Incident = GM->GetCrimeDirector()->SpawnIncidentAt(Crime, FTransform((-Fwd).Rotation(), Feet), INDEX_NONE, true);
	AFTOPerp* Perp = Incident ? Incident->GetPerp() : nullptr;
	if (Perp)
	{
		// Staged for the camera: nobody bolts at the sight of the officer unless a check asks them to.
		Perp->SetForcedResponse(EFTOArrestResponse::Comply);
	}
	return Perp;
}

void AFTOSmokeTest::AimAt(const FVector& Target)
{
	APlayerController* PC = GetPC();
	if (PC && PC->PlayerCameraManager)
	{
		PC->SetControlRotation((Target - PC->PlayerCameraManager->GetCameraLocation()).Rotation());
	}
}

namespace
{
	/** Which way a building's wall panel faces (out of the building). */
	FVector WallFacing(const UInstancedStaticMeshComponent* ISM, int32 Item)
	{
		FTransform Panel;
		ISM->GetInstanceTransform(Item, Panel, true);
		return Panel.GetRotation().GetForwardVector();
	}
}

FVector AFTOSmokeTest::ClearSpot(const FVector& From, const FVector& Wanted) const
{
	// As far towards Wanted as the camera can get from From without ending up inside something.
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOSmokeView), false, GetPawn());
	FCollisionObjectQueryParams Things(ECC_WorldStatic);
	Things.AddObjectTypesToQuery(ECC_WorldDynamic);
	if (GetWorld()->SweepSingleByObjectType(Hit, From, Wanted, FQuat::Identity, Things, FCollisionShape::MakeSphere(60.f), Params))
	{
		return Hit.Location - (Wanted - From).GetSafeNormal() * 40.f;
	}
	return Wanted;
}

bool AFTOSmokeTest::FindWall(int32 S, int32 Face, int32 Column, int32 Level, UInstancedStaticMeshComponent*& OutISM, int32& OutInstance, FVector& OutAt) const
{
	const AFTOCityGenerator* City = GetCity();
	const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
	if (!City || !Wreckage || !City->GetStructures().IsValidIndex(S))
	{
		return false;
	}
	for (const FFTOStructurePiece& Piece : City->GetStructures()[S].Pieces)
	{
		if (Piece.Role == EFTOPieceRole::Wall && Piece.Face == Face && Piece.Column == Column && Piece.Level == Level && !Wreckage->IsBroken(Piece.Component, Piece.Instance))
		{
			OutISM = City->FindInstanced(Piece.Component);
			OutInstance = Piece.Instance;
			OutAt = Piece.Location;
			return OutISM != nullptr;
		}
	}
	return false;
}

float AFTOSmokeTest::GroundZ(const FVector& At) const
{
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOSmokeGround), false);
	if (const APawn* Me = GetPawn())
	{
		Params.AddIgnoredActor(Me);
	}
	return GetWorld()->LineTraceSingleByObjectType(Ground, At + FVector(0.f, 0.f, 400.f), At - FVector(0.f, 0.f, 600.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params)
		? Ground.ImpactPoint.Z : At.Z;
}

bool AFTOSmokeTest::FindCityInstance(const TCHAR* Mesh, const FVector& Near, UInstancedStaticMeshComponent*& OutISM, int32& OutIndex, FTransform& OutTransform) const
{
	const AFTOCityGenerator* City = GetCity();
	OutISM = City ? City->FindInstanced(Mesh) : nullptr;
	OutIndex = INDEX_NONE;
	float Best = TNumericLimits<float>::Max();
	for (int32 i = 0; OutISM && i < OutISM->GetInstanceCount(); ++i)
	{
		FTransform Instance;
		OutISM->GetInstanceTransform(i, Instance, true);
		const float DistSq = FVector::DistSquared(Instance.GetLocation(), Near);
		// (Broken ones are tucked away far below the street.)
		if (Instance.GetLocation().Z > -10000.f && DistSq < Best)
		{
			Best = DistSq;
			OutIndex = i;
			OutTransform = Instance;
		}
	}
	return OutIndex != INDEX_NONE;
}

AFTOPerp* AFTOSmokeTest::FindNearestPerp(FName Crime) const
{
	const APawn* Cop = GetPawn();
	AFTOPerp* Nearest = nullptr;
	for (TActorIterator<AFTOPerp> It(GetWorld()); It && Cop; ++It)
	{
		if (It->GetIncident() && It->GetIncident()->GetInfo().TemplateId == Crime &&
			(!Nearest || FVector::DistSquared(It->GetActorLocation(), Cop->GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), Cop->GetActorLocation())))
		{
			Nearest = *It;
		}
	}
	return Nearest;
}

void AFTOSmokeTest::ViewArrest(const AActor* Suspect, float Side)
{
	const APawn* Cop = GetPawn();
	if (!Cop || !Suspect)
	{
		return;
	}
	const FVector Mid = (Cop->GetActorLocation() + Suspect->GetActorLocation()) * 0.5f;
	const FVector Along = (Suspect->GetActorLocation() - Cop->GetActorLocation()).GetSafeNormal2D();
	const FVector Across = FVector::CrossProduct(FVector::UpVector, Along) * Side;
	ViewFrom(Mid + Across * 420.f + FVector(0.f, 0.f, 50.f), Mid - FVector(0.f, 0.f, 35.f));
}

void AFTOSmokeTest::AddStep(const TCHAR* Name, float Delay, TFunction<void()> Action)
{
	Steps.Add({ Name, Delay, MoveTemp(Action), nullptr });
}

void AFTOSmokeTest::AddWait(const TCHAR* Name, float MaxSeconds, TFunction<bool()> Until)
{
	Steps.Add({ Name, MaxSeconds, []() {}, MoveTemp(Until) });
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

	// Pacing: a ten-minute shift, and the director putting new crimes a short run from the officer.
	AddStep(TEXT("crimes nearby"), 0.f, [this]()
	{
		AFTOGameMode* GM = GetAuthGameMode();
		const APawn* Officer = GetPawn();
		const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
		if (!GM || !Officer || !GS)
		{
			return;
		}
		UFTOCrimeDirector* Director = GM->GetCrimeDirector();
		const float Chance = Director->NearOfficerChance;
		Director->NearOfficerChance = 1.f;
		int32 Near = 0;
		float Farthest = 0.f;
		for (int32 i = 0; i < 4; ++i)
		{
			if (AFTOIncident* Incident = Director->SpawnIncident(TEXT("LostTourist"), true))
			{
				const float Dist = FVector::Dist2D(Incident->GetActorLocation(), Officer->GetActorLocation());
				Near += Dist >= Director->NearOfficerRange.X && Dist <= Director->NearOfficerRange.Y * 2.f ? 1 : 0;
				Farthest = FMath::Max(Farthest, Dist);
				Incident->Destroy();
			}
		}
		Director->NearOfficerChance = Chance;
		UE_LOG(LogFTO, Display, TEXT("SMOKE: pacing: a %.0f-minute shift (%.0f s left), %d of 4 new crimes a short way from the officer (farthest %.0f m)%s."),
			Director->ShiftLengthSeconds / 60.f, GS->GetShiftTimeRemaining(), Near, Farthest / 100.f,
			FMath::IsNearlyEqual(Director->ShiftLengthSeconds, 600.f) && Near == 4 ? TEXT("") : TEXT(": FAIL"));
	});

	// The shape of the shift: the set piece comes on schedule (this shift's is the heist), then the tour stages its own.
	AddStep(TEXT("set piece on schedule"), 0.5f, [this]()
	{
		if (AFTOGameMode* GM = GetAuthGameMode())
		{
			GM->GetCrimeDirector()->SetPieceAt = 0.f;
		}
	});
	AddStep(TEXT("set piece started"), 0.f, [this]()
	{
		AFTOGameMode* GM = GetAuthGameMode();
		const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
		if (!GM || !GS)
		{
			return;
		}
		UFTOCrimeDirector* Director = GM->GetCrimeDirector();
		Director->SetPieceAt = 2.f; // (no more by themselves)
		// (Whatever it was, out of the way: the tour stages its own. A heist at the bank already means the bomb instead.)
		TArray<AFTOIncident*> Pieces;
		for (AFTOIncident* Incident : GS->GetIncidents())
		{
			const FName Crime = Incident ? Incident->GetInfo().TemplateId : NAME_None;
			if (Incident && Incident->IsActive() && (Crime == TEXT("BankHeist") || Crime == TEXT("Bomb") || Crime == TEXT("Pursuit")))
			{
				Pieces.Add(Incident);
			}
		}
		for (AFTOIncident* Piece : Pieces)
		{
			if (Piece->GetInfo().TemplateId == TEXT("Pursuit") && Piece->GetAttachParentActor())
			{
				Piece->GetAttachParentActor()->Destroy();
			}
			Piece->Destroy();
		}
		UE_LOG(LogFTO, Display, TEXT("SMOKE: set piece on schedule: %s (%d set-piece call(s) open); in turn: %s, %s, %s."),
			GS->GetSetPiece() != NAME_None && Pieces.Num() > 0 ? *FString::Printf(TEXT("the %s started"), *GS->GetSetPiece().ToString()) : TEXT("NO SET PIECE"), Pieces.Num(),
			*UFTOCrimeDirector::SetPieceFor(0).ToString(), *UFTOCrimeDirector::SetPieceFor(1).ToString(), *UFTOCrimeDirector::SetPieceFor(2).ToString());
	});

	// Arrest: across town, catch a shoplifter, cuff them, then bring them home.
	AddStep(TEXT("arrest"), 1.f, [this]()
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
			if (const AFTOIncident* Shoplifting = GM->GetCrimeDirector()->SpawnIncidentAt(TEXT("Shoplifting"), Spot + FVector(150.f, 0.f, -90.f), true))
			{
				if (AFTOPerp* Shoplifter = Shoplifting->GetPerp())
				{
					Shoplifter->SetForcedResponse(EFTOArrestResponse::Comply); // no running off before the cuffs
					TestPerp = Shoplifter;
				}
			}
		}
	});
	AddStep(TEXT("cuff"), 3.6f, [this]()
	{
		// Walk up and cuff them (this one comes quietly; the ones who don't come later in the tour). The server does
		// the arresting (a client's staged nothing here anyway).
		AFTOCharacter* Officer = GetAuthGameMode() ? Cast<AFTOCharacter>(GetPawn()) : nullptr;
		AFTOPerp* Shoplifter = TestPerp.IsValid() && Officer ? TestPerp.Get() : nullptr;
		if (!Shoplifter)
		{
			UE_LOG(LogFTO, Display, TEXT("SMOKE: the staged shoplifter's gone (%s): arresting the nearest crook instead."), TestPerp.IsStale() ? TEXT("destroyed") : TEXT("never staged"));
		}
		const bool bStaged = Shoplifter != nullptr;
		for (TActorIterator<AFTOPerp> It(GetWorld()); It && Officer && !bStaged; ++It)
		{
			if (It->IsCriminal() && (!Shoplifter ||
				FVector::DistSquared(It->GetActorLocation(), Officer->GetActorLocation()) < FVector::DistSquared(Shoplifter->GetActorLocation(), Officer->GetActorLocation())))
			{
				Shoplifter = *It;
			}
		}
		if (Shoplifter)
		{
			Shoplifter->SetForcedResponse(EFTOArrestResponse::Comply);
			const bool bCould = Shoplifter->CanInteract(Officer);
			Shoplifter->Interact(Officer);
			UE_LOG(LogFTO, Display, TEXT("SMOKE: arresting the shoplifter: %s."), Shoplifter->GetArrestState() == EFTOPerpArrest::Cuffing ? TEXT("cuffing") :
				*FString::Printf(TEXT("NOT CUFFING (%s, %.0f cm away, state %d, %s)"), *Shoplifter->GetIncident()->GetInfo().Title.ToString(),
					FVector::Dist(Shoplifter->GetActorLocation(), Officer->GetActorLocation()), int32(Shoplifter->GetArrestState()), bCould ? TEXT("could interact") : TEXT("couldn't interact")));
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

	// The clock runs out: the squad votes for overtime (back on duty with more time on the clock)... The host runs the
	// vote (a client can't; its HUD hands the camera and controls back once the host is on duty again).
	if (GetNetMode() != NM_Client)
	{
		AddStep(TEXT("clock runs out"), 1.2f, [this]()
		{
			if (AFTOGameMode* GM = GetAuthGameMode()) { GM->FTOShiftTimeLeft(0.f); }
		});
		AddShot(TEXT("05b_overtime_vote"), 0.3f);
		AddStep(TEXT("vote for overtime"), 1.f, [this]()
		{
			const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
			UE_LOG(LogFTO, Display, TEXT("SMOKE: clock ran out: %s."), GS && GS->GetShiftPhase() == EFTOShiftPhase::OvertimeVote ? TEXT("the squad's voting") : TEXT("NO VOTE"));
			if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetPC())) { PC->FTOVote(TEXT("Overtime")); }
			// Don't wait on anyone else (a partner who says nothing leaves it to the host).
			if (AFTOGameMode* GM = GetAuthGameMode()) { GM->GetCrimeDirector()->ResolveOvertimeVote(); }
		});
		AddStep(TEXT("overtime"), 0.5f, [this]()
		{
			const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
			UE_LOG(LogFTO, Display, TEXT("SMOKE: overtime vote: %s (overtime %d, %.0f s on the clock)."), GS && GS->GetShiftPhase() == EFTOShiftPhase::OnDuty ? TEXT("back on duty") : TEXT("NOT BACK ON DUTY"),
				GS ? GS->GetOvertimes() : -1, GS ? GS->GetShiftTimeRemaining() : -1.f);
		});
		AddShot(TEXT("05c_overtime"), 0.5f);

		// ...then runs out again and they clock off: the squad lines up outside the precinct and dances while the scoreboard
		// counts up (the arrest and the booking above should have scored).
		AddStep(TEXT("end shift"), 1.2f, [this]()
		{
			if (const AFTOPlayerState* PS = GetPC() ? GetPC()->GetPlayerState<AFTOPlayerState>() : nullptr)
			{
				const FFTOOfficerStats& Stats = PS->GetStats();
				UE_LOG(LogFTO, Display, TEXT("SMOKE: score before the whistle: %d (%d arrests, %d booked, %d caught in the act, best combo x%s)."), Stats.Score, Stats.Arrests,
					Stats.Booked, Stats.CaughtInAct, *FString::SanitizeFloat(FTOScoring::ComboMultiplier(Stats.BestCombo)));
			}
			if (AFTOGameMode* GM = GetAuthGameMode()) { GM->FTOShiftTimeLeft(0.f); }
		});
		AddStep(TEXT("clock off"), 4.4f, [this]()
		{
			if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetPC())) { PC->FTOVote(TEXT("ClockOff")); }
			if (AFTOGameMode* GM = GetAuthGameMode()) { GM->GetCrimeDirector()->ResolveOvertimeVote(); }
		});
		AddShot(TEXT("06_shift_report"), 0.5f);
		AddStep(TEXT("after the debrief"), 0.3f, [this]()
		{
			if (const AFTOCharacter* Officer = Cast<AFTOCharacter>(GetPawn()))
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: debrief: the officer is %s outside the precinct, %s."),
					Officer->GetAnimAction() == EFTOAnimAction::Dance ? TEXT("dancing") : TEXT("NOT dancing"),
					GetCity() && FVector::Dist2D(Officer->GetActorLocation(), GetCity()->FindBuilding(EFTOBuildingType::Precinct)->DoorOutside) < 800.f ? TEXT("lined up") : TEXT("NOT LINED UP"));
			}
			// The tour goes on: hands back on the controls.
			if (APlayerController* PC = GetPC())
			{
				PC->ResetIgnoreInputFlags();
				PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f);
			}
		});
	}

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
			Crook->TalkChoice(Officer, 0); // "Seen anything unusual round here?"
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
			if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>()) { GS->SetShiftPhase(EFTOShiftPhase::OnDuty); if (AFTOGameMode* Mode = GetAuthGameMode()) { Mode->FTOShiftTimeLeft(600.f); } }
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
				GS->SetShiftPhase(EFTOShiftPhase::OnDuty); if (AFTOGameMode* Mode = GetAuthGameMode()) { Mode->FTOShiftTimeLeft(600.f); }
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
			Cop->DiveTackle(); // (a dive, even if someone else is close enough to grab)
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

		// Guns: sign a shotgun out of the armory, then a hold-up in the street: the robber opens fire, the officer
		// fires back, and the robber is subdued and cuffed. Then an officer is shot down and a partner helps them up.
		AddStep(TEXT("armory"), 1.2f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			const AFTOCityGenerator* City = GetCity();
			const FFTOBuilding* Precinct = City ? City->FindBuilding(EFTOBuildingType::Precinct) : nullptr;
			if (!Cop || !Precinct || Precinct->ArmorySpots.Num() < 2)
			{
				return;
			}
			const FTransform& Rack = Precinct->ArmorySpots[1];
			const FVector Fwd = Rack.GetRotation().GetForwardVector();
			const FVector Right = Rack.GetRotation().GetRightVector();
			Cop->TeleportTo(Rack.GetLocation() - Fwd * 60.f + FVector(0.f, 0.f, 100.f), Rack.Rotator());
			for (TActorIterator<AFTOArmoryRack> It(GetWorld()); It; ++It)
			{
				if (It->GetWeapon() == EFTOWeapon::Shotgun)
				{
					It->Interact(Cop);
				}
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: loadout after the armory: %s, %s, %s (in hand: %s)."), *FTOWeapons::DisplayName(Cop->GetWeaponInSlot(0)).ToString(),
				*FTOWeapons::DisplayName(Cop->GetWeaponInSlot(1)).ToString(), *FTOWeapons::DisplayName(Cop->GetWeaponInSlot(2)).ToString(),
				*FTOWeapons::DisplayName(Cop->GetDrawnWeapon()).ToString());
			// Face the rack with the shotgun up, filmed from the side.
			if (APlayerController* PC = GetPC())
			{
				PC->SetControlRotation(Rack.Rotator());
			}
			SetHUDVisible(false);
			const FVector At = Cop->GetActorLocation();
			ViewFrom(At - Right * 260.f + Fwd * 60.f + FVector(0.f, 0.f, 20.f), At + Fwd * 40.f + FVector(0.f, 0.f, 20.f));
		});
		AddShot(TEXT("17a_armory"), 0.3f, false);
		AddStep(TEXT("hold-up"), 0.45f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			AFTOGameMode* GM = GetAuthGameMode();
			AFTOCityGenerator* City = GetCity();
			if (!Cop || !GM || !City)
			{
				return;
			}
			// Down the middle of a street, a robber twelve metres ahead facing the officer.
			const FVector Start = City->GetIntersection(1, 1);
			const FVector Along = (City->GetIntersection(2, 1) - Start).GetSafeNormal();
			const FVector Officer = Start + Along * 900.f;
			const FVector Robber = Start + Along * 2100.f;
			Cop->TeleportTo(Officer + FVector(0.f, 0.f, 100.f), Along.Rotation());
			if (const AFTOIncident* HoldUp = GM->GetCrimeDirector()->SpawnIncidentAt(TEXT("ArmedRobbery"), FTransform((-Along).Rotation(), Robber), INDEX_NONE, true))
			{
				if (AFTOPerp* Gunman = HoldUp->GetPerp())
				{
					Gunman->SetForcedResponse(EFTOArrestResponse::Comply); // stands and shoots rather than running off
				}
			}
			if (APlayerController* PC = GetPC())
			{
				PC->SetViewTargetWithBlend(Cop, 0.f);
				PC->SetControlRotation(((Robber + FVector(0.f, 0.f, 110.f)) - (Officer + FVector(0.f, 0.f, 160.f))).Rotation());
			}
			SetHUDVisible(true);
			if (Cop->GetDrawnWeapon() == EFTOWeapon::None)
			{
				Cop->SelectSlot(1);
			}
		});
		AddStep(TEXT("robber close-up"), 0.12f, [this]()
		{
			// The robber, pistol levelled, seen from the pavement (before the officer gets a shot off).
			if (const AFTOPerp* Robber = FindNearestPerp(TEXT("ArmedRobbery")))
			{
				const FVector At = Robber->GetActorLocation();
				ViewFrom(At + Robber->GetActorForwardVector() * 220.f + Robber->GetActorRightVector() * 260.f + FVector(0.f, 0.f, 40.f), At + FVector(0.f, 0.f, 20.f));
			}
		});
		AddShot(TEXT("17b_armed_robber"), 0.1f);
		// Aim through the over-the-shoulder camera (as a player would put the crosshair on them), not from the
		// officer's eyes, which would leave the crosshair a shoulder's width off to the side.
		auto AimAtRobber = [this]()
		{
			if (const AFTOPerp* Robber = FindNearestPerp(TEXT("ArmedRobbery")))
			{
				AimAt(Robber->GetActorLocation() + FVector(0.f, 0.f, 25.f));
			}
		};
		AddStep(TEXT("officer's view"), 0.1f, [this]()
		{
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		});
		// (Once the view is the officer's own camera again, and they're on their feet: a burning car somewhere may
		// have gone up beside them.)
		AddWait(TEXT("officer steady"), 8.f, [this]()
		{
			const AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			return !Cop || Cop->IsReadyForAction();
		});
		AddStep(TEXT("aim"), 0.05f, AimAtRobber);
		AddShot(TEXT("17b2_officer_aims"), 0.05f);
		AddStep(TEXT("return fire"), 0.f, [this, AimAtRobber]()
		{
			if (AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn()))
			{
				AimAtRobber();
				Cop->FirePressed();
			}
		});
		AddShot(TEXT("17c_return_fire"), 2.8f);
		AddStep(TEXT("fire result"), 0.f, [this]()
		{
			// The robber in the street ahead (there may be others about town).
			const AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			TestPerp = FindNearestPerp(TEXT("ArmedRobbery"));
			const bool bFloored = TestPerp.IsValid() && TestPerp->GetArrestState() == EFTOPerpArrest::Surrendered;
			UE_LOG(LogFTO, Display, TEXT("SMOKE: returned fire: %s; the officer is %s, with %d in the shotgun."), bFloored ? TEXT("the robber went down, subdued") : TEXT("NOT SUBDUED"),
				Cop && Cop->IsDowned() ? TEXT("down") : TEXT("on their feet"), Cop ? Cop->GetClip(1) : -1);
		});
		AddStep(TEXT("cuff the robber"), 1.3f, [this]()
		{
			// Walk over and cuff them where they lie: hauled up onto their knees, the cuffs go on from behind.
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop || !TestPerp.IsValid())
			{
				return;
			}
			const FVector Body = TestPerp->GetKnockdown()->GetBodyLocation();
			const FVector From = (Cop->GetActorLocation() - Body).GetSafeNormal2D();
			Cop->TeleportTo(FVector(Body.X, Body.Y, Cop->GetActorLocation().Z) + From * 170.f, (-From).Rotation());
			TestPerp->Interact(Cop);
			ViewArrest(TestPerp.Get());
			UE_LOG(LogFTO, Display, TEXT("SMOKE: cuffing the robber: %s."), TestPerp->GetArrestState() == EFTOPerpArrest::Cuffing ? TEXT("on their knees, cuffs going on") : TEXT("NOT CUFFING"));
		});
		AddShot(TEXT("17d_cuffing"), 1.5f);
		AddStep(TEXT("robber cuffed"), 0.f, [this]()
		{
			int32 Cuffed = 0;
			for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
			{
				Cuffed += It->GetCrime().ToString().Contains(TEXT("Robbery")) ? 1 : 0;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: the robber %s."), Cuffed > 0 ? TEXT("is cuffed and following") : TEXT("WAS NOT ARRESTED"));
		});
		AddStep(TEXT("officer down"), 1.5f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop)
			{
				return;
			}
			// A stray round from nowhere in particular, then a partner arrives to help.
			FTOImpact::Shot(Cop, -Cop->GetActorForwardVector() * 30000.f, EFTOWeapon::Pistol, nullptr);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			TestPassenger = GetWorld()->SpawnActor<AFTOCharacter>(AFTOCharacter::StaticClass(), Cop->GetActorLocation() + Cop->GetActorRightVector() * 200.f, Cop->GetActorRotation(), Params);
			ViewFrom(Cop->GetActorLocation() + Cop->GetActorRightVector() * 500.f + FVector(0.f, 0.f, 250.f), Cop->GetActorLocation() - FVector(0.f, 0.f, 50.f));
			UE_LOG(LogFTO, Display, TEXT("SMOKE: officer shot: %s."), Cop->IsDowned() ? TEXT("down") : TEXT("STILL STANDING"));
		});
		AddShot(TEXT("17e_officer_down"), 0.2f);
		AddStep(TEXT("help up"), 2.2f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (Cop && TestPassenger)
			{
				const bool bCould = Cop->CanInteract(TestPassenger);
				Cop->Interact(TestPassenger);
				UE_LOG(LogFTO, Display, TEXT("SMOKE: partner %s: the officer is %s."), bCould ? TEXT("helped them up") : TEXT("COULDN'T HELP"), Cop->IsDowned() ? TEXT("STILL DOWN") : TEXT("back up"));
			}
		});
		AddShot(TEXT("17f_helped_up"), 0.3f);
		AddStep(TEXT("tidy up"), 0.3f, [this]()
		{
			if (TestPassenger)
			{
				TestPassenger->Destroy();
				TestPassenger = nullptr;
			}
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		});

		// Arrests that don't go quietly: a brawler who fights back and is wrestled down (mashing), a vandal who wins
		// the struggle, shoves the officer over and runs (and is tackled), and a getaway driver who gives up beside
		// their car. Each ends in the cuffs.
		AddStep(TEXT("struggle"), 0.6f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			AFTOCityGenerator* City = GetCity();
			if (!Cop || !City)
			{
				return;
			}
			if (AFTOGameMode* GM = GetAuthGameMode())
			{
				GM->FTOAddChaos(-100.f);
			}
			if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
			{
				GS->SetShiftPhase(EFTOShiftPhase::OnDuty); if (AFTOGameMode* Mode = GetAuthGameMode()) { Mode->FTOShiftTimeLeft(600.f); }
			}
			// Down the hold-up's street again, a brawler two steps ahead.
			const FVector Start = City->GetIntersection(1, 1);
			const FVector Along = (City->GetIntersection(2, 1) - Start).GetSafeNormal();
			Cop->TeleportTo(Start + Along * 500.f + FVector(0.f, 0.f, 100.f), Along.Rotation());
			TestPerp = StagePerp(TEXT("BarFight"), 200.f);
			if (TestPerp.IsValid())
			{
				TestPerp->SetForcedResponse(EFTOArrestResponse::Struggle);
				TestPerp->Interact(Cop);
				ViewArrest(TestPerp.Get());
				UE_LOG(LogFTO, Display, TEXT("SMOKE: arresting a brawler: %s."), TestPerp->GetArrestState() == EFTOPerpArrest::Struggling ? TEXT("they're fighting back") : TEXT("NO STRUGGLE"));
			}
		});
		AddShot(TEXT("18a_struggle"), 0.3f);
		AddStep(TEXT("mash"), 1.2f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop || !TestPerp.IsValid())
			{
				return;
			}
			const float Before = TestPerp->GetStruggleMeter();
			for (int32 Press = 0; Press < 10 && TestPerp->GetArrestState() == EFTOPerpArrest::Struggling; ++Press)
			{
				TestPerp->Mash(Cop);
			}
			ViewArrest(TestPerp.Get());
			UE_LOG(LogFTO, Display, TEXT("SMOKE: mashed (meter was %.2f): %s."), Before,
				TestPerp->GetArrestState() == EFTOPerpArrest::Cuffing ? TEXT("wrestled them down, cuffs going on") : TEXT("STILL STRUGGLING"));
		});
		AddShot(TEXT("18b_cuffing"), 1.6f);
		AddStep(TEXT("escort the brawler"), 1.6f, [this]()
		{
			int32 Cuffed = 0;
			for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
			{
				Cuffed += It->GetCrime().ToString().Contains(TEXT("Bar Fight")) ? 1 : 0;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: the brawler %s."), Cuffed > 0 ? TEXT("is cuffed") : TEXT("WAS NOT ARRESTED"));
			// A few steps back up the street, the arrestee trotting after with their hands cuffed behind them.
			if (APawn* Cop = GetPawn())
			{
				WalkDirection = -Cop->GetActorForwardVector().GetSafeNormal2D();
				bWalkOfficer = true;
			}
		});
		AddStep(TEXT("film the escort"), 0.05f, [this]()
		{
			// Side on to the officer and whoever's following.
			const APawn* Cop = GetPawn();
			const AFTOArrestee* Follower = nullptr;
			for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
			{
				if (It->GetCrime().ToString().Contains(TEXT("Bar Fight")))
				{
					Follower = *It;
				}
			}
			if (Cop && Follower)
			{
				const FVector Right = FVector::CrossProduct(FVector::UpVector, WalkDirection);
				const FVector Mid = (Cop->GetActorLocation() + Follower->GetActorLocation()) * 0.5f + WalkDirection * 60.f;
				ViewFrom(Mid + Right * 460.f + FVector(0.f, 0.f, 30.f), Mid - FVector(0.f, 0.f, 25.f));
			}
		});
		AddShot(TEXT("18c_escorting"), 0.2f);
		AddStep(TEXT("stop"), 0.3f, [this]() { bWalkOfficer = false; });

		AddStep(TEXT("lost struggle"), 2.6f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop)
			{
				return;
			}
			// Nobody mashes this time.
			TestPerp = StagePerp(TEXT("Vandalism"), 200.f);
			if (TestPerp.IsValid())
			{
				TestPerp->SetForcedResponse(EFTOArrestResponse::Struggle);
				TestPerp->Interact(Cop);
				ViewArrest(TestPerp.Get(), -1.f);
			}
		});
		AddStep(TEXT("broke free"), 0.35f, [this]()
		{
			const AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop || !TestPerp.IsValid())
			{
				return;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: lost the struggle: the officer's %s and the vandal %s."),
				Cop->GetKnockdown()->IsDown() ? TEXT("been shoved over") : TEXT("STILL STANDING"), TestPerp->IsFleeing() ? TEXT("is running for it") : TEXT("ISN'T RUNNING"));
			// From behind and above the runner.
			const FVector Runner = TestPerp->GetActorLocation();
			const FVector Dir = TestPerp->GetMoveDirection().GetSafeNormal2D();
			ViewFrom(Runner - Dir * 650.f + FVector(0.f, 0.f, 380.f), Runner + Dir * 250.f);
		});
		AddShot(TEXT("18d_on_the_run"), 2.2f);
		AddStep(TEXT("tackle the runner"), 0.7f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop || !TestPerp.IsValid())
			{
				return;
			}
			// Back up, and after them (the sprint the test skips): right behind them, and dive.
			const FVector Dir = TestPerp->GetMoveDirection().GetSafeNormal2D();
			const FVector Runner = TestPerp->GetActorLocation();
			Cop->TeleportTo(Runner - Dir * 140.f + FVector(0.f, 0.f, 96.f - AFTOPedestrian::HalfHeight + 2.f), Dir.Rotation());
			Cop->DiveTackle(); // (as from a sprint)
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Dir);
			ViewFrom(Runner + Dir * 150.f + Right * 560.f + FVector(0.f, 0.f, 120.f), Runner + Dir * 120.f);
		});
		AddShot(TEXT("18e_tackled"), 0.6f);
		AddStep(TEXT("cuff the runner"), 2.9f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop || !TestPerp.IsValid())
			{
				return;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: tackled the runner: %s."), TestPerp->GetArrestState() == EFTOPerpArrest::Surrendered ? TEXT("down, subdued") : TEXT("GOT AWAY"));
			TestPerp->Interact(Cop);
			ViewArrest(TestPerp.Get());
		});
		AddStep(TEXT("runner cuffed"), 0.2f, [this]()
		{
			int32 Cuffed = 0;
			for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
			{
				Cuffed += It->GetCrime().ToString().Contains(TEXT("Vandalism")) ? 1 : 0;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: the runner %s."), Cuffed > 0 ? TEXT("is cuffed") : TEXT("WAS NOT ARRESTED"));
		});

		AddStep(TEXT("getaway"), 1.2f, [this]()
		{
			// The nearest car on the road floors it...
			const APawn* Cop = GetPawn();
			AFTOTrafficCar* Car = nullptr;
			for (TActorIterator<AFTOTrafficCar> It(GetWorld()); It && Cop; ++It)
			{
				if (It->GetCarState() == EFTOCarState::Driving &&
					(!Car || FVector::DistSquared(It->GetActorLocation(), Cop->GetActorLocation()) < FVector::DistSquared(Car->GetActorLocation(), Cop->GetActorLocation())))
				{
					Car = *It;
				}
			}
			if (Car)
			{
				Car->MakeGetaway();
				TestCar = Car;
			}
		});
		AddStep(TEXT("run to ground"), 0.5f, [this]()
		{
			// ...and a chase later, it's over: the driver climbs out beside the car and gives up.
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			AFTOIncident* Chase = TestCar.IsValid() ? TestCar->GetChaseIncident() : nullptr;
			if (!Cop || !Chase)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: getaway: NO CHASE."));
				return;
			}
			Chase->TalkedDown();
			TestPerp = Chase->GetPerp();
			if (!TestPerp.IsValid())
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: getaway: NO DRIVER."));
				return;
			}
			const FVector Driver = TestPerp->GetActorLocation();
			const FVector Out = (Driver - TestCar->GetActorLocation()).GetSafeNormal2D();
			const FVector Along = TestCar->GetActorForwardVector().GetSafeNormal2D();
			Cop->TeleportTo(Driver + Out * 250.f + Along * 120.f + FVector(0.f, 0.f, 6.f), (-Out).Rotation());
			ViewFrom(Driver + Out * 520.f - Along * 380.f + FVector(0.f, 0.f, 200.f), Driver - Out * 60.f);
			UE_LOG(LogFTO, Display, TEXT("SMOKE: getaway driver: %s, the car %s."),
				TestPerp->GetArrestState() == EFTOPerpArrest::Surrendered ? TEXT("out of the car and on their knees") : TEXT("NOT SURRENDERED"),
				TestCar->GetCarState() == EFTOCarState::Busted ? TEXT("busted") : TEXT("STILL GOING"));
		});
		AddShot(TEXT("18f_busted_driver"), 0.4f);
		AddStep(TEXT("cuff the driver"), 2.9f, [this]()
		{
			if (AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn()); Cop && TestPerp.IsValid())
			{
				TestPerp->Interact(Cop);
			}
		});
		AddStep(TEXT("driver cuffed"), 0.3f, [this]()
		{
			int32 Cuffed = 0;
			for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
			{
				Cuffed += It->GetCrime().ToString().Contains(TEXT("Car Chase")) ? 1 : 0;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: the getaway driver %s."), Cuffed > 0 ? TEXT("is cuffed") : TEXT("WAS NOT ARRESTED"));
		});
		AddShot(TEXT("18g_driver_cuffed"), 0.3f);
		AddStep(TEXT("back to the officer"), 0.3f, [this]()
		{
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		});

		// Crimes that play out: a mugging with its victim, the mugger slipping off into the crowd, and a search.
		AddStep(TEXT("mugging"), 1.5f, [this]()
		{
			TestPerp = StagePerp(TEXT("Mugging"), 600.f);
			if (!TestPerp.IsValid())
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: mugging: NO MUGGER."));
				return;
			}
			TestPerp->SetForcedResponse(EFTOArrestResponse::Comply); // no bolting at the sight of the camera
			int32 Victims = 0;
			for (TActorIterator<AFTOCrimeExtra> It(GetWorld()); It; ++It)
			{
				Victims += It->GetRole() == EFTOExtraRole::Victim && FVector::Dist2D(It->GetActorLocation(), TestPerp->GetActorLocation()) < 300.f ? 1 : 0;
			}
			const FVector At = TestPerp->GetActorLocation();
			ViewFrom(At + TestPerp->GetActorRightVector() * 420.f + TestPerp->GetActorForwardVector() * 60.f + FVector(0.f, 0.f, 80.f), At + TestPerp->GetActorForwardVector() * 60.f);
			UE_LOG(LogFTO, Display, TEXT("SMOKE: mugging: %s, the mugger %s."), Victims > 0 ? TEXT("a victim with their hands up") : TEXT("NO VICTIM"),
				TestPerp->GetAnimAction() == EFTOAnimAction::Point ? TEXT("demanding their wallet") : TEXT("NOT AT IT"));
		});
		AddShot(TEXT("21a_mugging"), 0.3f);
		AddStep(TEXT("mugger slips away"), 4.f, [this]()
		{
			if (!TestPerp.IsValid())
			{
				return;
			}
			TestPerp->FinishDeedNow();
			const AFTOIncident* Incident = TestPerp->GetIncident();
			UE_LOG(LogFTO, Display, TEXT("SMOKE: the mugger %s; the call %s (\"%s\")."), TestPerp->IsHiding() ? TEXT("walked off into the crowd") : TEXT("DIDN'T LEAVE"),
				Incident && Incident->IsSearching() ? TEXT("is a search") : TEXT("IS NOT A SEARCH"), Incident ? *Incident->GetInfo().SuspectDescription.ToString() : TEXT(""));
		});
		AddStep(TEXT("film the getaway"), 0.1f, [this]()
		{
			if (TestPerp.IsValid())
			{
				const FVector At = TestPerp->GetActorLocation();
				ViewFrom(At + FVector(-500.f, -500.f, 400.f), At);
			}
		});
		AddShot(TEXT("21b_slipped_away"), 0.3f);
		AddStep(TEXT("track them down"), 3.4f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop || !TestPerp.IsValid())
			{
				return;
			}
			const FVector At = TestPerp->GetActorLocation();
			const FVector Back = -TestPerp->GetActorForwardVector().GetSafeNormal2D();
			Cop->TeleportTo(At + Back * 150.f + FVector(0.f, 0.f, 6.f), (-Back).Rotation());
			const bool bCould = TestPerp->CanInteract(Cop);
			const FString Prompt = TestPerp->GetInteractPrompt(Cop).ToString();
			// Stop them for a word, search them (the goods turn up), and arrest them.
			TestPerp->Interact(Cop);
			const bool bTalking = Cop->GetTalkingTo() == TestPerp.Get();
			TestPerp->TalkChoice(Cop, 2);
			const FString Found = TestPerp->GetFound();
			TestPerp->TalkChoice(Cop, 2);
			UE_LOG(LogFTO, Display, TEXT("SMOKE: found the mugger (prompt \"%s\", %s, the search turned up \"%s\"): %s."), *Prompt,
				bTalking ? TEXT("talking") : TEXT("NO CONVERSATION"), *Found,
				bCould && TestPerp->GetArrestState() == EFTOPerpArrest::Cuffing ? TEXT("cuffing them") : TEXT("NOT ARRESTED"));
		});
		AddStep(TEXT("mugger cuffed"), 0.2f, [this]()
		{
			int32 Cuffed = 0;
			for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
			{
				Cuffed += It->GetCrime().ToString().Contains(TEXT("Mugging")) ? 1 : 0;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: the mugger %s."), Cuffed > 0 ? TEXT("is cuffed") : TEXT("WAS NOT ARRESTED"));
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		});

		// Every crime its twist. A pickpocket hiding in a crowd of look-alikes, picked out by the description.
		AddStep(TEXT("pickpocket"), 2.f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			TestPerp = StagePerp(TEXT("PettyTheft"), 900.f);
			if (!Cop || !TestPerp.IsValid())
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: pickpocket: NO PICKPOCKET."));
				return;
			}
			const AFTOIncident* Incident = TestPerp->GetIncident();
			int32 Crowd = 0;
			int32 LookAlikes = 0;
			int32 SameLook = 0;
			const FString Wanted = TestPerp->DescribeLook();
			for (TActorIterator<AFTOCrimeExtra> It(GetWorld()); It; ++It)
			{
				if (It->GetRole() == EFTOExtraRole::Bystander && FVector::Dist2D(It->GetActorLocation(), TestPerp->GetActorLocation()) < 900.f)
				{
					++Crowd;
					const FString Look = It->DescribeLook();
					SameLook += Look == Wanted ? 1 : 0;
					LookAlikes += Look != Wanted && (Look.Left(Look.Find(TEXT(","))) == Wanted.Left(Wanted.Find(TEXT(","))) || Look.Mid(Look.Find(TEXT(","))) == Wanted.Mid(Wanted.Find(TEXT(",")))) ? 1 : 0;
				}
			}
			// Standing right there talks nobody down.
			Cop->TeleportTo(Incident->GetActorLocation() + FVector(-200.f, 0.f, 100.f), Cop->GetActorRotation());
			UE_LOG(LogFTO, Display, TEXT("SMOKE: pickpocket: %s with %d bystanders (%d look alike, %d identical), the pickpocket %s; looking for \"%s\"."),
				Incident && Incident->IsCrowd() ? TEXT("in a crowd") : TEXT("NOT IN A CROWD"), Crowd, LookAlikes, SameLook,
				TestPerp->IsHiding() ? TEXT("blending in") : TEXT("NOT HIDING"), *Incident->GetInfo().SuspectDescription.ToString());
			const FVector At = Incident->GetActorLocation();
			ViewFrom(At + FVector(-700.f, -500.f, 450.f), At + FVector(0.f, 0.f, 80.f));
		});
		AddShot(TEXT("21c_pickpocket_crowd"), 0.3f);
		AddStep(TEXT("pick out the pickpocket"), 3.4f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop || !TestPerp.IsValid())
			{
				return;
			}
			const AFTOIncident* Incident = TestPerp->GetIncident();
			const float Progress = Incident ? Incident->GetProgress() : -1.f;
			// The wrong one first: a bystander's clean.
			FString WrongFound = TEXT("(nobody)");
			for (TActorIterator<AFTOCrimeExtra> It(GetWorld()); It; ++It)
			{
				if (It->GetRole() == EFTOExtraRole::Bystander && FVector::Dist2D(It->GetActorLocation(), TestPerp->GetActorLocation()) < 900.f)
				{
					Cop->TeleportTo(It->GetActorLocation() + It->GetActorForwardVector() * 140.f + FVector(0.f, 0.f, 6.f), (-It->GetActorForwardVector()).Rotation());
					It->Interact(Cop);
					It->TalkChoice(Cop, 2);
					if (Cop->IsInSyncedAction())
					{
						Cop->EndSyncedAction();
					}
					WrongFound = It->GetFound().IsEmpty() ? TEXT("clean") : It->GetFound();
					It->TalkChoice(Cop, 3);
					break;
				}
			}
			// Then the one who matches: searched, the wallets turn up, and they're arrested.
			Cop->TeleportTo(TestPerp->GetActorLocation() + TestPerp->GetActorForwardVector() * 140.f + FVector(0.f, 0.f, 6.f), (-TestPerp->GetActorForwardVector()).Rotation());
			const FString Title = TestPerp->GetTalkTitle().ToString();
			TestPerp->Interact(Cop);
			TestPerp->TalkChoice(Cop, 2);
			if (Cop->IsInSyncedAction())
			{
				Cop->EndSyncedAction();
			}
			const FString Found = TestPerp->GetFound();
			TestPerp->TalkChoice(Cop, 2);
			UE_LOG(LogFTO, Display, TEXT("SMOKE: pickpocket: progress standing in the crowd %.2f, a bystander searched (%s), the one matching (\"%s\") had \"%s\": %s."),
				Progress, *WrongFound, *Title, *Found, TestPerp->GetArrestState() == EFTOPerpArrest::Cuffing ? TEXT("cuffing them") : TEXT("NOT ARRESTED"));
		});
		AddStep(TEXT("pickpocket cuffed"), 0.2f, [this]()
		{
			int32 Cuffed = 0;
			for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
			{
				Cuffed += It->GetCrime().ToString().Contains(TEXT("Pickpocket")) ? 1 : 0;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: the pickpocket %s."), Cuffed > 0 ? TEXT("is cuffed") : TEXT("WAS NOT ARRESTED"));
		});

		// A burglar hiding somewhere in the building, upstairs if there is one: found, and they give up.
		AddStep(TEXT("burglar hides"), 0.5f, [this]()
		{
			AFTOGameMode* GM = GetAuthGameMode();
			TestPerp.Reset();
			int32 Tries = 0;
			for (; Tries < 8 && GM; ++Tries)
			{
				AFTOIncident* Incident = GM->GetCrimeDirector()->SpawnIncident(TEXT("Burglary"), true);
				AFTOPerp* Perp = Incident ? Incident->GetPerp() : nullptr;
				if (Perp && Perp->IsHidingInBuilding() && (Perp->IsHidingUpstairs() || Tries == 7))
				{
					TestPerp = Perp;
					break;
				}
				if (Incident)
				{
					Incident->Destroy();
				}
			}
			if (!TestPerp.IsValid())
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: burglar: NO HIDDEN BURGLAR."));
				return;
			}
			TestPerp->SetForcedResponse(EFTOArrestResponse::Comply);
			const AFTOIncident* Incident = TestPerp->GetIncident();
			UE_LOG(LogFTO, Display, TEXT("SMOKE: burglar: hiding %s, %.0f m from the call and %.0f m up (%d tries); the board says \"%s\"."),
				TestPerp->IsHidingUpstairs() ? TEXT("upstairs") : TEXT("downstairs"), FVector::Dist2D(TestPerp->GetActorLocation(), Incident->GetActorLocation()) / 100.f,
				(TestPerp->GetActorLocation().Z - Incident->GetActorLocation().Z) / 100.f, Tries + 1, *Incident->GetTwistHint());
			const FVector At = TestPerp->GetActorLocation();
			ViewFrom(ClearSpot(At, At + TestPerp->GetActorForwardVector() * 300.f + FVector(0.f, 0.f, 80.f)), At);
		});
		AddShot(TEXT("21d_burglar_hiding"), 0.3f);
		AddStep(TEXT("search the building"), 0.f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop || !TestPerp.IsValid())
			{
				return;
			}
			// The officer comes up to the floor and gets a look at them.
			const FVector At = TestPerp->GetActorLocation();
			const FVector Spot = ClearSpot(At, At + TestPerp->GetActorForwardVector() * 320.f);
			Cop->TeleportTo(Spot + FVector(0.f, 0.f, 6.f), (At - Spot).Rotation());
		});
		AddWait(TEXT("burglar found"), 4.f, [this]() { return !TestPerp.IsValid() || !TestPerp->IsHidingInBuilding(); });
		AddStep(TEXT("cuff the burglar"), 3.2f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop || !TestPerp.IsValid())
			{
				return;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: burglar: %s (%s)."), TestPerp->IsHidingInBuilding() ? TEXT("NOT FOUND") : TEXT("found"),
				TestPerp->GetArrestState() == EFTOPerpArrest::Surrendered ? TEXT("they've given up") : TEXT("NOT GIVING UP"));
			Cop->TeleportTo(TestPerp->GetActorLocation() - TestPerp->GetActorForwardVector() * 120.f + FVector(0.f, 0.f, 6.f), TestPerp->GetActorRotation());
			TestPerp->Interact(Cop);
		});
		AddStep(TEXT("burglar cuffed"), 0.2f, [this]()
		{
			int32 Cuffed = 0;
			for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
			{
				Cuffed += It->GetCrime().ToString().Contains(TEXT("Burglary")) ? 1 : 0;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: the burglar %s."), Cuffed > 0 ? TEXT("is cuffed") : TEXT("WAS NOT ARRESTED"));
		});

		// A drunk, talked round: one answer that winds them up, then the friendly ones.
		AddStep(TEXT("drunk"), 0.5f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			// (Back down to the street first.)
			if (AFTOCityGenerator* City = GetCity(); Cop && City)
			{
				const FVector Corner = City->GetSidewalkCorner(1, 1, 0);
				Cop->TeleportTo(Corner + FVector(0.f, 0.f, 100.f), Cop->GetActorRotation());
			}
			TestPerp = StagePerp(TEXT("Drunk"), 220.f);
			if (!Cop || !TestPerp.IsValid())
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: drunk: NO DRUNK."));
				return;
			}
			TestPerp->Interact(Cop);
			TArray<FText> Options;
			TestPerp->GetTalkOptions(Cop, Options);
			const FString Title = TestPerp->GetTalkTitle().ToString();
			const int32 Wrong = (TestPerp->GetDrunkRightAnswer() + 1) % 3;
			TestPerp->TalkChoice(Cop, Wrong);
			UE_LOG(LogFTO, Display, TEXT("SMOKE: drunk: %s (\"%s\", %d options); a wrong answer: temper %d."), Cop->GetTalkingTo() == TestPerp.Get() ? TEXT("talking") : TEXT("NO CONVERSATION"),
				*Title, Options.Num(), TestPerp->GetDrunkTemper());
			const FVector At = TestPerp->GetActorLocation();
			ViewFrom(At + TestPerp->GetActorRightVector() * 380.f + FVector(0.f, 0.f, 60.f), At);
		});
		AddShot(TEXT("21f_drunk"), 0.3f);
		AddStep(TEXT("talk them round"), 1.f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop || !TestPerp.IsValid())
			{
				return;
			}
			AFTOIncident* Incident = TestPerp->GetIncident();
			for (int32 i = 0; i < AFTOPerp::DrunkStages && TestPerp->GetDrunkStage() < AFTOPerp::DrunkStages; ++i)
			{
				TestPerp->TalkChoice(Cop, TestPerp->GetDrunkRightAnswer());
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: drunk: talked round (%d of %d): %s."), TestPerp->GetDrunkStage(), AFTOPerp::DrunkStages,
				Incident && Incident->GetState() == EFTOIncidentState::Resolved ? TEXT("handled, waving us off") : TEXT("NOT HANDLED"));
		});

		// A brawl: one officer can't pull them apart (it takes two, or fists).
		AddStep(TEXT("brawl needs two"), 2.5f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			TestPerp = StagePerp(TEXT("BarFight"), 300.f);
			if (!Cop || !TestPerp.IsValid())
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: brawl needs two: NO BRAWL."));
				return;
			}
		});
		AddStep(TEXT("brawl with one officer"), 0.f, [this]()
		{
			if (!TestPerp.IsValid())
			{
				return;
			}
			const AFTOIncident* Incident = TestPerp->GetIncident();
			int32 Brawling = 0;
			for (TActorIterator<AFTOCrimeExtra> It(GetWorld()); It; ++It)
			{
				Brawling += It->GetRole() == EFTOExtraRole::Brawler && It->GetAnimAction() == EFTOAnimAction::FightIdle ? 1 : 0;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: brawl needs two: one officer on scene (%d), progress %.2f, %s (%d brawler(s) still at it)."), Incident->GetOfficersOnScene(), Incident->GetProgress(),
				Incident->GetProgress() <= 0.f && Brawling > 0 ? TEXT("they keep fighting") : TEXT("ONE OFFICER BROKE IT UP"), Brawling);
			if (AFTOIncident* Live = TestPerp->GetIncident())
			{
				Live->Destroy();
			}
			TestPerp.Reset();
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		});

		// Set pieces. The bomb: a wrong wire costs time, the right three defuse it.
		AddStep(TEXT("bomb"), 0.5f, [this]()
		{
			AFTOGameMode* GM = GetAuthGameMode();
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			AFTOIncident* Incident = GM ? GM->FTOSetPieceNow(TEXT("Bomb")) : nullptr;
			AFTOBomb* Bomb = nullptr;
			for (TActorIterator<AFTOBomb> It(GetWorld()); It; ++It)
			{
				Bomb = *It;
			}
			if (!Cop || !Incident || !Bomb)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: bomb: NO BOMB."));
				return;
			}
			const FVector Front = Bomb->GetActorLocation() + Bomb->GetActorForwardVector() * 150.f;
			Cop->TeleportTo(Front + FVector(0.f, 0.f, 100.f), (Bomb->GetActorLocation() - Front).Rotation());
			Bomb->Interact(Cop);
			const float Before = Bomb->GetTimeLeft();
			const FString Title = Bomb->GetTalkTitle().ToString();
			// Every wrong wire once (some of them are needed later: a wrong cut mustn't spoil them).
			const int32 Next = Bomb->GetNextWire();
			int32 Wrong = 0;
			for (int32 Wire = 0; Wire < AFTOBomb::NumWires; ++Wire)
			{
				if (Wire != Next)
				{
					Bomb->TalkChoice(Cop, Wire);
					++Wrong;
				}
			}
			const float After = Bomb->GetTimeLeft();
			UE_LOG(LogFTO, Display, TEXT("SMOKE: bomb: %s (\"%s\"); %d wrong wires took %.0f s off."), Cop->GetTalkingTo() == Bomb ? TEXT("at the wires") : TEXT("NOT AT THE WIRES"), *Title, Wrong, Before - After);
			const FVector At = Bomb->GetActorLocation();
			ViewFrom(At + Bomb->GetActorRightVector() * 260.f + Bomb->GetActorForwardVector() * 200.f + FVector(0.f, 0.f, 160.f), At);
		});
		AddShot(TEXT("21g_bomb"), 0.3f);
		AddStep(TEXT("defuse"), 0.6f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			AFTOBomb* Bomb = nullptr;
			for (TActorIterator<AFTOBomb> It(GetWorld()); It; ++It)
			{
				Bomb = *It;
			}
			if (!Cop || !Bomb)
			{
				return;
			}
			for (int32 i = 0; i < AFTOBomb::WiresToCut && !Bomb->IsDefused(); ++i)
			{
				Bomb->TalkChoice(Cop, Bomb->GetNextWire());
			}
			int32 Handled = 0;
			for (const AFTOIncident* Incident : GetWorld()->GetGameState<AFTOGameState>()->GetIncidents())
			{
				Handled += Incident && Incident->GetInfo().TemplateId == TEXT("Bomb") && Incident->GetState() == EFTOIncidentState::Resolved ? 1 : 0;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: bomb: %s, %d s to spare (the call %s)."), Bomb->IsDefused() ? TEXT("defused") : TEXT("NOT DEFUSED"),
				FMath::RoundToInt(Bomb->GetTimeLeft()), Handled > 0 ? TEXT("handled") : TEXT("NOT HANDLED"));
		});
		AddShot(TEXT("21h_bomb_defused"), 0.3f);
		// A second one is left to tick down: it goes off.
		AddStep(TEXT("bomb goes off"), 2.f, [this]()
		{
			AFTOGameMode* GM = GetAuthGameMode();
			TestCar.Reset();
			for (TActorIterator<AFTOBomb> It(GetWorld()); It; ++It)
			{
				It->Destroy(); // (the defused one, out of the way)
			}
			AFTOIncident* Incident = GM ? GM->FTOSetPieceNow(TEXT("Bomb")) : nullptr;
			for (TActorIterator<AFTOBomb> It(GetWorld()); It; ++It)
			{
				if (!It->IsActorBeingDestroyed())
				{
					// (Out at the edge of town, well away from the rest of the tour.)
					if (const AFTOCityGenerator* City = GetCity())
					{
						It->SetActorLocation(City->GetSidewalkCorner(0, 0, 0));
					}
					It->SetTimeLeft(0.5f);
					const FVector At = It->GetActorLocation();
					ViewFrom(At + FVector(-1200.f, -900.f, 700.f), At);
				}
			}
			ChaosBefore = GetWorld()->GetGameState<AFTOGameState>()->GetChaos();
			if (!Incident)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: bomb goes off: NO BOMB."));
			}
		});
		AddShot(TEXT("21i_bomb_boom"), 0.2f);
		AddStep(TEXT("bomb result"), 0.f, [this]()
		{
			int32 Exploded = 0;
			for (TActorIterator<AFTOBomb> It(GetWorld()); It; ++It)
			{
				Exploded += It->HasExploded() ? 1 : 0;
			}
			const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
			UE_LOG(LogFTO, Display, TEXT("SMOKE: bomb left alone: %s (chaos +%.0f)."), Exploded > 0 ? TEXT("it went off") : TEXT("NOTHING HAPPENED"), GS->GetChaos() - ChaosBefore);
			if (AFTOGameMode* GM = GetAuthGameMode()) { GM->FTOAddChaos(-100.f); }
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		});

		// The heist: the crew make off in a car, a tough one; then the city-wide pursuit.
		AddStep(TEXT("heist"), 0.5f, [this]()
		{
			AFTOGameMode* GM = GetAuthGameMode();
			AFTOIncident* Heist = GM ? GM->FTOSetPieceNow(TEXT("Heist")) : nullptr;
			if (!Heist)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: heist: NO HEIST."));
				return;
			}
			GM->GetCrimeDirector()->TriggerHeistGetaway();
			const AFTOIncident* Chase = nullptr;
			for (const AFTOIncident* Incident : GetWorld()->GetGameState<AFTOGameState>()->GetIncidents())
			{
				Chase = Incident && Incident->IsActive() && Incident->GetInfo().TemplateId == TEXT("HeistGetaway") ? Incident : Chase;
			}
			const AFTOTrafficCar* Car = Chase ? Cast<AFTOTrafficCar>(Chase->GetAttachParentActor()) : nullptr;
			UE_LOG(LogFTO, Display, TEXT("SMOKE: heist: the crew %s (the heist call %s), the getaway car %s, toughness x%.1f."),
				Chase ? TEXT("made a run for it") : TEXT("NEVER LEFT"), Heist->IsActive() ? TEXT("STILL OPEN") : TEXT("over"),
				Car && Car->GetCarState() == EFTOCarState::Fleeing ? TEXT("fleeing") : TEXT("NOT FLEEING"), Car && Car->GetDamage() ? Car->GetDamage()->Toughness : 0.f);
			if (Car)
			{
				ViewFrom(Car->GetActorLocation() + FVector(-900.f, 0.f, 600.f), Car->GetActorLocation());
			}
		});
		AddShot(TEXT("21j_heist_getaway"), 0.6f);
		AddStep(TEXT("pursuit"), 0.5f, [this]()
		{
			AFTOGameMode* GM = GetAuthGameMode();
			const AFTOIncident* Chase = GM ? GM->FTOSetPieceNow(TEXT("Pursuit")) : nullptr;
			const AFTOTrafficCar* Car = Chase ? Cast<AFTOTrafficCar>(Chase->GetAttachParentActor()) : nullptr;
			UE_LOG(LogFTO, Display, TEXT("SMOKE: pursuit: %s, the car %s, toughness x%.1f, %.0f m from the officer."),
				Chase ? *Chase->GetInfo().Title.ToString() : TEXT("NO PURSUIT"), Car && Car->GetCarState() == EFTOCarState::Fleeing ? TEXT("fleeing") : TEXT("NOT FLEEING"),
				Car && Car->GetDamage() ? Car->GetDamage()->Toughness : 0.f, Car && GetPawn() ? FVector::Dist2D(Car->GetActorLocation(), GetPawn()->GetActorLocation()) / 100.f : -1.f);
			// (Tidy the chases away: the tour goes on.)
			TArray<AFTOIncident*> Chases;
			for (AFTOIncident* Incident : GetWorld()->GetGameState<AFTOGameState>()->GetIncidents())
			{
				if (Incident && (Incident->GetInfo().TemplateId == TEXT("Pursuit") || Incident->GetInfo().TemplateId == TEXT("HeistGetaway")))
				{
					Chases.Add(Incident);
				}
			}
			for (AFTOIncident* Incident : Chases)
			{
				if (AActor* Getaway = Incident->GetAttachParentActor())
				{
					Getaway->Destroy();
				}
				Incident->Destroy();
			}
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		});

		// Rush hour: the last two minutes.
		AddStep(TEXT("rush hour"), 2.5f, [this]()
		{
			if (AFTOGameMode* GM = GetAuthGameMode())
			{
				GM->FTOShiftTimeLeft(100.f);
			}
		});
		AddShot(TEXT("21k_rush_hour"), 0.2f);
		AddStep(TEXT("rush hour result"), 0.f, [this]()
		{
			const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
			int32 Open = 0;
			for (const AFTOIncident* Incident : GS->GetIncidents())
			{
				Open += Incident && Incident->IsActive() ? 1 : 0;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: rush hour: %s (%.0f s left, %d calls open)."), GS->IsRushHour() ? TEXT("on") : TEXT("NOT ON"), GS->GetShiftTimeRemaining(), Open);
			if (AFTOGameMode* GM = GetAuthGameMode())
			{
				GM->FTOShiftTimeLeft(600.f);
			}
		});

		// A word with a passer-by: the conversation panel.
		AddStep(TEXT("stop a citizen"), 1.2f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			AFTOPedestrian* Nearest = nullptr;
			for (TActorIterator<AFTOPedestrian> It(GetWorld()); It && Cop; ++It)
			{
				if (It->GetClass() == AFTOPedestrian::StaticClass() &&
					(!Nearest || FVector::DistSquared(It->GetActorLocation(), Cop->GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), Cop->GetActorLocation())))
				{
					Nearest = *It;
				}
			}
			if (!Nearest)
			{
				return;
			}
			const FVector Front = Nearest->GetActorLocation() + Nearest->GetActorForwardVector() * 160.f;
			Cop->TeleportTo(Front + FVector(0.f, 0.f, 6.f), (Nearest->GetActorLocation() - Front).Rotation());
			if (APlayerController* PC = GetPC())
			{
				PC->SetViewTargetWithBlend(Cop, 0.f);
				PC->SetControlRotation((Nearest->GetActorLocation() - Front).Rotation() + FRotator(-10.f, 0.f, 0.f));
			}
			Nearest->Interact(Cop);
			Nearest->TalkChoice(Cop, 1);
		});
		AddShot(TEXT("21e_conversation"), 0.3f);

		// Stop and search on the street: talk to citizens, search them, and arrest one who's carrying (and one who isn't).
		AddStep(TEXT("stop and search"), 3.f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop)
			{
				return;
			}
			TArray<AFTOPedestrian*> Crowd;
			for (TActorIterator<AFTOPedestrian> It(GetWorld()); It; ++It)
			{
				if (It->GetClass() == AFTOPedestrian::StaticClass() && !It->IsActorBeingDestroyed())
				{
					Crowd.Add(*It);
				}
			}
			Crowd.Sort([Cop](const AFTOPedestrian& A, const AFTOPedestrian& B)
			{
				return FVector::DistSquared(A.GetActorLocation(), Cop->GetActorLocation()) < FVector::DistSquared(B.GetActorLocation(), Cop->GetActorLocation());
			});
			TWeakObjectPtr<AFTOPedestrian> Carrying;
			TWeakObjectPtr<AFTOPedestrian> Clean;
			int32 Searched = 0;
			for (AFTOPedestrian* Citizen : Crowd)
			{
				if (Searched >= 40 || (Carrying.IsValid() && Clean.IsValid()))
				{
					break;
				}
				const FVector At = Citizen->GetActorLocation();
				Cop->TeleportTo(At + Citizen->GetActorForwardVector() * 140.f + FVector(0.f, 0.f, 6.f), (-Citizen->GetActorForwardVector()).Rotation());
				Citizen->Interact(Cop);
				if (Searched == 0)
				{
					Citizen->TalkChoice(Cop, 0);
					Citizen->TalkChoice(Cop, 1);
					TArray<FText> Options;
					Citizen->GetTalkOptions(Cop, Options);
					UE_LOG(LogFTO, Display, TEXT("SMOKE: talking to a citizen (\"%s\"): %d options, %s."), *Citizen->GetTalkTitle().ToString(), Options.Num(),
						Cop->GetTalkingTo() == Citizen ? TEXT("still talking") : TEXT("CONVERSATION ENDED"));
				}
				Citizen->TalkChoice(Cop, 2);
				// (Done with the pat-down at once: the next one's straight after.)
				if (Cop->IsInSyncedAction())
				{
					Cop->EndSyncedAction();
				}
				++Searched;
				TWeakObjectPtr<AFTOPedestrian>& Keep = Citizen->GetFound().IsEmpty() ? Clean : Carrying;
				if (!Keep.IsValid())
				{
					Keep = Citizen;
				}
				Citizen->TalkChoice(Cop, 3);
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: searched %d citizens: %s carrying something, %s clean."), Searched,
				Carrying.IsValid() ? TEXT("one") : TEXT("NONE"), Clean.IsValid() ? TEXT("one") : TEXT("NONE"));

			// The one with contraband: arrested for it (a crime scene springs up with them as the perp).
			if (AFTOPedestrian* Citizen = Carrying.Get())
			{
				Cop->TeleportTo(Citizen->GetActorLocation() + Citizen->GetActorForwardVector() * 140.f + FVector(0.f, 0.f, 6.f), (-Citizen->GetActorForwardVector()).Rotation());
				const FString Found = Citizen->GetFound();
				Citizen->Interact(Cop);
				Citizen->TalkChoice(Cop, 2);
				int32 Caught = 0;
				for (TActorIterator<AFTOPerp> It(GetWorld()); It; ++It)
				{
					Caught += It->GetIncident() && It->GetIncident()->GetInfo().TemplateId == TEXT("StolenGoods") && It->GetArrestState() != EFTOPerpArrest::None ? 1 : 0;
				}
				UE_LOG(LogFTO, Display, TEXT("SMOKE: arrested a citizen carrying %s: %s."), *Found, Caught > 0 ? TEXT("they're a suspect now") : TEXT("NO ARREST"));
			}
			// The clean one: a wrongful arrest, and the city minds.
			if (AFTOPedestrian* Citizen = Clean.Get())
			{
				const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
				const float Before = GS ? GS->GetChaos() : 0.f;
				Cop->TeleportTo(Citizen->GetActorLocation() + Citizen->GetActorForwardVector() * 140.f + FVector(0.f, 0.f, 6.f), (-Citizen->GetActorForwardVector()).Rotation());
				Citizen->Interact(Cop);
				Citizen->TalkChoice(Cop, 2);
				int32 Wrongful = 0;
				for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
				{
					Wrongful += It->GetCrime().ToString().Contains(TEXT("Wrongful")) ? 1 : 0;
				}
				UE_LOG(LogFTO, Display, TEXT("SMOKE: arrested a clean citizen: %s, chaos %+.1f."), Wrongful > 0 ? TEXT("cuffed for nothing") : TEXT("NOT ARRESTED"),
					GS ? GS->GetChaos() - Before : 0.f);
			}
		});

		AddStep(TEXT("tidy up the stop and search"), 0.3f, [this]()
		{
			// Whatever came of those arrests (cuffed, a scuffle, a runner), the station takes it from here, so the officer's
			// hands are free for what's next.
			for (TActorIterator<AFTOIncident> It(GetWorld()); It; ++It)
			{
				if (It->GetInfo().TemplateId == TEXT("StolenGoods"))
				{
					It->Destroy();
				}
			}
			for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
			{
				if (It->GetCrime().ToString().Contains(TEXT("Wrongful")) || It->GetCrime().ToString().Contains(TEXT("Stolen")))
				{
					It->Destroy();
				}
			}
			if (AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn()))
			{
				if (Cop->IsInSyncedAction())
				{
					Cop->EndSyncedAction();
				}
				if (Cop->GetKnockdown() && Cop->GetKnockdown()->IsDown())
				{
					Cop->GetKnockdown()->Recover();
				}
			}
		});

		// Upstairs: the lift to the top of a tower and back down, and the outside stairs up to a house's first floor.
		// (Once the officer's hands are free: the arrests before take a moment to finish.)
		AddWait(TEXT("officer free"), 6.f, [this]()
		{
			const AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			return !Cop || Cop->IsReadyForAction();
		});
		AddStep(TEXT("call the lift"), 0.3f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			AFTOLift* Lift = nullptr;
			for (TActorIterator<AFTOLift> It(GetWorld()); It && Cop; ++It)
			{
				if (It->GetFloor() == 0 && It->GetNumFloors() >= 4 &&
					(!Lift || FVector::DistSquared(It->GetActorLocation(), Cop->GetActorLocation()) < FVector::DistSquared(Lift->GetActorLocation(), Cop->GetActorLocation())))
				{
					Lift = *It;
				}
			}
			if (!Cop || !Lift)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: lift: NO LIFT."));
				return;
			}
			TestLift = Lift;
			Cop->TeleportTo(Lift->GetArrivalPoint() + FVector(0.f, 0.f, 6.f), (-Lift->GetActorForwardVector()).Rotation());
			if (APlayerController* PC = GetPC())
			{
				PC->SetViewTargetWithBlend(Cop, 0.f);
				PC->SetControlRotation((-Lift->GetActorForwardVector()).Rotation() + FRotator(-8.f, 0.f, 0.f));
			}
		});
		AddShot(TEXT("22a_lift_street"), 0.3f);
		AddStep(TEXT("ride to the top"), 2.6f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop || !TestLift.IsValid())
			{
				return;
			}
			TestLift->Interact(Cop);
			const bool bButtons = Cop->GetTalkingTo() == TestLift.Get();
			Cop->TalkPressed(2); // "Top floor"
			UE_LOG(LogFTO, Display, TEXT("SMOKE: lift: %s, going up %d floors."), bButtons ? TEXT("buttons up") : TEXT("NO BUTTONS"), TestLift->GetNumFloors() - 1);
		});
		AddStep(TEXT("at the top"), 0.2f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			const AFTOLift* Top = nullptr;
			for (TActorIterator<AFTOLift> It(GetWorld()); It && TestLift.IsValid(); ++It)
			{
				if (It->GetFloor() == TestLift->GetNumFloors() - 1 && FVector::Dist2D(It->GetActorLocation(), TestLift->GetActorLocation()) < 200.f)
				{
					Top = *It;
				}
			}
			const float Off = Cop && Top ? FVector::Dist(Cop->GetActorLocation(), Top->GetArrivalPoint()) : -1.f;
			UE_LOG(LogFTO, Display, TEXT("SMOKE: lift ride: %s (%.0f cm up, %.0f cm from the top stop)."), Top && Off >= 0.f && Off < 150.f ? TEXT("out on the top floor") : TEXT("DIDN'T ARRIVE"),
				Cop && TestLift.IsValid() ? Cop->GetActorLocation().Z - TestLift->GetActorLocation().Z : 0.f, Off);
			if (Cop && Top)
			{
				// Look round the floor.
				if (APlayerController* PC = GetPC())
				{
					PC->SetControlRotation(Top->GetActorForwardVector().Rotation() + FRotator(-5.f, 30.f, 0.f));
				}
			}
		});
		AddShot(TEXT("22b_top_floor"), 0.4f);
		AddStep(TEXT("walk the floor"), 1.5f, [this]()
		{
			if (const APawn* Cop = GetPawn())
			{
				WalkDirection = Cop->GetActorForwardVector();
				bWalkOfficer = true;
			}
		});
		AddStep(TEXT("stop walking the floor"), 0.2f, [this]()
		{
			bWalkOfficer = false;
			const APawn* Cop = GetPawn();
			UE_LOG(LogFTO, Display, TEXT("SMOKE: walked across the top floor: %s."),
				Cop && TestLift.IsValid() && Cop->GetActorLocation().Z - TestLift->GetActorLocation().Z > 300.f ? TEXT("still up there, on a floor") : TEXT("FELL THROUGH"));
		});
		AddStep(TEXT("stairs"), 0.2f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			const AFTOCityGenerator* City = GetCity();
			if (!Cop || !City || City->GetOutsideStairs().IsEmpty())
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: stairs: NO STAIRS."));
				return;
			}
			const FTransform& Foot = City->GetOutsideStairs()[0];
			Cop->TeleportTo(Foot.GetLocation() + FVector(0.f, 0.f, 100.f), Foot.Rotator());
			StairsFoot = Foot.GetLocation();
			WalkDirection = Foot.GetRotation().GetForwardVector();
			bWalkOfficer = true;
			if (APlayerController* PC = GetPC())
			{
				PC->SetControlRotation(Foot.Rotator() + FRotator(-10.f, 0.f, 0.f));
			}
		});
		// At the top of the flight: turn in through the doorway.
		AddWait(TEXT("reach the landing"), 4.f, [this]()
		{
			const APawn* Cop = GetPawn();
			const AFTOCityGenerator* City = GetCity();
			return !Cop || !City || City->GetOutsideStairTops().IsEmpty() ||
				FVector::Dist2D(Cop->GetActorLocation(), City->GetOutsideStairTops()[0].GetLocation()) < 70.f;
		});
		AddStep(TEXT("through the doorway"), 1.2f, [this]()
		{
			if (const AFTOCityGenerator* City = GetCity(); City && !City->GetOutsideStairTops().IsEmpty())
			{
				WalkDirection = City->GetOutsideStairTops()[0].GetRotation().GetForwardVector();
			}
		});
		AddStep(TEXT("up the stairs"), 0.3f, [this]()
		{
			bWalkOfficer = false;
			const APawn* Cop = GetPawn();
			const float Climbed = Cop ? Cop->GetActorLocation().Z - StairsFoot.Z : 0.f;
			UE_LOG(LogFTO, Display, TEXT("SMOKE: stairs: %s (%.0f cm up, %.0f cm from the foot)."), Climbed > 330.f ? TEXT("climbed to the first floor") : TEXT("DIDN'T MAKE IT UP"), Climbed,
				Cop ? FVector::Dist2D(Cop->GetActorLocation(), StairsFoot) : -1.f);
			if (Cop)
			{
				ViewFrom(StairsFoot + WalkDirection * 300.f + FVector::CrossProduct(FVector::UpVector, WalkDirection) * 700.f + FVector(0.f, 0.f, 450.f), StairsFoot + WalkDirection * 300.f + FVector(0.f, 0.f, 200.f));
			}
		});
		AddShot(TEXT("22c_upstairs"), 0.3f);

		// A tagger at the wall and a vandal going from bin to bin.
		AddStep(TEXT("graffiti"), 5.f, [this]()
		{
			TestPerp = StagePerp(TEXT("Graffiti"), 500.f);
			if (TestPerp.IsValid())
			{
				TestPerp->SetForcedResponse(EFTOArrestResponse::Comply);
			}
		});
		AddStep(TEXT("graffiti result"), 0.1f, [this]()
		{
			const AFTOGraffitiTag* Tag = nullptr;
			for (TActorIterator<AFTOGraffitiTag> It(GetWorld()); It; ++It)
			{
				Tag = *It;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: graffiti: %s (%.0f%% sprayed, the tagger %.0f cm from it)."), Tag ? TEXT("a tag on the wall") : TEXT("NO TAG"), Tag ? Tag->GetProgress() * 100.f : 0.f,
				Tag && TestPerp.IsValid() ? FVector::Dist2D(Tag->GetActorLocation(), TestPerp->GetActorLocation()) : -1.f);
			if (Tag)
			{
				const_cast<AFTOGraffitiTag*>(Tag)->SetProgress(1.f); // the whole thing, for the photo
			}
			if (Tag && TestPerp.IsValid())
			{
				ViewFrom(Tag->GetActorLocation() + Tag->GetActorForwardVector() * 450.f + TestPerp->GetActorRightVector() * 320.f + FVector(0.f, 0.f, 60.f), Tag->GetActorLocation());
			}
		});
		AddShot(TEXT("21c_graffiti"), 0.3f);
		AddStep(TEXT("vandal"), 9.f, [this]()
		{
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
			const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			CrashHealthBefore = Wreckage ? Wreckage->NumBroken() : 0;
			TestPerp = StagePerp(TEXT("Vandalism"), 600.f);
			if (TestPerp.IsValid())
			{
				TestPerp->SetForcedResponse(EFTOArrestResponse::Comply);
				ViewArrest(TestPerp.Get());
			}
		});
		AddStep(TEXT("vandal result"), 0.f, [this]()
		{
			const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			const int32 Smashed = Wreckage ? Wreckage->NumBroken() - int32(CrashHealthBefore) : 0;
			UE_LOG(LogFTO, Display, TEXT("SMOKE: the vandal smashed %d thing%s."), Smashed, Smashed == 1 ? TEXT("") : TEXT("s"));
			if (TestPerp.IsValid())
			{
				ViewArrest(TestPerp.Get());
			}
		});
		AddShot(TEXT("21d_vandal"), 0.3f);
		AddStep(TEXT("back to the officer again"), 0.3f, [this]()
		{
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		});

		AddStep(TEXT("footsteps"), 0.f, [this]()
		{
			// Everyone walking about has been heard, on what they were walking on.
			UE_LOG(LogFTO, Display, TEXT("SMOKE: footsteps so far: %d on concrete, %d wood, %d tile, %d carpet, %d metal, %d grass."),
				UFTOFootsteps::StepsOn(0), UFTOFootsteps::StepsOn(1), UFTOFootsteps::StepsOn(2), UFTOFootsteps::StepsOn(3), UFTOFootsteps::StepsOn(4), UFTOFootsteps::StepsOn(5));
		});
		AddStep(TEXT("arrest scores"), 0.f, [this]()
		{
			if (const AFTOPlayerState* PS = GetPC() ? GetPC()->GetPlayerState<AFTOPlayerState>() : nullptr)
			{
				const FFTOOfficerStats& Stats = PS->GetStats();
				UE_LOG(LogFTO, Display, TEXT("SMOKE: score after the hard arrests: %d (%d arrests, %d busts, best combo x%s)."), Stats.Score, Stats.Arrests, Stats.Busts,
					*FString::SanitizeFloat(FTOScoring::ComboMultiplier(Stats.BestCombo)));
			}
		});

		// Radio: going down called "officer down" by itself; then the callout wheel (up for backup), and push-to-talk.
		AddStep(TEXT("radio wheel"), 0.6f, [this]()
		{
			AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetPC());
			const AFTOPlayerState* PS = PC ? PC->GetPlayerState<AFTOPlayerState>() : nullptr;
			if (!PC || !PS)
			{
				return;
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: radio after the officer went down: %s."), PS->GetCallout().Callout == EFTOCallout::OfficerDown ? TEXT("officer down called in") : TEXT("NOTHING CALLED IN"));
			PC->WheelOpened();
			PC->WheelStick(FInputActionValue(FVector2D(0.f, 1.f)));
		});
		AddShot(TEXT("19a_radio_wheel"), 0.2f);
		AddStep(TEXT("radio callout"), 0.8f, [this]()
		{
			AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetPC());
			if (PC)
			{
				const EFTOCallout Picked = PC->GetWheelChoice();
				PC->WheelClosed();
				UE_LOG(LogFTO, Display, TEXT("SMOKE: radio wheel pointed at %s."), *StaticEnum<EFTOCallout>()->GetNameStringByValue(int64(Picked)));
			}
		});
		AddStep(TEXT("radio transmit"), 0.5f, [this]()
		{
			AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetPC());
			const AFTOPlayerState* PS = PC ? PC->GetPlayerState<AFTOPlayerState>() : nullptr;
			if (!PC || !PS)
			{
				return;
			}
			const USoundEffectSourcePresetChain* Filter = FTORadio::GetVoiceFilter();
			UE_LOG(LogFTO, Display, TEXT("SMOKE: radio callout: %s; voice filter has %d effects; squelch %s, chirp %s."),
				*StaticEnum<EFTOCallout>()->GetNameStringByValue(int64(PS->GetCallout().Callout)), Filter ? Filter->Chain.Num() : 0,
				FTORadio::SquelchOpen() && FTORadio::SquelchClose() ? TEXT("loaded") : TEXT("MISSING"), FTORadio::CalloutChirp() ? TEXT("loaded") : TEXT("MISSING"));
			PC->RadioPressed();
		});
		AddShot(TEXT("19b_radio_transmitting"), 0.3f);
		AddStep(TEXT("radio release"), 0.3f, [this]()
		{
			AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetPC());
			const AFTOPlayerState* PS = PC ? PC->GetPlayerState<AFTOPlayerState>() : nullptr;
			if (PC && PS)
			{
				const bool bWasOn = PS->IsOnRadio();
				PC->RadioReleased();
				UE_LOG(LogFTO, Display, TEXT("SMOKE: push-to-talk: %s, then %s."), bWasOn ? TEXT("on air") : TEXT("NOT ON AIR"), PS->IsOnRadio() ? TEXT("STILL ON AIR") : TEXT("off"));
			}
		});
		// Destruction: a shop window and a hydrant shot out, a lamp post knocked flat by a cruiser, the cruiser crashed
		// into a wall until it's a burning wreck, and a citizen's car written off.
		AddStep(TEXT("window: take aim"), 0.5f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			const AFTOCityGenerator* City = GetCity();
			if (!Cop || !City)
			{
				return;
			}
			if (AFTOGameMode* GM = GetAuthGameMode())
			{
				GM->FTOAddChaos(-100.f);
			}
			// The suspects trailing after the officer have been taken off to the cells (they'd crowd every shot).
			for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
			{
				It->Destroy();
			}
			// The shop front nearest the officer with nothing (a bus stop, a tree) between the pavement and its window.
			TArray<const FFTOBuilding*> Shops;
			for (const FFTOBuilding& Building : City->GetBuildings())
			{
				if (Building.Type == EFTOBuildingType::Shop)
				{
					Shops.Add(&Building);
				}
			}
			const FVector From = Cop->GetActorLocation();
			Shops.Sort([&From](const FFTOBuilding& A, const FFTOBuilding& B) { return FVector::DistSquared(A.DoorOutside, From) < FVector::DistSquared(B.DoorOutside, From); });
			UInstancedStaticMeshComponent* Glass = nullptr;
			FVector Stand = FVector::ZeroVector;
			for (const FFTOBuilding* Shop : Shops)
			{
				UInstancedStaticMeshComponent* Found = nullptr;
				FTransform Pane;
				if (!FindCityInstance(TEXT("SM_Wall_G_Shop_Glass"), Shop->DoorOutside, Found, TestInstance, Pane))
				{
					continue;
				}
				TestAway = (Shop->DoorOutside - Shop->Room.GetLocation()).GetSafeNormal2D();
				TestTarget = Pane.TransformPosition(Found->GetStaticMesh()->GetBoundingBox().GetCenter());
				Stand = TestTarget + TestAway * 450.f;
				FHitResult Sight;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOSmokeSight), false, Cop);
				const FVector Eye(Stand.X, Stand.Y, GroundZ(Stand) + 160.f);
				if (!GetWorld()->LineTraceSingleByChannel(Sight, Eye, TestTarget, ECC_FTOProjectile, Params) ||
					(Sight.GetComponent() == Found && Sight.Item == TestInstance))
				{
					Glass = Found;
					break;
				}
			}
			if (!Glass)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: window: NO SHOP WINDOW."));
				return;
			}
			TestISM = Glass;
			Cop->TeleportTo(FVector(Stand.X, Stand.Y, GroundZ(Stand) + 98.f), (-TestAway).Rotation());
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(Cop, 0.f); }
			if (Cop->GetDrawnWeapon() != EFTOWeapon::Shotgun)
			{
				Cop->SelectSlot(1);
			}
		});
		// (Aimed through the camera once it's caught up with the officer, and fired a moment later.)
		AddStep(TEXT("window: aim"), 0.12f, [this]() { AimAt(TestTarget); });
		AddStep(TEXT("window: aim again"), 0.12f, [this]()
		{
			// (Nobody strolling across the line of fire.)
			if (const APawn* Cop = GetPawn())
			{
				for (TActorIterator<AFTOPedestrian> It(GetWorld()); It; ++It)
				{
					if (FMath::PointDistToSegment(It->GetActorLocation(), Cop->GetActorLocation(), TestTarget) < 250.f)
					{
						It->Destroy();
					}
				}
			}
			AimAt(TestTarget); // (the camera swings with the aim: settle it)
		});
		AddStep(TEXT("window: fire"), 0.08f, [this]()
		{
			if (AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn()))
			{
				AimAt(TestTarget);
				const APlayerController* PC = GetPC();
				FHitResult Sight;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOSmokeSight), false, Cop);
				const FVector Eye = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
				if (PC && PC->PlayerCameraManager)
				{
					GetWorld()->LineTraceSingleByChannel(Sight, Eye, Eye + PC->PlayerCameraManager->GetCameraRotation().Vector() * 3000.f, ECC_FTOProjectile, Params);
				}
				UE_LOG(LogFTO, Display, TEXT("SMOKE: firing with the %s (%d in it)%s%s, crosshair on %s (%s, %.0f cm from the pane)."), *FTOWeapons::DisplayName(Cop->GetDrawnWeapon()).ToString(),
					Cop->GetClip(Cop->GetDrawnSlot()), Cop->IsReadyForAction() ? TEXT("") : TEXT(", NOT READY"), Cop->IsReloading() ? TEXT(", RELOADING") : TEXT(""),
					*GetNameSafe(Sight.GetComponent()), *GetNameSafe(Sight.GetActor()), Sight.bBlockingHit ? FVector::Dist(Sight.ImpactPoint, TestTarget) : -1.f);
				Cop->FirePressed();
				const FVector Across = FVector::CrossProduct(FVector::UpVector, TestAway);
				ViewFrom(TestTarget + TestAway * 330.f + Across * 330.f + FVector(0.f, 0.f, 40.f), TestTarget - FVector(0.f, 0.f, 40.f));
			}
		});
		AddShot(TEXT("20a_window"), 1.2f);
		AddStep(TEXT("window result"), 0.f, [this]()
		{
			const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			const UFTODebris* Debris = UFTODebris::Get(GetWorld());
			const bool bBroken = Wreckage && TestISM.IsValid() && Wreckage->IsBroken(TestISM->GetFName(), TestInstance);
			UE_LOG(LogFTO, Display, TEXT("SMOKE: shot a shop window: %s (%d bits flying, %d bullet holes)."), bBroken ? TEXT("shattered") : TEXT("STILL THERE"),
				Debris ? Debris->NumPieces() : -1, Debris ? Debris->NumHoles() : -1);
		});

		AddStep(TEXT("hydrant: take aim"), 0.45f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			UInstancedStaticMeshComponent* Hydrants = nullptr;
			FTransform Hydrant;
			if (!Cop || !FindCityInstance(TEXT("SM_Hydrant"), Cop->GetActorLocation(), Hydrants, TestInstance, Hydrant))
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: hydrant: NONE FOUND."));
				return;
			}
			TestISM = Hydrants;
			TestTarget = Hydrant.GetLocation() + FVector(0.f, 0.f, 40.f);
			// A clear shot at it from a few metres along the pavement (or across it).
			FVector Stand = Hydrant.GetLocation() + FVector(550.f, 0.f, 0.f);
			for (const FVector& Dir : { FVector(1.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), FVector(0.f, -1.f, 0.f) })
			{
				const FVector Spot = Hydrant.GetLocation() + Dir * 550.f;
				const FVector Eye(Spot.X, Spot.Y, GroundZ(Spot) + 150.f);
				FHitResult Block;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOSmokeClearShot), false, Cop);
				if (!GetWorld()->LineTraceSingleByChannel(Block, Eye, TestTarget, ECC_FTOProjectile, Params) || Block.GetComponent() == Hydrants)
				{
					Stand = Spot;
					TestAway = Dir;
					break;
				}
			}
			Cop->TeleportTo(FVector(Stand.X, Stand.Y, GroundZ(Stand) + 98.f), (TestTarget - Stand).GetSafeNormal2D().Rotation());
			// Back behind the officer (the crosshair is wherever their own camera looks).
			if (APlayerController* PC = GetPC())
			{
				PC->SetViewTargetWithBlend(Cop, 0.f);
				PC->SetControlRotation((TestTarget - Stand).Rotation());
			}
		});
		AddStep(TEXT("hydrant: aim"), 0.12f, [this]() { AimAt(TestTarget); });
		AddStep(TEXT("hydrant: aim again"), 0.12f, [this]() { AimAt(TestTarget); });
		AddStep(TEXT("hydrant: fire"), 0.1f, [this]()
		{
			if (AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn()))
			{
				AimAt(TestTarget);
				const UFTODebris* Debris = UFTODebris::Get(GetWorld());
				const APlayerController* PC = GetPC();
				FHitResult Sight;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOSmokeSight), false, Cop);
				const FVector Eye = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
				const bool bSees = PC && PC->PlayerCameraManager && GetWorld()->LineTraceSingleByChannel(Sight, Eye, Eye + PC->PlayerCameraManager->GetCameraRotation().Vector() * 3000.f,
					ECC_FTOProjectile, Params) && Sight.GetComponent() == TestISM.Get();
				UE_LOG(LogFTO, Display, TEXT("SMOKE: firing at the hydrant (%.0f m) with the %s (%d in it), crosshair %s; %d holes before."),
					FVector::Dist(Cop->GetActorLocation(), TestTarget) / 100.f, *FTOWeapons::DisplayName(Cop->GetDrawnWeapon()).ToString(), Cop->GetClip(Cop->GetDrawnSlot()),
					bSees ? TEXT("on it") : *FString::Printf(TEXT("on %s"), *GetNameSafe(Sight.GetComponent())), Debris ? Debris->NumHoles() : -1);
				Cop->FirePressed();
			}
		});
		AddStep(TEXT("hydrant: film"), 0.9f, [this]()
		{
			const FVector Across = FVector::CrossProduct(FVector::UpVector, TestAway);
			ViewFrom(TestTarget + TestAway * 250.f + Across * 420.f + FVector(0.f, 0.f, 120.f), TestTarget + FVector(0.f, 0.f, 60.f));
		});
		AddShot(TEXT("20b_hydrant"), 0.2f);
		AddStep(TEXT("hydrant result"), 0.f, [this]()
		{
			const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			const UFTODebris* Debris = UFTODebris::Get(GetWorld());
			const bool bBroken = Wreckage && TestISM.IsValid() && Wreckage->IsBroken(TestISM->GetFName(), TestInstance);
			UE_LOG(LogFTO, Display, TEXT("SMOKE: shot a hydrant: %s, %d fountain(s) going."), bBroken ? TEXT("knocked off") : TEXT("STILL STANDING"), Debris ? Debris->NumFountains() : -1);
		});

		AddStep(TEXT("lamp post"), 0.f, [this]()
		{
			const APawn* Cop = GetPawn();
			UInstancedStaticMeshComponent* Posts = nullptr;
			FTransform Post;
			if (!Cop || !TestCruiser || !FindCityInstance(TEXT("SM_LampPost"), Cop->GetActorLocation(), Posts, TestInstance, Post))
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: lamp post: NO CAR OR POST."));
				return;
			}
			TestISM = Posts;
			TestTarget = Post.GetLocation();
			// A run-up with nothing in the way but the post.
			TestCruiser->SetAutopilot(false);
			TestCruiser->StopDead();
			FVector Start = TestTarget + FVector(1100.f, 0.f, 0.f);
			for (const FVector& Dir : { FVector(1.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), FVector(0.f, -1.f, 0.f) })
			{
				const FVector Spot = TestTarget + Dir * 1100.f;
				const float Z = GroundZ(Spot) + AFTOCruiser::RideHeight;
				FHitResult First;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOSmokeRunUp), false, TestCruiser);
				const FVector From(Spot.X, Spot.Y, Z);
				const FVector To(TestTarget.X, TestTarget.Y, Z);
				if (GetWorld()->SweepSingleByChannel(First, From, To, (-Dir).ToOrientationQuat(), ECC_Pawn, FCollisionShape::MakeBox(FVector(240.f, 108.f, 72.f)), Params) &&
					First.GetComponent() == Posts)
				{
					Start = From;
					TestAway = Dir;
					break;
				}
			}
			// (Nobody else's car wandering into the run-up.)
			for (TActorIterator<AFTOTrafficCar> It(GetWorld()); It; ++It)
			{
				if (FMath::PointDistToSegment(It->GetActorLocation(), Start, TestTarget) < 600.f)
				{
					It->Destroy();
				}
			}
			TestCruiser->SetActorLocationAndRotation(Start, (-TestAway).Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
			TestCruiser->SetAutopilot(true, 1.f, 0.f);
			const FVector Across = FVector::CrossProduct(FVector::UpVector, TestAway);
			ViewFrom(TestTarget + TestAway * 200.f + Across * 900.f + FVector(0.f, 0.f, 250.f), TestTarget + FVector(0.f, 0.f, 150.f));
		});
		// Off the gas once it's through (so it doesn't carry on into the building behind), and a moment to see it fall.
		AddWait(TEXT("post goes"), 3.f, [this]()
		{
			const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			return Wreckage && TestISM.IsValid() && Wreckage->IsBroken(TestISM->GetFName(), TestInstance);
		});
		AddStep(TEXT("brake"), 0.35f, [this]()
		{
			if (TestCruiser)
			{
				TestCruiser->SetAutopilot(false);
				TestCruiser->StopDead();
			}
		});
		AddShot(TEXT("20c_lamp_post"), 0.4f);
		AddStep(TEXT("lamp post result"), 0.f, [this]()
		{
			const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			const bool bDown = Wreckage && TestISM.IsValid() && Wreckage->IsBroken(TestISM->GetFName(), TestInstance);
			UE_LOG(LogFTO, Display, TEXT("SMOKE: drove into a lamp post: %s."), bDown ? TEXT("knocked flat") : TEXT("STILL STANDING"));
		});

		// Into the wall beside a front door, harder each time.
		for (const float RunUp : { 380.f, 750.f, 1150.f })
		{
			AddStep(TEXT("crash"), 0.f, [this, RunUp]()
			{
				const AFTOCityGenerator* City = GetCity();
				if (!TestCruiser || !City)
				{
					return;
				}
				if (RunUp < 400.f)
				{
					// A stretch of front wall near the car (beside a door) with a clear run at it, the car's box swept
					// all the way in: the first thing it meets must be the wall itself.
					TArray<const FFTOBuilding*> Nearby;
					for (const FFTOBuilding& Building : City->GetBuildings())
					{
						Nearby.Add(&Building);
					}
					const FVector Car = TestCruiser->GetActorLocation();
					Nearby.Sort([&Car](const FFTOBuilding& A, const FFTOBuilding& B) { return FVector::DistSquared(A.DoorOutside, Car) < FVector::DistSquared(B.DoorOutside, Car); });
					bool bFound = false;
					for (int32 i = 0; i < FMath::Min(8, Nearby.Num()) && !bFound; ++i)
					{
						const FVector Out = (Nearby[i]->DoorOutside - Nearby[i]->Room.GetLocation()).GetSafeNormal2D();
						const FVector Along = FVector::CrossProduct(FVector::UpVector, Out);
						for (const float Offset : { 320.f, -320.f, 600.f, -600.f })
						{
							const FVector Wall = Nearby[i]->DoorOutside - Out * 160.f + Along * Offset;
							const FVector Far = Wall + Out * 1400.f;
							const float Z = GroundZ(Far) + AFTOCruiser::RideHeight;
							FHitResult First;
							FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOSmokeRunUp), false, TestCruiser);
							if (GetPawn())
							{
								Params.AddIgnoredActor(GetPawn());
							}
							if (GetWorld()->SweepSingleByChannel(First, FVector(Far.X, Far.Y, Z), FVector(Wall.X, Wall.Y, Z), (-Out).ToOrientationQuat(), ECC_Pawn,
								FCollisionShape::MakeBox(FVector(240.f, 108.f, 72.f)), Params) && First.GetComponent() && !First.GetComponent()->IsA<USkeletalMeshComponent>() &&
								AFTODestruction::KindOf(First.GetComponent()) == EFTOBreakKind::None && !First.GetActor()->IsA<APawn>() &&
								FVector::Dist2D(First.ImpactPoint, Wall) < 250.f)
							{
								TestAway = Out;
								TestTarget = Wall;
								bFound = true;
								break;
							}
						}
					}
					if (!bFound)
					{
						UE_LOG(LogFTO, Display, TEXT("SMOKE: crash: NO CLEAR WALL."));
						return;
					}
					// Fresh from the motor pool, so each knock shows.
					if (TestCruiser->GetDamage())
					{
						TestCruiser->GetDamage()->Repair();
					}
					// The officer stands well clear.
					if (AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn()))
					{
						const FVector Clear = TestTarget + TestAway * 1600.f + FVector::CrossProduct(FVector::UpVector, TestAway) * 900.f;
						Cop->TeleportTo(FVector(Clear.X, Clear.Y, GroundZ(Clear) + 98.f), (-TestAway).Rotation());
					}
				}
				TestCruiser->SetAutopilot(false);
				TestCruiser->StopDead();
				const FVector Start = TestTarget + TestAway * (250.f + RunUp);
				TestCruiser->SetActorLocationAndRotation(FVector(Start.X, Start.Y, GroundZ(Start) + AFTOCruiser::RideHeight), (-TestAway).Rotation(), false, nullptr,
					ETeleportType::TeleportPhysics);
				CrashHealthBefore = TestCruiser->GetDamage() ? TestCruiser->GetDamage()->GetHealth() : 0.f;
				TestCruiser->SetAutopilot(true, 1.f, 0.f);
				const FVector Across = FVector::CrossProduct(FVector::UpVector, TestAway);
				ViewFrom(Start + Across * 800.f - TestAway * RunUp * 0.5f + FVector(0.f, 0.f, 260.f), TestTarget + TestAway * 300.f);
			});
			// One clean hit: off the gas the moment it lands (the autopilot would keep bumping it into the wall).
			AddWait(TEXT("crash lands"), 3.f, [this]()
			{
				return TestCruiser && TestCruiser->GetDamage() && TestCruiser->GetDamage()->GetHealth() < CrashHealthBefore;
			});
			AddStep(TEXT("crash result"), 0.4f, [this]()
			{
				if (!TestCruiser)
				{
					return;
				}
				TestCruiser->SetAutopilot(false);
				TestCruiser->StopDead();
				const UFTOVehicleDamage* Damage = TestCruiser->GetDamage();
				UE_LOG(LogFTO, Display, TEXT("SMOKE: cruiser crashed: health %.0f, %s, %d dent(s)."), Damage ? Damage->GetHealth() : -1.f,
					Damage ? *StaticEnum<EFTOCarDamage>()->GetNameStringByValue(int64(Damage->GetStage())) : TEXT("NO DAMAGE"), Damage ? Damage->GetDents().Num() : 0);
			});
			if (RunUp > 700.f)
			{
				AddStep(TEXT("look at the cruiser"), 1.5f, [this]()
				{
					if (TestCruiser)
					{
						const FVector Car = TestCruiser->GetActorLocation();
						const FVector Across = FVector::CrossProduct(FVector::UpVector, TestAway);
						ViewFrom(Car + TestAway * 350.f + Across * 520.f + FVector(0.f, 0.f, 240.f), Car + FVector(0.f, 0.f, 40.f));
					}
				});
				AddShot(RunUp > 1000.f ? TEXT("20e_wrecked") : TEXT("20d_smoking"), 0.3f);
			}
		}

		// A citizen's car side-swiped: a dent in the door right where it was hit, paint scraped to the metal.
		AddStep(TEXT("dent a citizen's car"), 0.8f, [this]()
		{
			const APawn* Cop = GetPawn();
			AFTOTrafficCar* Car = nullptr;
			for (TActorIterator<AFTOTrafficCar> It(GetWorld()); It && Cop; ++It)
			{
				if (It->GetCarState() == EFTOCarState::Driving &&
					(!Car || FVector::DistSquared(It->GetActorLocation(), Cop->GetActorLocation()) < FVector::DistSquared(Car->GetActorLocation(), Cop->GetActorLocation())))
				{
					Car = *It;
				}
			}
			UFTOVehicleDamage* Damage = Car ? Car->FindComponentByClass<UFTOVehicleDamage>() : nullptr;
			if (!Damage)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: dent: NO CAR."));
				return;
			}
			Car->Hold(); // stood still for the photo
			// A knock in the door and a harder one on the front corner.
			const FVector Side = Car->GetActorLocation() + Car->GetActorRightVector() * 95.f + FVector(0.f, 0.f, 20.f);
			Damage->ApplyDamage(18.f, Side, nullptr);
			Damage->ApplyDamage(12.f, Side + Car->GetActorForwardVector() * 40.f, nullptr);
			Damage->ApplyDamage(30.f, Car->GetActorLocation() + Car->GetActorForwardVector() * 210.f + Car->GetActorRightVector() * 70.f + FVector(0.f, 0.f, 10.f), nullptr);
			// (And the same on the other side, so whichever way the camera looks there's a dent in shot.)
			Damage->ApplyDamage(12.f, Car->GetActorLocation() - Car->GetActorRightVector() * 95.f + FVector(0.f, 0.f, 20.f), nullptr);
			Damage->ApplyDamage(14.f, Car->GetActorLocation() + Car->GetActorForwardVector() * 210.f - Car->GetActorRightVector() * 70.f + FVector(0.f, 0.f, 10.f), nullptr);
			TestCar = Car;
			UE_LOG(LogFTO, Display, TEXT("SMOKE: side-swiped a citizen's car: %d dent(s), health %.0f."), Damage->GetDents().Num(), Damage->GetHealth());
		});
		AddStep(TEXT("film the dent"), 0.1f, [this]()
		{
			if (TestCar.IsValid())
			{
				const AActor* Car = TestCar.Get();
				ViewFrom(Car->GetActorLocation() + Car->GetActorRightVector() * 330.f + Car->GetActorForwardVector() * 380.f + FVector(0.f, 0.f, 140.f),
					Car->GetActorLocation() + Car->GetActorRightVector() * 60.f + Car->GetActorForwardVector() * 90.f);
			}
		});
		AddShot(TEXT("20g_dented_door"), 0.3f);
		AddStep(TEXT("film the other side"), 0.1f, [this]()
		{
			if (TestCar.IsValid())
			{
				const AActor* Car = TestCar.Get();
				ViewFrom(Car->GetActorLocation() - Car->GetActorRightVector() * 330.f + Car->GetActorForwardVector() * 380.f + FVector(0.f, 0.f, 140.f),
					Car->GetActorLocation() - Car->GetActorRightVector() * 60.f + Car->GetActorForwardVector() * 90.f);
			}
		});
		AddShot(TEXT("20g2_dented_other_side"), 0.3f);
		AddStep(TEXT("write off a citizen's car"), 1.4f, [this]()
		{
			const APawn* Cop = GetPawn();
			AFTOTrafficCar* Car = nullptr;
			for (TActorIterator<AFTOTrafficCar> It(GetWorld()); It && Cop; ++It)
			{
				if (It->GetCarState() == EFTOCarState::Driving &&
					(!Car || FVector::DistSquared(It->GetActorLocation(), Cop->GetActorLocation()) < FVector::DistSquared(Car->GetActorLocation(), Cop->GetActorLocation())))
				{
					Car = *It;
				}
			}
			if (UFTOVehicleDamage* Damage = Car ? Car->FindComponentByClass<UFTOVehicleDamage>() : nullptr)
			{
				Damage->ApplyDamage(60.f, Car->GetActorLocation() + Car->GetActorForwardVector() * 200.f, GetPC());
				Damage->ApplyDamage(60.f, Car->GetActorLocation() - Car->GetActorForwardVector() * 200.f, GetPC());
				TestCar = Car;
				ViewFrom(Car->GetActorLocation() + Car->GetActorRightVector() * 600.f + Car->GetActorForwardVector() * 300.f + FVector(0.f, 0.f, 250.f), Car->GetActorLocation());
			}
		});
		AddShot(TEXT("20f_wrecked_car"), 0.3f);
		AddStep(TEXT("wreck result"), 0.f, [this]()
		{
			const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
			UE_LOG(LogFTO, Display, TEXT("SMOKE: citizen's car written off: %s (report card: %d things broken, %d cars)."),
				TestCar.IsValid() && TestCar->GetCarState() == EFTOCarState::Wrecked ? TEXT("wrecked, stopped") : TEXT("NOT WRECKED"),
				GS ? GS->PropertyBroken : -1, GS ? GS->CarsWrecked : -1);
			// Good as new for whoever needs a cruiser next.
			if (TestCruiser && TestCruiser->GetDamage())
			{
				TestCruiser->GetDamage()->Repair();
			}
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		});

		// Demolition. A wall rammed (cracked), then driven straight through; rounds wearing a panel down; a stretch of
		// ground floor knocked out so the panels above it drop; a house brought down to its foundations; a car going up
		// beside a building.
		AddStep(TEXT("demolition: find a wall"), 0.f, [this]()
		{
			AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			const AFTOCityGenerator* City = GetCity();
			if (!Wreckage || !City || !TestCruiser)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: demolition: NO CITY OR CAR."));
				return;
			}
			TestStructure = INDEX_NONE;
			const TArray<FFTOStructure>& All = City->GetStructures();
			const FVector Car = TestCruiser->GetActorLocation();
			TArray<int32> Order;
			for (int32 s = 0; s < All.Num(); ++s)
			{
				// (Untouched: the crash checks before may have knocked a building or two about.)
				const bool bUntouched = !All[s].Pieces.ContainsByPredicate([Wreckage](const FFTOStructurePiece& Piece)
				{
					return Piece.Role == EFTOPieceRole::Wall && (Wreckage->GetWallDamage(Piece.Component, Piece.Instance) > 0.f || Wreckage->IsBroken(Piece.Component, Piece.Instance));
				});
				if (bUntouched && !Wreckage->IsStructureDown(s, 99) && All[s].Floors >= 2)
				{
					Order.Add(s);
				}
			}
			Order.Sort([&All, &Car](int32 A, int32 B) { return FVector::DistSquared(All[A].Center, Car) < FVector::DistSquared(All[B].Center, Car); });
			FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOSmokeRunUp), false, TestCruiser);
			if (GetPawn())
			{
				Params.AddIgnoredActor(GetPawn());
			}
			for (int32 k = 0; k < Order.Num() && TestStructure == INDEX_NONE; ++k)
			{
				const FFTOStructure& S = All[Order[k]];
				for (const FFTOStructurePiece& Piece : S.Pieces)
				{
					// (A solid stretch of wall: not a doorway.)
					if (Piece.Role != EFTOPieceRole::Wall || Piece.Level != 0 || Piece.Face < 0 || Piece.Component.ToString().Contains(TEXT("Door")) ||
						Piece.Component.ToString().Contains(TEXT("Roller")))
					{
						continue;
					}
					const FVector Out = Piece.Face == 0 ? FVector(1.f, 0.f, 0.f) : Piece.Face == 1 ? FVector(-1.f, 0.f, 0.f) : Piece.Face == 2 ? FVector(0.f, 1.f, 0.f) : FVector(0.f, -1.f, 0.f);
					const FVector Far = Piece.Location + Out * 1100.f;
					const float Z = GroundZ(Far) + AFTOCruiser::RideHeight;
					FHitResult First;
					EFTOPieceRole Role;
					UInstancedStaticMeshComponent* Wall = nullptr;
					int32 WallItem = INDEX_NONE;
					if (GetWorld()->SweepSingleByChannel(First, FVector(Far.X, Far.Y, Z), FVector(Piece.Location.X, Piece.Location.Y, Z) + Out * 5.f, (-Out).ToOrientationQuat(), ECC_Pawn,
							FCollisionShape::MakeBox(FVector(240.f, 108.f, 72.f)), Params) &&
						Wreckage->GetStructurePiece(First.GetComponent(), First.Item, Role) && (Role == EFTOPieceRole::Wall || Role == EFTOPieceRole::Glass) &&
						FVector::Dist2D(First.ImpactPoint, Piece.Location) < 120.f &&
						Wreckage->FindWallOf(First.GetComponent(), First.Item, Wall, WallItem) && FMath::Abs(GroundZ(Far) - S.Center.Z) < 40.f)
					{
						TestStructure = Order[k];
						TestTarget = Piece.Location;
						TestAway = Out;
						TestWallName = Wall->GetFName();
						TestWallInstance = WallItem;
						break;
					}
				}
			}
			if (TestStructure == INDEX_NONE)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: demolition: NO CLEAR WALL."));
				return;
			}
			if (TestCruiser->GetDamage())
			{
				TestCruiser->GetDamage()->Repair();
			}
			if (AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn()))
			{
				const FVector Clear = TestTarget + TestAway * 1800.f + FVector::CrossProduct(FVector::UpVector, TestAway) * 1000.f;
				Cop->TeleportTo(FVector(Clear.X, Clear.Y, GroundZ(Clear) + 98.f), (-TestAway).Rotation());
			}
			// A gentle run at it first.
			TestCruiser->SetAutopilot(false);
			TestCruiser->StopDead();
			const FVector Start = TestTarget + TestAway * 420.f;
			TestCruiser->SetActorLocationAndRotation(FVector(Start.X, Start.Y, GroundZ(Start) + AFTOCruiser::RideHeight), (-TestAway).Rotation(), false, nullptr,
				ETeleportType::TeleportPhysics);
			TestCruiser->Launch(1150.f);
			TestCruiser->SetAutopilot(true, 0.2f, 0.f);
			const FVector Across = FVector::CrossProduct(FVector::UpVector, TestAway);
			ViewFrom(TestTarget + TestAway * 700.f + Across * 700.f + FVector(0.f, 0.f, 250.f), TestTarget + FVector(0.f, 0.f, 180.f));
		});
		// (Whichever panels the bumper caught: the one aimed at, or its neighbours.)
		auto WornNear = [this]()
		{
			const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			const AFTOCityGenerator* City = GetCity();
			float Most = 0.f;
			if (Wreckage && City && City->GetStructures().IsValidIndex(TestStructure))
			{
				for (const FFTOStructurePiece& Piece : City->GetStructures()[TestStructure].Pieces)
				{
					if (Piece.Role == EFTOPieceRole::Wall && Piece.Level == 0 && FVector::Dist(Piece.Location, TestTarget) < 450.f)
					{
						Most = FMath::Max(Most, Wreckage->GetWallDamage(Piece.Component, Piece.Instance));
					}
				}
			}
			return Most;
		};
		AddStep(TEXT("before the ram"), 0.f, [this, WornNear]() { RamBaseline = WornNear(); });
		AddWait(TEXT("ram lands"), 4.f, [this, WornNear]() { return WornNear() > RamBaseline; });
		AddStep(TEXT("ram result"), 0.5f, [this, WornNear]()
		{
			if (TestCruiser)
			{
				TestCruiser->SetAutopilot(false);
				TestCruiser->StopDead();
			}
			const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			const UFTODebris* Debris = UFTODebris::Get(GetWorld());
			const float Worn = WornNear() > RamBaseline ? WornNear() : 0.f;
			UE_LOG(LogFTO, Display, TEXT("SMOKE: rammed a wall: %s (%.0f of %.0f worn, %d crack(s) showing)."), Worn >= AFTODestruction::WallStrength - 5.f ? TEXT("knocked in") : Worn > 0.f && Debris && Debris->NumCracks() > 0 ? TEXT("cracked") : TEXT("NO DAMAGE"),
				Worn, AFTODestruction::WallStrength, Debris ? Debris->NumCracks() : -1);
			if (TestCruiser)
			{
				// Back off for the photo.
				const FVector Back = TestTarget + TestAway * 900.f;
				TestCruiser->SetActorLocation(FVector(Back.X, Back.Y, GroundZ(Back) + AFTOCruiser::RideHeight), false, nullptr, ETeleportType::TeleportPhysics);
			}
			const FVector Across = FVector::CrossProduct(FVector::UpVector, TestAway);
			ViewFrom(TestTarget + TestAway * 420.f + Across * 260.f + FVector(0.f, 0.f, 170.f), TestTarget + FVector(0.f, 0.f, 120.f));
		});
		AddShot(TEXT("23a_cracked_wall"), 0.3f);
		AddStep(TEXT("through the wall"), 0.f, [this]()
		{
			if (!TestCruiser || TestStructure == INDEX_NONE)
			{
				return;
			}
			if (TestCruiser->GetDamage())
			{
				TestCruiser->GetDamage()->Repair();
			}
			BrokenBefore = AFTODestruction::Get(GetWorld()) ? AFTODestruction::Get(GetWorld())->NumBroken() : 0;
			const FVector Start = TestTarget + TestAway * 700.f;
			TestCruiser->SetActorLocationAndRotation(FVector(Start.X, Start.Y, GroundZ(Start) + AFTOCruiser::RideHeight), (-TestAway).Rotation(), false, nullptr,
				ETeleportType::TeleportPhysics);
			TestCruiser->Launch(2300.f);
			TestCruiser->SetAutopilot(true, 1.f, 0.f);
			const FVector Across = FVector::CrossProduct(FVector::UpVector, TestAway);
			ViewFrom(TestTarget + TestAway * 900.f + Across * 900.f + FVector(0.f, 0.f, 300.f), TestTarget + FVector(0.f, 0.f, 150.f));
		});
		AddWait(TEXT("wall goes"), 2.f, [this]()
		{
			const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			return Wreckage && Wreckage->IsBroken(TestWallName, TestWallInstance);
		});
		AddStep(TEXT("drive on in"), 0.4f, [this]() {});
		AddStep(TEXT("brake inside"), 0.35f, [this]()
		{
			if (TestCruiser)
			{
				TestCruiser->SetAutopilot(false);
				TestCruiser->StopDead();
			}
		});
		AddShot(TEXT("23b_through_the_wall"), 0.5f);
		AddStep(TEXT("through the wall result"), 0.f, [this]()
		{
			const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			const bool bGone = Wreckage && Wreckage->IsBroken(TestWallName, TestWallInstance);
			// (How far in its front bumper got.)
			const float Past = TestCruiser ? 240.f - FVector::DotProduct(TestCruiser->GetActorLocation() - TestTarget, TestAway) : 0.f;
			UE_LOG(LogFTO, Display, TEXT("SMOKE: drove through a wall: %s (front %.0f cm in, %d pieces broken, cruiser health %.0f)."),
				// (Through it, or through the hole the ram had already opened beside it.)
				Past > 50.f ? TEXT("through it") : bGone ? TEXT("WALL BROKE, CAR STOPPED") : TEXT("BOUNCED OFF"), Past,
				Wreckage ? Wreckage->NumBroken() - BrokenBefore : -1, TestCruiser && TestCruiser->GetDamage() ? TestCruiser->GetDamage()->GetHealth() : -1.f);
			if (TestCruiser)
			{
				// Out of there and good as new.
				const FVector Back = TestTarget + TestAway * 1500.f;
				TestCruiser->SetActorLocation(FVector(Back.X, Back.Y, GroundZ(Back) + AFTOCruiser::RideHeight), false, nullptr, ETeleportType::TeleportPhysics);
				if (TestCruiser->GetDamage())
				{
					TestCruiser->GetDamage()->Repair();
				}
			}
		});
		AddStep(TEXT("rounds into a wall"), 0.3f, [this]()
		{
			AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			UInstancedStaticMeshComponent* Wall = nullptr;
			int32 Item = INDEX_NONE;
			FVector At = FVector::ZeroVector;
			const AFTOCityGenerator* City = GetCity();
			const APawn* Cop = GetPawn();
			// A first-floor panel on the nearest building still standing.
			TArray<int32> Order;
			for (int32 s = 0; City && Wreckage && Cop && s < City->GetStructures().Num(); ++s)
			{
				if (!Wreckage->IsStructureDown(s, 99) && City->GetStructures()[s].Floors >= 1)
				{
					Order.Add(s);
				}
			}
			const TArray<FFTOStructure>* All = City ? &City->GetStructures() : nullptr;
			Order.Sort([All, Cop](int32 A, int32 B) { return FVector::DistSquared((*All)[A].Center, Cop->GetActorLocation()) < FVector::DistSquared((*All)[B].Center, Cop->GetActorLocation()); });
			const int32 Columns = Order.Num();
			bool bFound = false;
			for (int32 k = 0; k < Order.Num() && !bFound; ++k)
			{
				for (int32 Face = 0; Face < 4 && !bFound; ++Face)
				{
					for (int32 Col = 1; Col < 4 && !bFound; ++Col)
					{
						bFound = FindWall(Order[k], Face, Col, 1, Wall, Item, At);
					}
				}
			}
			if (!bFound)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: rounds into a wall: NO WALL (%d buildings standing)."), Columns);
				return;
			}
			const FVector Out = WallFacing(Wall, Item);
			for (int32 Shot = 0; Shot < 6; ++Shot)
			{
				const FVector Hit = At + FVector(0.f, 0.f, 120.f + Shot * 12.f) + FVector::CrossProduct(FVector::UpVector, Out) * (Shot * 9.f - 25.f);
				Wreckage->RoundHit(Wall, Item, Hit, -Out * 60000.f, EFTOWeapon::Rifle, GetPC());
			}
			const float Worn = Wreckage->GetWallDamage(Wall->GetFName(), Item);
			const UFTODebris* Debris = UFTODebris::Get(GetWorld());
			const int32 Cracks = Debris ? Debris->NumCracks() : 0;
			UE_LOG(LogFTO, Display, TEXT("SMOKE: six rifle rounds into a wall: %s (%.0f of %.0f worn, %d crack(s) showing)."),
				Worn > 20.f && Cracks > 0 ? TEXT("chipped away") : TEXT("NOT A MARK"), Worn, AFTODestruction::WallStrength, Cracks);
		});
		AddStep(TEXT("knock out a ground floor"), 0.f, [this]()
		{
			AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			const AFTOCityGenerator* City = GetCity();
			const APawn* Cop = GetPawn();
			if (!Wreckage || !City || !Cop)
			{
				return;
			}
			// A long face (seven panels or more) on a building with storeys above, still standing.
			const TArray<FFTOStructure>& All = City->GetStructures();
			int32 Best = INDEX_NONE;
			int32 BestFace = INDEX_NONE;
			for (int32 s = 0; s < All.Num(); ++s)
			{
				if (s == TestStructure || All[s].Floors < 1 || Wreckage->IsStructureDown(s, 99))
				{
					continue;
				}
				for (int32 Face = 0; Face < 4; ++Face)
				{
					if (All[s].Columns[Face] >= 7 && (Best == INDEX_NONE ||
						FVector::DistSquared(All[s].Center, Cop->GetActorLocation()) < FVector::DistSquared(All[Best].Center, Cop->GetActorLocation())))
					{
						Best = s;
						BestFace = Face;
					}
				}
			}
			if (Best == INDEX_NONE)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: knock out a ground floor: NO BUILDING."));
				return;
			}
			TestStructure = Best;
			TestInstance = BestFace;
			BrokenBefore = Wreckage->NumBroken();
			// Five panels in a row along it, leaving one at each end.
			FVector Middle = FVector::ZeroVector;
			for (int32 Col = 1; Col <= 5; ++Col)
			{
				UInstancedStaticMeshComponent* Wall = nullptr;
				int32 Item = INDEX_NONE;
				FVector At = FVector::ZeroVector;
				if (FindWall(Best, BestFace, Col, 0, Wall, Item, At))
				{
					const FVector Out = WallFacing(Wall, Item);
					Wreckage->Break(Wall, Item, At + FVector(0.f, 0.f, 150.f), -Out * 500.f, nullptr);
					if (Col == 3)
					{
						Middle = At;
						TestAway = Out;
					}
				}
			}
			TestTarget = Middle;
			const FVector Across = FVector::CrossProduct(FVector::UpVector, TestAway);
			ViewFrom(ClearSpot(Middle + TestAway * 60.f + FVector(0.f, 0.f, 300.f), Middle + TestAway * 1300.f + Across * 400.f + FVector(0.f, 0.f, 450.f)), Middle + FVector(0.f, 0.f, 350.f));
		});
		AddShot(TEXT("23c_ground_floor_out"), 0.7f);
		AddStep(TEXT("ground floor result"), 0.f, [this]()
		{
			const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			UInstancedStaticMeshComponent* Wall = nullptr;
			int32 Item = INDEX_NONE;
			FVector At = FVector::ZeroVector;
			// The panel above the middle had nothing within reach to hold it: it should have dropped.
			const bool bAboveStanding = FindWall(TestStructure, TestInstance, 3, 1, Wall, Item, At);
			const bool bDown = Wreckage && Wreckage->IsStructureDown(TestStructure, 99);
			UE_LOG(LogFTO, Display, TEXT("SMOKE: five ground-floor panels knocked out: %s, %s (%d pieces down)."),
				bAboveStanding ? TEXT("THE PANEL ABOVE IS HANGING IN THE AIR") : TEXT("the panel above dropped"),
				bDown ? TEXT("AND THE WHOLE BUILDING CAME DOWN") : TEXT("the rest still standing"), Wreckage ? Wreckage->NumBroken() - BrokenBefore : -1);
		});
		AddStep(TEXT("bring a house down"), 1.1f, [this]()
		{
			AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			const AFTOCityGenerator* City = GetCity();
			const APawn* Cop = GetPawn();
			if (!Wreckage || !City || !Cop)
			{
				return;
			}
			// The nearest two-storey house.
			const TArray<FFTOStructure>& All = City->GetStructures();
			int32 House = INDEX_NONE;
			for (int32 s = 0; s < All.Num(); ++s)
			{
				const FFTOBuilding* Room = City->GetBuilding(All[s].Building);
				if (Room && Room->Type == EFTOBuildingType::Home && All[s].Floors == 1 && !Wreckage->IsStructureDown(s, 99) && (House == INDEX_NONE ||
					FVector::DistSquared(All[s].Center, Cop->GetActorLocation()) < FVector::DistSquared(All[House].Center, Cop->GetActorLocation())))
				{
					House = s;
				}
			}
			if (House == INDEX_NONE)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: bring a house down: NO HOUSE."));
				return;
			}
			TestStructure = House;
			// Knock out one side of the ground floor, a panel at a time, till it gives.
			int32 Knocked = 0;
			FVector Side = All[House].Center;
			FVector Out = FVector::ForwardVector;
			for (int32 Col = 0; Col < All[House].Columns[0] && !Wreckage->IsStructureDown(House); ++Col)
			{
				UInstancedStaticMeshComponent* Wall = nullptr;
				int32 Item = INDEX_NONE;
				FVector At = FVector::ZeroVector;
				if (FindWall(House, 0, Col, 0, Wall, Item, At))
				{
					Out = WallFacing(Wall, Item);
					Side = At;
					Wreckage->Break(Wall, Item, At + FVector(0.f, 0.f, 150.f), -Out * 500.f, GetPC());
					++Knocked;
				}
			}
			TestInstance = Knocked;
			TestAway = Out;
			TestTarget = All[House].Center;
			// Watched from up over the street out front.
			const FFTOBuilding* Room = City->GetBuilding(All[House].Building);
			const FVector Front = Room ? (Room->DoorOutside - All[House].Center).GetSafeNormal2D() : Out;
			const FVector Aside = FVector::CrossProduct(FVector::UpVector, Front);
			ViewFrom(ClearSpot(TestTarget + Front * 650.f + FVector(0.f, 0.f, 700.f), TestTarget + Front * 1900.f + Aside * 500.f + FVector(0.f, 0.f, 900.f)),
				TestTarget + FVector(0.f, 0.f, 250.f));
			UE_LOG(LogFTO, Display, TEXT("SMOKE: bringing a house down: %s after %d panel(s), %d piece(s) falling."),
				Wreckage->IsStructureDown(House) ? TEXT("it gave way") : TEXT("STILL STANDING"), Knocked, Wreckage->NumFalling());
		});
		AddShot(TEXT("23d_house_coming_down"), 0.2f);
		AddStep(TEXT("let the dust settle"), 6.5f, [this]() {});
		AddShot(TEXT("23e_rubble"), 0.f);
		AddStep(TEXT("house result"), 0.f, [this]()
		{
			const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			const AFTOCityGenerator* City = GetCity();
			const FFTOStructure* S = City && City->GetStructures().IsValidIndex(TestStructure) ? &City->GetStructures()[TestStructure] : nullptr;
			int32 Left = 0;
			for (const FFTOStructurePiece& Piece : S ? S->Pieces : TArray<FFTOStructurePiece>())
			{
				Left += Piece.Role != EFTOPieceRole::Foundation && Wreckage && !Wreckage->IsBroken(Piece.Component, Piece.Instance) ? 1 : 0;
			}
			const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
			UE_LOG(LogFTO, Display, TEXT("SMOKE: the house: %s, %d piece(s) left standing, %d lumps of rubble, still falling %d (chaos %.0f)."),
				S && Wreckage && Wreckage->IsBuildingDown(S->Building) ? TEXT("down to its foundations") : TEXT("NOT DOWN"), Left, Wreckage ? Wreckage->NumRubble() : -1,
				Wreckage ? Wreckage->NumFalling() : -1, GS ? GS->GetChaos() : -1.f);
		});
		AddStep(TEXT("car goes up by a wall"), 0.f, [this]()
		{
			AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			const AFTOCityGenerator* City = GetCity();
			const APawn* Cop = GetPawn();
			if (!Wreckage || !City || !Cop || !TestCruiser || !TestCruiser->GetDamage())
			{
				return;
			}
			// A standing wall near the officer, the cruiser parked against it, set alight and left to go up.
			const TArray<FFTOStructure>& All = City->GetStructures();
			TArray<int32> Order;
			for (int32 s = 0; s < All.Num(); ++s)
			{
				if (!Wreckage->IsStructureDown(s, 99) && s != TestStructure)
				{
					Order.Add(s);
				}
			}
			Order.Sort([&All, Cop](int32 A, int32 B) { return FVector::DistSquared(All[A].Center, Cop->GetActorLocation()) < FVector::DistSquared(All[B].Center, Cop->GetActorLocation()); });
			UInstancedStaticMeshComponent* Wall = nullptr;
			int32 Item = INDEX_NONE;
			FVector At = FVector::ZeroVector;
			bool bFound = false;
			for (int32 k = 0; k < Order.Num() && !bFound; ++k)
			{
				for (int32 Face = 0; Face < 4 && !bFound; ++Face)
				{
					bFound = FindWall(Order[k], Face, 1, 0, Wall, Item, At) && FMath::Abs(GroundZ(At + WallFacing(Wall, Item) * 300.f) - At.Z) < 40.f;
				}
			}
			if (!bFound)
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: car bomb: NO WALL."));
				return;
			}
			const FVector Out = WallFacing(Wall, Item);
			TestISM = Wall;
			TestInstance = Item;
			TestTarget = At;
			TestAway = Out;
			TestCruiser->SetAutopilot(false);
			TestCruiser->StopDead();
			const FVector Park = At + Out * 260.f;
			TestCruiser->SetActorLocationAndRotation(FVector(Park.X, Park.Y, GroundZ(Park) + AFTOCruiser::RideHeight), FVector::CrossProduct(FVector::UpVector, Out).Rotation(),
				false, nullptr, ETeleportType::TeleportPhysics);
			TestCruiser->GetDamage()->Repair();
			TestCruiser->GetDamage()->ApplyDamage(90.f, Park + FVector(0.f, 0.f, 40.f), nullptr);
			// The officer's standing a little too close.
			if (AFTOCharacter* Officer = Cast<AFTOCharacter>(GetPawn()))
			{
				const FVector Near = Park + Out * 330.f;
				Officer->SetActorLocationAndRotation(FVector(Near.X, Near.Y, GroundZ(Near) + 98.f), (-Out).Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
			}
			const FVector Across = FVector::CrossProduct(FVector::UpVector, Out);
			ViewFrom(At + Out * 1300.f + Across * 700.f + FVector(0.f, 0.f, 300.f), At + Out * 150.f + FVector(0.f, 0.f, 150.f));
			UE_LOG(LogFTO, Display, TEXT("SMOKE: car bomb: cruiser %s by the wall (health %.0f)."),
				TestCruiser->GetDamage()->GetStage() == EFTOCarDamage::Burning ? TEXT("burning") : TEXT("NOT BURNING"), TestCruiser->GetDamage()->GetHealth());
		});
		AddWait(TEXT("it goes up"), 14.f, [this]()
		{
			return TestCruiser && TestCruiser->GetDamage() && TestCruiser->GetDamage()->IsWrecked();
		});
		AddShot(TEXT("23f_car_bomb"), 0.25f);
		AddStep(TEXT("car bomb result"), 0.f, [this]()
		{
			const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
			const float Worn = Wreckage && TestISM.IsValid() ? Wreckage->GetWallDamage(TestISM->GetFName(), TestInstance) : 0.f;
			const bool bGone = Wreckage && TestISM.IsValid() && Wreckage->IsBroken(TestISM->GetFName(), TestInstance);
			const UFTOKnockdownComponent* Knockdown = GetPawn() ? GetPawn()->FindComponentByClass<UFTOKnockdownComponent>() : nullptr;
			UE_LOG(LogFTO, Display, TEXT("SMOKE: car went up by a wall: %s, the wall %s (%.0f worn), the officer beside it %s."),
				TestCruiser && TestCruiser->GetDamage() && TestCruiser->GetDamage()->IsWrecked() ? TEXT("burned down and blew") : TEXT("NEVER WENT UP"),
				bGone ? TEXT("blown in") : Worn > 0.f ? TEXT("blackened and cracked") : TEXT("UNTOUCHED"), Worn,
				Knockdown && Knockdown->IsDown() ? TEXT("thrown off their feet") : TEXT("STILL STANDING"));
			if (TestCruiser && TestCruiser->GetDamage())
			{
				TestCruiser->GetDamage()->Repair();
			}
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		});
		// Hand to hand. A bar fight where the brawlers trade blows; an officer's combo on a suspect who fights back
		// (until they're down and cuffed); and a grab and a throw.
		AddStep(TEXT("fight: a brawl"), 0.f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			// (A fresh start: the demolition before has the city in uproar.)
			if (AFTOGameMode* GM = GetAuthGameMode())
			{
				GM->FTOAddChaos(-100.f);
			}
			TestPerp = StagePerp(TEXT("Riot"), 650.f);
			if (!Cop || !TestPerp.IsValid())
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: brawl: NO BRAWL."));
				return;
			}
			TestPerp->SetForcedResponse(EFTOArrestResponse::Fight);
			// (Watched from where the officer stands, well back: officers on the scene would break it up.)
			const FVector At = TestPerp->GetActorLocation();
			ViewFrom(ClearSpot(At + FVector(0.f, 0.f, 140.f), At + (Cop->GetActorLocation() - At).GetSafeNormal2D() * 520.f + FVector(0.f, 0.f, 160.f)), At);
			BlowsBefore = 0;
			for (TActorIterator<AFTOCrimeExtra> It(GetWorld()); It; ++It)
			{
				BlowsBefore += It->FindComponentByClass<UFTOKnockdownComponent>() ? It->FindComponentByClass<UFTOKnockdownComponent>()->GetMoveSerial() : 0;
			}
			BlowsBefore += TestPerp->FindComponentByClass<UFTOKnockdownComponent>() ? TestPerp->FindComponentByClass<UFTOKnockdownComponent>()->GetMoveSerial() : 0;
		});
		AddStep(TEXT("brawl goes on"), 3.4f, [this]() {});
		AddShot(TEXT("24a_brawl"), 0.6f);
		AddStep(TEXT("brawl result"), 0.f, [this]()
		{
			int32 Blows = 0;
			int32 Brawlers = 0;
			for (TActorIterator<AFTOCrimeExtra> It(GetWorld()); It; ++It)
			{
				Blows += It->FindComponentByClass<UFTOKnockdownComponent>() ? It->FindComponentByClass<UFTOKnockdownComponent>()->GetMoveSerial() : 0;
				Brawlers += It->GetRole() == EFTOExtraRole::Brawler ? 1 : 0;
			}
			Blows += TestPerp.IsValid() && TestPerp->FindComponentByClass<UFTOKnockdownComponent>() ? TestPerp->FindComponentByClass<UFTOKnockdownComponent>()->GetMoveSerial() : 0;
			// (Each swing and each one landed counts.)
			UE_LOG(LogFTO, Display, TEXT("SMOKE: a street brawl with %d brawler(s): %s (%d swings and hits)."), Brawlers,
				uint8(Blows - BlowsBefore) >= 3 ? TEXT("trading blows") : TEXT("NOBODY SWINGING"), uint8(Blows - BlowsBefore));
		});
		// (On their feet again first: the car bomb threw the officer over.)
		AddWait(TEXT("officer back up"), 9.f, [this]()
		{
			const AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			return !Cop || Cop->IsReadyForAction();
		});
		AddStep(TEXT("fight: square up"), 0.3f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop)
			{
				return;
			}
			// Empty-handed, face to face with a suspect who'd rather fight.
			if (Cop->GetDrawnWeapon() != EFTOWeapon::None)
			{
				Cop->SelectSlot(Cop->GetDrawnSlot()); // (put it away)
			}
			TestPerp = StagePerp(TEXT("Vandalism"), 110.f);
			if (!TestPerp.IsValid())
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: fist fight: NO SUSPECT."));
				return;
			}
			TestPerp->SetForcedResponse(EFTOArrestResponse::Fight);
			if (APlayerController* PC = GetPC())
			{
				PC->SetControlRotation((TestPerp->GetActorLocation() - Cop->GetActorLocation()).GetSafeNormal2D().Rotation());
				PC->SetViewTargetWithBlend(Cop, 0.f);
			}
			UE_LOG(LogFTO, Display, TEXT("SMOKE: squared up to a suspect %.0f cm away (officer %s)."), FVector::Dist2D(TestPerp->GetActorLocation(), Cop->GetActorLocation()),
				Cop->CanFight() ? TEXT("ready") : TEXT("NOT READY TO FIGHT"));
			// Filmed side on.
			const FVector Mid = (TestPerp->GetActorLocation() + Cop->GetActorLocation()) * 0.5f;
			const FVector Side = FVector::CrossProduct(FVector::UpVector, (TestPerp->GetActorLocation() - Cop->GetActorLocation()).GetSafeNormal2D());
			const FVector Toward = (TestPerp->GetActorLocation() - Cop->GetActorLocation()).GetSafeNormal2D();
			ViewFrom(ClearSpot(Mid + FVector(0.f, 0.f, 200.f), Mid - Toward * 320.f + Side * 260.f + FVector(0.f, 0.f, 220.f)), Mid);
			OfficerHits = 0;
		});
		for (int32 Swing = 0; Swing < 12; ++Swing)
		{
			AddStep(TEXT("fight: swing"), 0.82f, [this, Swing]()
			{
				AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
				if (!Cop || !TestPerp.IsValid() || TestPerp->GetArrestState() == EFTOPerpArrest::Surrendered)
				{
					return;
				}
				// Face them, then a jab, cross, hook... with a kick to finish every fourth.
				if (APlayerController* PC = GetPC())
				{
					PC->SetControlRotation((TestPerp->GetActorLocation() - Cop->GetActorLocation()).GetSafeNormal2D().Rotation());
				}
				OfficerHits += Cop->FindComponentByClass<UFTOKnockdownComponent>() && Cop->FindComponentByClass<UFTOKnockdownComponent>()->GetMove() != EFTOAnimAction::None &&
					uint8(Cop->FindComponentByClass<UFTOKnockdownComponent>()->GetMove()) >= uint8(EFTOAnimAction::HitLightFront) && uint8(Cop->FindComponentByClass<UFTOKnockdownComponent>()->GetMove()) <= uint8(EFTOAnimAction::HitHeavy) ? 1 : 0;
				Swing % 4 == 3 ? Cop->KickPressed() : Cop->PunchPressed();
			});
			if (Swing == 2)
			{
				AddShot(TEXT("24b_punch"), 0.f);
			}
		}
		AddStep(TEXT("fight result"), 0.5f, [this]()
		{
			const bool bDown = TestPerp.IsValid() && (TestPerp->GetArrestState() == EFTOPerpArrest::Surrendered || (TestPerp->FindComponentByClass<UFTOKnockdownComponent>() && TestPerp->FindComponentByClass<UFTOKnockdownComponent>()->IsDown()));
			UE_LOG(LogFTO, Display, TEXT("SMOKE: fist fight with a suspect: %s (they fought back: %s, officer rocked %d time(s))."),
				bDown ? TEXT("put them down") : TEXT("STILL STANDING"), TestPerp.IsValid() && (TestPerp->IsFighting() || bDown) ? TEXT("yes") : TEXT("NO"), OfficerHits);
		});
		AddWait(TEXT("fighter on their knees"), 5.f, [this]()
		{
			const AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			return !TestPerp.IsValid() || (TestPerp->GetArrestState() == EFTOPerpArrest::Surrendered && Cop && Cop->IsReadyForAction());
		});
		AddStep(TEXT("cuff the fighter"), 0.f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (Cop && TestPerp.IsValid() && TestPerp->GetArrestState() == EFTOPerpArrest::Surrendered)
			{
				if (UFTOKnockdownComponent* Down = TestPerp->FindComponentByClass<UFTOKnockdownComponent>(); Down && Down->IsDown())
				{
					Down->Recover();
				}
				TestPerp->Interact(Cop);
			}
		});
		AddWait(TEXT("fighter cuffed"), 6.f, [this]() { return !TestPerp.IsValid() || TestPerp->IsActorBeingDestroyed(); });
		AddStep(TEXT("fighter result"), 0.f, [this]()
		{
			UE_LOG(LogFTO, Display, TEXT("SMOKE: the fighter: %s."), !TestPerp.IsValid() || TestPerp->IsActorBeingDestroyed() ? TEXT("cuffed") :
				*FString::Printf(TEXT("NOT CUFFED (%s)"), *StaticEnum<EFTOPerpArrest>()->GetNameStringByValue(int64(TestPerp->GetArrestState()))));
		});
		AddStep(TEXT("fight: grab"), 0.25f, [this]()
		{
			AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn());
			if (!Cop)
			{
				return;
			}
			TestPerp = StagePerp(TEXT("Shoplifting"), 300.f); // (no victim in the way, and a moment before they move: a vandal wanders off to the next bin)
			if (!TestPerp.IsValid())
			{
				UE_LOG(LogFTO, Display, TEXT("SMOKE: grab: NO SUSPECT."));
				return;
			}
			TestPerp->SetForcedResponse(EFTOArrestResponse::Comply);
			const FVector At = TestPerp->GetActorLocation();
			const FVector Back = TestPerp->GetActorForwardVector().GetSafeNormal2D();
			Cop->TeleportTo(At + Back * 95.f + FVector(0.f, 0.f, 4.f), (-Back).Rotation());
			if (APlayerController* PC = GetPC())
			{
				PC->SetControlRotation((-Back).Rotation());
			}
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Back);
			ViewFrom(ClearSpot(At + FVector(0.f, 0.f, 60.f), At + Back * 50.f + Side * 450.f + FVector(0.f, 0.f, 90.f)), At + Back * 50.f);
		});
		AddStep(TEXT("grab them"), 1.2f, [this]()
		{
			if (AFTOCharacter* Cop = Cast<AFTOCharacter>(GetPawn()))
			{
				Cop->TacklePressed(); // close and standing: a grab
			}
		});
		AddShot(TEXT("24c_throw"), 1.6f);
		AddStep(TEXT("grab result"), 0.f, [this]()
		{
			const bool bThrown = TestPerp.IsValid() && ((TestPerp->FindComponentByClass<UFTOKnockdownComponent>() && TestPerp->FindComponentByClass<UFTOKnockdownComponent>()->IsDown()) || TestPerp->GetArrestState() == EFTOPerpArrest::Surrendered);
			UE_LOG(LogFTO, Display, TEXT("SMOKE: grabbed a suspect and threw them: %s."), bThrown ? TEXT("over they went") : TEXT("NOT THROWN"));
			if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
		});
	}

	// Two players (-FTOSmokeRideAlong): the host parks in a cruiser and waits; the client (whose tour
	// runs ~20 s behind) hops in beside them, looks around from both cameras, and gets out again.
	if (FParse::Param(FCommandLine::Get(), TEXT("FTOSmokeRideAlong")))
	{
		if (GetNetMode() != NM_Client)
		{
			AddStep(TEXT("wait for a rider"), 0.5f, [this]()
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
			// However long the client takes to get here (its tour skips the host-only checks above).
			AddWait(TEXT("rider aboard"), 90.f, [this]() { return TestCruiser && TestCruiser->GetPassenger() != nullptr; });
			AddShot(TEXT("11_host_rider"), 2.f, false);
			AddShot(TEXT("11b_host_rider"), 1.f, false);

			// Once they're out again, a shoplifter turns up beside the client's officer for them to arrest (from their
			// own machine: the cuffing's a move the server locks them into).
			AddWait(TEXT("rider gets out"), 30.f, [this]() { return TestCruiser && !TestCruiser->GetPassenger(); });
			AddStep(TEXT("stage an arrest for the client"), 0.f, [this]()
			{
				AFTOGameMode* GM = GetAuthGameMode();
				const AFTOCharacter* Rider = nullptr;
				for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
				{
					if (It->IsValid() && !(*It)->IsLocalController())
					{
						Rider = Cast<AFTOCharacter>((*It)->GetPawn());
					}
				}
				if (!GM || !Rider || !TestCruiser)
				{
					return;
				}
				const FVector Fwd = Rider->GetActorForwardVector().GetSafeNormal2D();
				const FVector Out = (Rider->GetActorLocation() - TestCruiser->GetActorLocation()).GetSafeNormal2D();
				const FVector Feet = Rider->GetActorLocation() - FVector(0.f, 0.f, 96.f) + Fwd * 170.f + Out * 80.f;
				if (const AFTOIncident* Incident = GM->GetCrimeDirector()->SpawnIncidentAt(TEXT("Shoplifting"), FTransform((-Fwd).Rotation(), Feet), INDEX_NONE, true))
				{
					if (AFTOPerp* Perp = Incident->GetPerp())
					{
						Perp->SetForcedResponse(EFTOArrestResponse::Comply);
						RiderPerp = Perp;
					}
				}
				UE_LOG(LogFTO, Display, TEXT("SMOKE: staged a shoplifter for the client: %s."), RiderPerp.IsValid() ? TEXT("there") : TEXT("NOT THERE"));
			});
			AddWait(TEXT("client arrests"), 25.f, [this]() { return !RiderPerp.IsValid(); });
			AddStep(TEXT("client's arrest"), 0.f, [this]()
			{
				int32 Following = 0;
				for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
				{
					Following += It->GetEscort() && !It->GetEscort()->IsLocallyControlled() ? 1 : 0;
				}
				UE_LOG(LogFTO, Display, TEXT("SMOKE: the client's arrest: %s."), Following > 0 ? TEXT("cuffed, following the client's officer") : TEXT("NO ARRESTEE"));
			});

			// Then the client calls for backup and keys the radio: their callout and voice should reach us.
			AddStep(TEXT("listen to the radio"), 0.f, [this]()
			{
				SetHUDVisible(true);
				if (APlayerController* PC = GetPC()) { PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f); }
			});
			AddWait(TEXT("hear the client"), 40.f, [this]()
			{
				for (const APlayerState* PS : GetWorld()->GetGameState()->PlayerArray)
				{
					const AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS);
					if (Officer && !Officer->IsLocalOfficer() && Officer->IsOnRadio() && Officer->GetCallout().Callout == EFTOCallout::Backup)
					{
						return true;
					}
				}
				return false;
			});
			AddStep(TEXT("heard on the radio"), 0.f, [this]()
			{
				const AGameStateBase* GS = GetWorld()->GetGameState();
				UE_LOG(LogFTO, Display, TEXT("SMOKE: host's radio has %d officers on it."), GS ? GS->PlayerArray.Num() : -1);
				for (const APlayerState* PS : GS ? GS->PlayerArray : TArray<TObjectPtr<APlayerState>>())
				{
					const AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS);
					if (Officer && !Officer->IsLocalOfficer())
					{
						UE_LOG(LogFTO, Display, TEXT("SMOKE: host heard %s: callout %s (%.1f s ago), %s, their voice %s."), *Officer->GetCallsign(),
							*StaticEnum<EFTOCallout>()->GetNameStringByValue(int64(Officer->GetCallout().Callout)), Officer->GetCalloutAge(),
							Officer->IsOnRadio() ? TEXT("on air") : TEXT("off air"), Officer->HasRadioVoice() ? TEXT("through the radio filter") : TEXT("NOT SET UP"));
					}
				}
			});
			AddShot(TEXT("11g_host_hears_backup"), 1.f);
			// Stay up till the client's done its checks (it leaves when it's finished).
			AddWait(TEXT("client finishes"), 30.f, [this]()
			{
				const AGameStateBase* GS = GetWorld()->GetGameState();
				return !GS || GS->PlayerArray.Num() < 2;
			});
		}
		else
		{
			// Wait for the host to be parked up at the wheel in the precinct lot (they run extra checks first, and their
			// test drives across town have a driver too).
			AddWait(TEXT("find a ride"), 150.f, [this]()
			{
				const AFTOCityGenerator* City = GetCity();
				for (TActorIterator<AFTOCruiser> It(GetWorld()); It && City; ++It)
				{
					if (It->HasDriver() && It->GetDriver() != GetPawn() && !It->GetPassenger() && It->GetVelocity().Size() < 20.f &&
						FVector::Dist2D(It->GetActorLocation(), City->GetPrecinctLocation()) < 3000.f)
					{
						return true;
					}
				}
				return false;
			});
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
			// The host puts a shoplifter beside us: turn to them and press E, the way a player would. The server steps
			// us in behind them for the cuffs (the move replicates back here).
			AddWait(TEXT("wait for a suspect"), 150.f, [this]()
			{
				const AFTOPerp* Perp = FindNearestPerp(TEXT("Shoplifting"));
				return Perp && GetPawn() && FVector::Dist2D(Perp->GetActorLocation(), GetPawn()->GetActorLocation()) < 500.f;
			});
			AddStep(TEXT("client faces them"), 0.6f, [this]()
			{
				AFTOCharacter* Me = Cast<AFTOCharacter>(GetPawn());
				const AFTOPerp* Perp = FindNearestPerp(TEXT("Shoplifting"));
				if (Me && Perp)
				{
					Me->SetActorRotation(FRotator(0.f, (Perp->GetActorLocation() - Me->GetActorLocation()).Rotation().Yaw, 0.f));
				}
			});
			AddStep(TEXT("client presses E"), 0.9f, [this]()
			{
				if (AFTOCharacter* Me = Cast<AFTOCharacter>(GetPawn())) { Me->PressInteract(); }
			});
			AddStep(TEXT("client cuffing"), 0.f, [this]()
			{
				const AFTOCharacter* Me = Cast<AFTOCharacter>(GetPawn());
				const bool bCuffing = Me && Me->GetSyncedAction() == EFTOAnimAction::Cuffing;
				UE_LOG(LogFTO, Display, TEXT("SMOKE: client arresting: %s (can move: %s)."), bCuffing ? TEXT("stepped in to cuff them") : TEXT("NOT CUFFING"),
					Me && Me->GetCharacterMovement()->MovementMode == MOVE_None ? TEXT("no") : TEXT("YES"));
			});
			AddShot(TEXT("12_client_cuffing"), 2.6f);
			AddStep(TEXT("client's arrestee"), 0.f, [this]()
			{
				int32 Mine = 0;
				for (TActorIterator<AFTOArrestee> It(GetWorld()); It; ++It)
				{
					Mine += It->GetEscort() == GetPawn() ? 1 : 0;
				}
				const AFTOCharacter* Me = Cast<AFTOCharacter>(GetPawn());
				UE_LOG(LogFTO, Display, TEXT("SMOKE: client arrest done: %s, and free to move: %s."), Mine > 0 ? TEXT("a cuffed suspect is following me") : TEXT("NO ARRESTEE"),
					Me && Me->GetCharacterMovement()->MovementMode == MOVE_Walking ? TEXT("yes") : TEXT("NO"));
			});
			// A client draws the taser and fires down the street (predicted here, flown for real on the server).
			AddStep(TEXT("client draws"), 0.6f, [this]()
			{
				if (AFTOCharacter* Me = Cast<AFTOCharacter>(GetPawn())) { Me->SelectSlot(0); }
			});
			AddStep(TEXT("client fires"), 0.f, [this]()
			{
				if (AFTOCharacter* Me = Cast<AFTOCharacter>(GetPawn()))
				{
					Me->FirePressed();
					const UFTOBallistics* Ballistics = UFTOBallistics::Get(GetWorld());
					UE_LOG(LogFTO, Display, TEXT("SMOKE: client fired the %s: %d rounds in flight, %d left."), *FTOWeapons::DisplayName(Me->GetDrawnWeapon()).ToString(),
						Ballistics ? Ballistics->NumInFlight() : -1, Me->GetClip(0));
				}
			});
			AddShot(TEXT("11d_client_fires"), 0.f);
			// Call for backup and stay on air a while (the host is listening for it).
			AddStep(TEXT("client radio"), 1.5f, [this]()
			{
				if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetPC()))
				{
					PC->RadioPressed();
					PC->FTOCallout(TEXT("Backup"));
					for (const APlayerState* PS : GetWorld()->GetGameState()->PlayerArray)
					{
						const AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS);
						if (Officer && !Officer->IsLocalOfficer())
						{
							UE_LOG(LogFTO, Display, TEXT("SMOKE: client hears %s's voice %s."), *Officer->GetCallsign(), Officer->HasRadioVoice() ? TEXT("through the radio filter") : TEXT("NOT SET UP"));
						}
					}
				}
			});
			AddShot(TEXT("11f_client_on_radio"), 6.f);
			AddStep(TEXT("client off the radio"), 5.f, [this]()
			{
				if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(GetPC())) { PC->RadioReleased(); }
			});
			// Whatever the host broke (windows, a hydrant, a lamp post) is broken here too.
			AddStep(TEXT("client sees the damage"), 0.f, [this]()
			{
				const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
				UE_LOG(LogFTO, Display, TEXT("SMOKE: client sees %d broken things in the city."), Wreckage ? Wreckage->NumApplied() : -1);
				const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
				UE_LOG(LogFTO, Display, TEXT("SMOKE: client sees this shift's set piece: %s."), GS && GS->GetSetPiece() != NAME_None ? *GS->GetSetPiece().ToString() : TEXT("NONE"));
			});
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

	// One step per frame, and never while shaders are still compiling (the shot would be grey). A waiting step moves
	// on as soon as what it's waiting for happens.
	const bool bWaitOver = Now >= NextStepTime || (WaitUntil && WaitUntil());
	if (Steps.IsValidIndex(NextStep) && bWaitOver && AreShadersReady())
	{
		const FStep& Step = Steps[NextStep];
		UE_LOG(LogFTO, Display, TEXT("SMOKE: %s"), *Step.Name);
		Step.Action();
		NextStepTime = Now + Step.Delay;
		WaitUntil = Step.Until;
		++NextStep;
	}
}
