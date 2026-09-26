#include "City/FTOOccupant.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameMode.h"
#include "Core/FTOPlayerController.h"
#include "Crime/FTOCrimeDirector.h"
#include "Crime/FTOIncident.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Physics/FTOKnockdownComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "FTO.h"

namespace
{
	template <int32 N>
	FString Pick(FRandomStream& Rng, const TCHAR* const (&Lines)[N])
	{
		return Lines[Rng.RandRange(0, N - 1)];
	}

	const TCHAR* const ShopTalk[] =
	{
		TEXT("\"Welcome in! Everything's 10% off, except whatever you wanted.\""),
		TEXT("\"If you see a guy with fourteen chickens under his coat, that's Gary.\""),
		TEXT("\"We don't sell donuts. Please stop asking.\""),
	};
	const TCHAR* const DinerTalk[] =
	{
		TEXT("\"Coffee's on the house for the boys and girls in blue.\""),
		TEXT("\"Donut? Don't answer, I can see it in your eyes.\""),
		TEXT("\"Special today is the pie. It's always the pie.\""),
	};
	const TCHAR* const BarTalk[] =
	{
		TEXT("\"What'll it be, officer? Oh. Right. On duty.\""),
		TEXT("\"Trivia night's Thursday. It gets violent.\""),
		TEXT("\"Nobody's been served more than twelve, I promise.\""),
	};
	const TCHAR* const OfficeTalk[] =
	{
		TEXT("\"Is this about the printer? It's always about the printer.\""),
		TEXT("\"I've got a meeting about meetings in five.\""),
		TEXT("\"Someone ate my labelled yoghurt. Can you investigate?\""),
	};
	const TCHAR* const WarehouseTalk[] =
	{
		TEXT("\"Mind the forklift. It's got a mind of its own.\""),
		TEXT("\"Everything here is legit. Mostly boxes. Legit boxes.\""),
	};
	const TCHAR* const BankTalk[] =
	{
		TEXT("\"Please don't say the H-word in here, officer.\""),
		TEXT("\"Would you like to open a savings account? We have lollipops.\""),
	};
	const TCHAR* const OfficerTalk[] =
	{
		TEXT("\"Holding cells are through the right-hand door. Bring 'em in, we'll book 'em.\""),
		TEXT("\"Armory's straight through the middle. Sign for anything you take.\""),
		TEXT("\"Coffee's fresh. Well. Fresh-ish.\""),
	};
	const TCHAR* const CustomerTalk[] =
	{
		TEXT("\"Just browsing, officer.\""),
		TEXT("\"The pie here should be illegal. In a good way.\""),
		TEXT("\"I only come here for the free napkins.\""),
		TEXT("\"Is it true you get a free hat?\""),
	};
	const TCHAR* const ResidentTalk[] =
	{
		TEXT("\"Is there a problem, officer? Wipe your feet.\""),
		TEXT("\"We didn't order a cop. Did we order a cop?\""),
		TEXT("\"If this is about the bagpipes, that was the neighbours.\""),
	};
	const TCHAR* const InnocentCrook[] =
	{
		TEXT("\"Me? I just like stripes, officer.\""),
		TEXT("\"This mask is for my allergies. Very specific allergies.\""),
		TEXT("\"Nice day to not be doing any crimes, isn't it?\""),
	};
	const TCHAR* const GuiltyCrook[] =
	{
		TEXT("\"Okay, okay! The gnomes are in the back. All forty of them.\""),
		TEXT("\"Fine! It was me! The stolen spoons are in my socks!\""),
		TEXT("\"You'll never take me al- oh, you've taken me. Fair enough.\""),
	};

	/** Standing folk who wander from their spot now and then. */
	bool Fidgets(EFTOOccupantRole Who) { return Who == EFTOOccupantRole::Customer || Who == EFTOOccupantRole::Resident || Who == EFTOOccupantRole::Crook; }
}

AFTOOccupant::AFTOOccupant()
{
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Officer(TEXT("/Game/FTO/Characters/Officer/SK_Officer.SK_Officer"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Crook(TEXT("/Game/FTO/Characters/Civilians/SK_Suspect.SK_Suspect"));
	OfficerLook = Officer.Object;
	CrookLook = Crook.Object;

	// Indoors, half-hidden behind walls: animate only when seen, and less often further away.
	Body->bEnableUpdateRateOptimizations = true;
}

void AFTOOccupant::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOOccupant, OccupantRole);
	DOREPLIFETIME(AFTOOccupant, BuildingType);
	DOREPLIFETIME(AFTOOccupant, PostAction);
	DOREPLIFETIME(AFTOOccupant, Reaction);
	DOREPLIFETIME(AFTOOccupant, bSeated);
	DOREPLIFETIME(AFTOOccupant, bQuestioned);
}

void AFTOOccupant::Settle(int32 Index, EFTOBuildingType Type, const FFTOSpot& InSpot, EFTOOccupantRole InRole, int32 InSeed)
{
	check(HasAuthority());
	BuildingIndex = Index;
	BuildingType = Type;
	Spot = InSpot;
	OccupantRole = InRole;
	PostAction = Spot.Action;
	bSeated = Spot.bSeated;
	Rng.Initialize(InSeed);
	LookSeed = InSeed;
	ApplyLook();

	TeleportAndHold(SpotLocation());
	SetActorRotation(FRotator(0.f, SpotYaw(), 0.f));
	FaceYaw(SpotYaw());
	ScheduleFidget();
}

void AFTOOccupant::ApplyLook()
{
	switch (OccupantRole)
	{
	case EFTOOccupantRole::Officer:
		if (OfficerLook)
		{
			// Precinct staff in plain navy; the players' officers wear their badge colours.
			Body->SetSkeletalMeshAsset(OfficerLook);
			PaintBody(FLinearColor(0.18f, 0.24f, 0.42f));
			return;
		}
		break;
	case EFTOOccupantRole::Crook:
		if (CrookLook)
		{
			Body->SetSkeletalMeshAsset(CrookLook);
			PaintBody(FLinearColor::White);
			return;
		}
		break;
	default:
		break;
	}
	Super::ApplyLook();
}

EFTOAnimAction AFTOOccupant::GetAnimAction() const
{
	if (Knockdown && Knockdown->IsDown())
	{
		return EFTOAnimAction::None;
	}
	if (bHandsUp)
	{
		return bSeated ? EFTOAnimAction::SitHandsUp : EFTOAnimAction::HandsUp;
	}
	if (Reaction != EFTOAnimAction::None)
	{
		if (!bSeated)
		{
			return Reaction;
		}
		// Sat down, the arms say it all.
		const bool bArmsUp = Reaction == EFTOAnimAction::HandsUp || Reaction == EFTOAnimAction::Cower || Reaction == EFTOAnimAction::Cheer;
		return bArmsUp ? EFTOAnimAction::SitHandsUp : PostAction;
	}
	if (GetCurrentSpeed() > 0.f)
	{
		return EFTOAnimAction::None; // walking: let the legs walk
	}
	if (bChatting && !bSeated)
	{
		return EFTOAnimAction::Talk;
	}
	return PostAction;
}

void AFTOOccupant::FaceOfficer(const AActor* Officer)
{
	// Sat down, we stay sat (and facing the table).
	if (!bSeated)
	{
		Super::FaceOfficer(Officer);
	}
}

void AFTOOccupant::React(EFTOAnimAction Action, const FVector& Toward)
{
	check(HasAuthority());
	if (Reaction == Action)
	{
		return;
	}
	Reaction = Action;
	if (Action == EFTOAnimAction::None)
	{
		Resume();
		return;
	}
	GetWorldTimerManager().ClearTimer(ResumeTimer);
	bChatting = false;
	bHandsUp = false;
	if (!bSeated)
	{
		Hold();
		FaceToward(Toward);
	}
}

void AFTOOccupant::Confront(const FVector& Where, const FVector& Toward, EFTOAnimAction Action)
{
	check(HasAuthority());
	if (bConfronting && Reaction == Action)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(ResumeTimer);
	bConfronting = true;
	bSeated = false;
	bChatting = false;
	bHandsUp = false;
	Reaction = Action;
	TeleportAndHold(Where + FVector(0.f, 0.f, HalfHeight));
	SetActorRotation(FRotator(0.f, (Toward - Where).Rotation().Yaw, 0.f));
	FaceToward(Toward);
}

void AFTOOccupant::Resume()
{
	bChatting = false;
	bHandsUp = false;
	if (Reaction != EFTOAnimAction::None && !bConfronting)
	{
		return; // still hunkered down: the trouble isn't over
	}
	if (bConfronting)
	{
		bConfronting = false;
		Reaction = EFTOAnimAction::None;
	}
	bSeated = Spot.bSeated;
	bWandered = false;

	// Back to our spot: a few steps if we're standing nearby, else straight there (back in our seat, or up
	// off the floor after a tumble).
	const FVector Home = SpotLocation();
	const float Distance = FVector::Dist(GetActorLocation(), Home);
	if (!bSeated && Distance > 5.f && Distance < 150.f)
	{
		MoveTo(Home, 90.f);
	}
	else
	{
		TeleportAndHold(Home);
		SetActorRotation(FRotator(0.f, SpotYaw(), 0.f));
		FaceYaw(SpotYaw());
	}
}

void AFTOOccupant::OnArrived()
{
	// Deliberately not the pedestrian's "on to the next corner".
	FaceYaw(bWandered ? SpotYaw() + Rng.FRandRange(-60.f, 60.f) : SpotYaw());
}

void AFTOOccupant::ScheduleFidget()
{
	if (Fidgets(OccupantRole) && !Spot.bSeated)
	{
		GetWorldTimerManager().SetTimer(FidgetTimer, this, &AFTOOccupant::Fidget, Rng.FRandRange(4.f, 11.f), false);
	}
}

void AFTOOccupant::Fidget()
{
	ScheduleFidget();
	if (bSeated || bConfronting || bChatting || bHandsUp || Reaction != EFTOAnimAction::None || (Knockdown && Knockdown->IsDown()) || GetCurrentSpeed() > 0.f)
	{
		return;
	}
	if (bWandered)
	{
		bWandered = false;
		MoveTo(SpotLocation(), 70.f);
		return;
	}

	// A step or two along the aisle, if nothing's in the way.
	const FVector Right = Spot.Transform.GetRotation().GetRightVector();
	const FVector Forward = Spot.Transform.GetRotation().GetForwardVector();
	const FVector From = GetActorLocation();
	const FVector To = SpotLocation() + Right * Rng.FRandRange(-110.f, 110.f) - Forward * Rng.FRandRange(0.f, 40.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOOccupantFidget), false, this);
	FHitResult Hit;
	const FVector Lift(0.f, 0.f, 25.f); // clear of rugs and doormats
	if (!GetWorld()->SweepSingleByChannel(Hit, From + Lift, To + Lift, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(22.f, 60.f), Params))
	{
		bWandered = true;
		MoveTo(To, 70.f);
	}
}

FText AFTOOccupant::GetInteractPrompt(const AFTOCharacter* Officer) const
{
	switch (OccupantRole)
	{
	case EFTOOccupantRole::Crook:
		return bQuestioned ? INVTEXT("Chat with shady character") : INVTEXT("Question suspicious person");
	case EFTOOccupantRole::Officer:
		return INVTEXT("Chat with fellow officer");
	case EFTOOccupantRole::Resident:
		return INVTEXT("Chat with resident");
	case EFTOOccupantRole::Customer:
		return INVTEXT("Chat with customer");
	default:
		break;
	}
	switch (BuildingType)
	{
	case EFTOBuildingType::Shop:      return INVTEXT("Chat with the shopkeeper");
	case EFTOBuildingType::Diner:     return INVTEXT("Chat with the waiter");
	case EFTOBuildingType::Bar:       return INVTEXT("Chat with the bartender");
	case EFTOBuildingType::Office:    return INVTEXT("Chat with office worker");
	case EFTOBuildingType::Warehouse: return INVTEXT("Chat with warehouse worker");
	case EFTOBuildingType::Bank:      return INVTEXT("Chat with the teller");
	default:                          return INVTEXT("Chat");
	}
}

FString AFTOOccupant::GetSmallTalk()
{
	switch (OccupantRole)
	{
	case EFTOOccupantRole::Crook:    return Pick(Rng, InnocentCrook);
	case EFTOOccupantRole::Officer:  return Pick(Rng, OfficerTalk);
	case EFTOOccupantRole::Resident: return Pick(Rng, ResidentTalk);
	case EFTOOccupantRole::Customer: return Pick(Rng, CustomerTalk);
	default: break;
	}
	switch (BuildingType)
	{
	case EFTOBuildingType::Shop:      return Pick(Rng, ShopTalk);
	case EFTOBuildingType::Diner:     return Pick(Rng, DinerTalk);
	case EFTOBuildingType::Bar:       return Pick(Rng, BarTalk);
	case EFTOBuildingType::Office:    return Pick(Rng, OfficeTalk);
	case EFTOBuildingType::Warehouse: return Pick(Rng, WarehouseTalk);
	case EFTOBuildingType::Bank:      return Pick(Rng, BankTalk);
	default:                          return Super::GetSmallTalk();
	}
}

void AFTOOccupant::Interact(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (OccupantRole == EFTOOccupantRole::Crook && !bQuestioned && Reaction == EFTOAnimAction::None)
	{
		Question(Officer);
		return;
	}
	Super::Interact(Officer);
}

void AFTOOccupant::Question(AFTOCharacter* Officer)
{
	AFTOPlayerController* PC = Officer ? Cast<AFTOPlayerController>(Officer->GetController()) : nullptr;
	if (!PC)
	{
		return;
	}
	bQuestioned = true;
	Officer->PlayTimedAction(EFTOAnimAction::Interact, 1.5f);

	// Two in three crack under the pressure of a firm, fair question.
	AFTOGameMode* GM = GetWorld()->GetAuthGameMode<AFTOGameMode>();
	if (GM && Rng.FRand() < 0.65f)
	{
		// Crooks lurk on their feet (never in a seat), so the perp can stand right where they are.
		const FTransform Where(FRotator(0.f, (Officer->GetActorLocation() - GetActorLocation()).Rotation().Yaw, 0.f), GetActorLocation() - FVector(0.f, 0.f, HalfHeight));
		if (AFTOIncident* Caught = GM->GetCrimeDirector()->SpawnIncidentAt(TEXT("StolenGoods"), Where, BuildingIndex, true))
		{
			Caught->ReportByOfficer();
			PC->ClientToast(FText::FromString(Pick(Rng, GuiltyCrook)), FLinearColor(1.f, 0.6f, 0.3f));
			UE_LOG(LogFTO, Log, TEXT("A questioned crook confessed in building %d."), BuildingIndex);
			// The perp standing in the incident takes it from here.
			Destroy();
			return;
		}
	}

	Hold();
	bChatting = true;
	FaceOfficer(Officer);
	GetWorldTimerManager().SetTimer(ResumeTimer, this, &AFTOPedestrian::Resume, 2.5f, false);
	PC->ClientToast(FText::FromString(Pick(Rng, InnocentCrook)), FLinearColor::White);
}
