#include "Crime/FTOPerp.h"
#include "Crime/FTOCrimeExtra.h"
#include "Combat/FTOFighting.h"
#include "Crime/FTOIncident.h"
#include "Crime/FTOGraffitiTag.h"
#include "Art/FTOArt.h"
#include "City/FTOCityGenerator.h"
#include "City/FTOLift.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Physics/FTODestruction.h"
#include "Physics/FTOKnockdownComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/FTOBallistics.h"
#include "Scoring/FTOScoring.h"

namespace
{
	/** Seconds into the cuffing before the hands come down off the head and go behind the back. */
	constexpr float HandsBehindAfter = 0.7f;
	constexpr int32 NumScufflePuffs = 7;

	const FLinearColor ArrestBlue(0.6f, 0.85f, 1.f);
	const FLinearColor Warning(1.f, 0.6f, 0.25f);
	const FLinearColor GoodNews(0.4f, 1.f, 0.5f);
	const FLinearColor BadNews(1.f, 0.4f, 0.3f);

	/** Crimes where the perp waves a gun about. */
	bool IsHoldUp(FName Crime)
	{
		return Crime == TEXT("ArmedRobbery") || Crime == TEXT("BankHeist") || Crime == TEXT("HostageSituation") || Crime == TEXT("Standoff");
	}

	/** What they're up to before the police arrive. */
	EFTOAnimAction DeedFor(FName Crime)
	{
		if (Crime == TEXT("BarFight") || Crime == TEXT("Riot"))
		{
			return EFTOAnimAction::FightIdle; // fists up, between swings at the other brawlers (TickBrawl)
		}
		if (Crime == TEXT("Vandalism"))
		{
			return EFTOAnimAction::Smash;
		}
		if (Crime == TEXT("Graffiti"))
		{
			return EFTOAnimAction::Spray;
		}
		if (Crime == TEXT("NoiseComplaint") || Crime == TEXT("Jaywalking") || Crime == TEXT("Yodelling"))
		{
			return EFTOAnimAction::Dance; // the party, or showing off in the middle of the road
		}
		if (Crime == TEXT("Mime"))
		{
			return EFTOAnimAction::Wave; // pressing on the walls of the invisible box
		}
		if (Crime == TEXT("Pigeons"))
		{
			return EFTOAnimAction::Point; // "fly, my pretties!"
		}
		if (Crime == TEXT("Gnomes"))
		{
			return EFTOAnimAction::Grab;
		}
		if (Crime == TEXT("Mugging"))
		{
			return EFTOAnimAction::Point; // "hand it over!"
		}
		if (Crime == TEXT("DomesticDispute") || Crime == TEXT("CatInTree"))
		{
			return EFTOAnimAction::Talk; // rowing, or pleading with the cat
		}
		if (Crime == TEXT("Shoplifting") || Crime == TEXT("Burglary") || Crime == TEXT("StolenGoods") || Crime == TEXT("PettyTheft"))
		{
			return EFTOAnimAction::Grab; // rummaging, stuffing a sack
		}
		if (Crime == TEXT("TerrorPlot"))
		{
			return EFTOAnimAction::Work; // fiddling with a ticking thing
		}
		if (IsHoldUp(Crime))
		{
			return EFTOAnimAction::None; // the gun says it all
		}
		return EFTOAnimAction::Interact; // squinting at a map, up to no good
	}

	/** A drunk's ramblings, the one friendly answer, two that wind them up, and what they say to the friendly one. */
	struct FDrunkLine
	{
		const TCHAR* Says;
		const TCHAR* Right;
		const TCHAR* Wrong[2];
		const TCHAR* Reply;
	};
	const FDrunkLine DrunkLines[] =
	{
		{ TEXT("Offisher! You're my besht friend. Have I told you that?"), TEXT("Course I am, mate. Let's get you some water."),
			{ TEXT("Back off or you're nicked!"), TEXT("I don't have friends. Only the badge.") }, TEXT("\"Water. Yesh. Water's my other besht friend.\"") },
		{ TEXT("I'm not drunk. I'm... *hic*... emotionally hydrated."), TEXT("Sounds like a long night. Where's home?"),
			{ TEXT("Walk this line. Now."), TEXT("Emotionally hydrated? I'm writing that down.") }, TEXT("\"Home's... that way. Or that way. Definitely a way.\"") },
		{ TEXT("That lamp post looked at me funny!"), TEXT("Let's leave the lamp post be. It's had a long day too."),
			{ TEXT("Touch it again and you're in the cells!"), TEXT("Which one? I'll arrest it.") }, TEXT("\"...You're right. Sorry, lamp post.\"") },
		{ TEXT("I can sing! Wanna hear? Wanna HEAR?!"), TEXT("Maybe later. How about we find you a taxi?"),
			{ TEXT("Go on then, belt it out!"), TEXT("Absolutely not. Nobody wants that.") }, TEXT("\"A taxi! With a radio! I'll sing in the taxi.\"") },
		{ TEXT("Nobody understands me! NOBODY!"), TEXT("I'm listening. Let's sit down a minute."),
			{ TEXT("Pull yourself together."), TEXT("Have you tried being understandable?") }, TEXT("\"...Thanks. You're alright, you are.\"") },
	};

	float ServerNow(const UWorld* World)
	{
		const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
		return GS ? GS->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.f);
	}

	AFTOPlayerController* PCOf(const AFTOCharacter* Officer)
	{
		return Officer ? Cast<AFTOPlayerController>(Officer->GetController()) : nullptr;
	}
}

AFTOPerp::AFTOPerp()
{
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Suspect(TEXT("/Game/FTO/Characters/Civilians/SK_Suspect.SK_Suspect"));
	SuspectLook = Suspect.Object;

	// The pistol goes in the right hand every frame (see FTOWeapons::HoldInHand), so it's placed in world space.
	Gun = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gun"));
	Gun->SetupAttachment(Capsule);
	Gun->SetUsingAbsoluteLocation(true);
	Gun->SetUsingAbsoluteRotation(true);
	Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Gun->SetCastShadow(false);
	Gun->SetVisibility(false);

	// The swag bag, hanging from the right hand (shown once they've got their hands on something).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SackMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Loot = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Loot"));
	Loot->SetupAttachment(Body); // onto the hand once there's a body to find it on (OnRep_Loot)
	Loot->SetStaticMesh(SackMesh.Object);
	Loot->SetRelativeLocation(FVector(0.f, 0.f, -20.f));
	Loot->SetUsingAbsoluteRotation(true);
	Loot->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.38f));
	Loot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Loot->SetVisibility(false);

	// Perps stay put and turn to face officers quickly.
	TurnRate = 720.f;
}

void AFTOPerp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOPerp, Incident);
	DOREPLIFETIME(AFTOPerp, bCriminal);
	DOREPLIFETIME(AFTOPerp, bArmed);
	DOREPLIFETIME(AFTOPerp, bStreetClothes);
	DOREPLIFETIME(AFTOPerp, bDisguised);
	DOREPLIFETIME(AFTOPerp, bHasLoot);
	DOREPLIFETIME(AFTOPerp, bShooting);
	DOREPLIFETIME(AFTOPerp, AimPitch);
	DOREPLIFETIME(AFTOPerp, ArrestState);
	DOREPLIFETIME(AFTOPerp, Arrester);
	DOREPLIFETIME(AFTOPerp, StruggleMeter);
	DOREPLIFETIME(AFTOPerp, CuffStartTime);
	DOREPLIFETIME(AFTOPerp, DrunkStage);
	DOREPLIFETIME(AFTOPerp, DrunkTemper);
}

void AFTOPerp::Setup(AFTOIncident* InIncident, bool bInCriminal, bool bInArmed, int32 Seed)
{
	check(HasAuthority());
	Incident = InIncident;
	bCriminal = bInCriminal;
	bArmed = bInArmed;
	Rng.Initialize(Seed);
	LookSeed = Seed;
	const FFTOIncidentInfo Info = InIncident ? InIncident->GetInfo() : FFTOIncidentInfo();
	bStreetClothes = Info.bStreetClothes;
	ApplyLook();
	OnRep_Armed();

	Home = GetActorLocation();
	HomeYaw = GetActorRotation().Yaw;
	TeleportAndHold(Home);
	FaceYaw(HomeYaw);

	// Some make a run for it when the police turn up.
	bWillRun = bCriminal && Rng.FRand() < Info.EscapeChance;
	BeginDeed();
	if (bCriminal && Info.TemplateId == TEXT("Burglary"))
	{
		HideInBuilding();
	}
}

FString AFTOPerp::DescribeSuspect() const
{
	FString Look = bStreetClothes || bDisguised ? DescribeLook() : FString(TEXT("striped jumper and a mask"));
	if (bDisguised)
	{
		Look += TEXT(" (ditched the striped jumper)");
	}
	if (bHasLoot)
	{
		Look += TEXT(", carrying a sack");
	}
	return Look;
}

void AFTOPerp::OnRep_Loot()
{
	if (bHasLoot && !Loot->IsVisible())
	{
		FTOArt::ApplyColor(Loot, BaseMaterial, FLinearColor(0.42f, 0.3f, 0.16f));
		Loot->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform, TEXT("hand_r"));
	}
	Loot->SetVisibility(bHasLoot);
}

void AFTOPerp::ApplyLook()
{
	if (bCriminal && SuspectLook && !bStreetClothes && !bDisguised)
	{
		Body->SetSkeletalMeshAsset(SuspectLook);
		PaintBody(FLinearColor::White);
		return;
	}
	Super::ApplyLook();
}

void AFTOPerp::OnRep_Armed()
{
	Gun->SetStaticMesh(bArmed ? FTOWeapons::Mesh(EFTOWeapon::Pistol) : nullptr);
}

AFTOCityGenerator* AFTOPerp::FindCity()
{
	if (!City)
	{
		for (TActorIterator<AFTOCityGenerator> It(GetWorld()); It; ++It)
		{
			City = *It;
			break;
		}
	}
	return City;
}

// ------------------------------------------------------------------------------------------
// Animation
// ------------------------------------------------------------------------------------------

EFTOAnimAction AFTOPerp::GetAnimAction() const
{
	if (Knockdown && Knockdown->IsDown())
	{
		return EFTOAnimAction::None;
	}
	switch (ArrestState)
	{
	case EFTOPerpArrest::Cuffing:
		// Hands on head while the officer steps in, then wrists behind the back for the cuffs.
		return ServerNow(GetWorld()) - CuffStartTime < HandsBehindAfter ? EFTOAnimAction::Kneel : EFTOAnimAction::Cuffed;
	case EFTOPerpArrest::Struggling:
		return EFTOAnimAction::Struggle;
	case EFTOPerpArrest::Fighting:
		// Fists up; shuffling in when out of reach (the punches themselves play over this).
		return Knockdown && Knockdown->IsDazed() ? EFTOAnimAction::Dazed : (GetCurrentSpeed() > 1.f ? EFTOAnimAction::FightStepFwd : EFTOAnimAction::FightIdle);
	case EFTOPerpArrest::Surrendered:
		// Seeing stars first, if they were put on the floor.
		return Knockdown && Knockdown->IsDazed() ? EFTOAnimAction::Dazed : EFTOAnimAction::Kneel;
	case EFTOPerpArrest::Fleeing:
		return bHandsUp ? EFTOAnimAction::HandsUp : EFTOAnimAction::None; // a whistle stops them for a moment
	case EFTOPerpArrest::Hiding:
		// Just another face in the crowd (in a pickpocket's crowd, idling like the rest of them).
		if (Incident && Incident->IsCrowd() && (bChatting || bHandsUp || bBeingSearched))
		{
			return AFTOPedestrian::GetAnimAction(); // talking to the police just like the bystanders do
		}
		if (!bHandsUp && !bChatting && Incident && Incident->IsCrowd() && GetCurrentSpeed() < 1.f)
		{
			static const EFTOAnimAction Idles[] = { EFTOAnimAction::Phone, EFTOAnimAction::IdleBored, EFTOAnimAction::Talk, EFTOAnimAction::None };
			return Idles[uint32(LookSeed) % UE_ARRAY_COUNT(Idles)];
		}
		return bHandsUp ? EFTOAnimAction::HandsUp : (bChatting ? EFTOAnimAction::Interact : EFTOAnimAction::None);
	default:
		break;
	}
	if (Knockdown && Knockdown->IsDazed())
	{
		return EFTOAnimAction::Dazed;
	}
	if (bShooting)
	{
		return EFTOAnimAction::None; // the gun's up: the aim layer does the rest
	}
	if (bHandsUp)
	{
		return EFTOAnimAction::HandsUp;
	}
	if (!Incident)
	{
		return EFTOAnimAction::None;
	}
	// Crouched out of sight somewhere in the building.
	if (Incident->IsHiddenInside())
	{
		return GetCurrentSpeed() > 1.f ? EFTOAnimAction::None : EFTOAnimAction::Cower;
	}
	if (IsDrunkCall())
	{
		// Swaying about, then rambling at the officers; talked round, off home.
		if (DrunkStage >= DrunkStages)
		{
			return EFTOAnimAction::Wave;
		}
		if (GetCurrentSpeed() > 1.f)
		{
			return EFTOAnimAction::None;
		}
		return bChatting || Incident->GetState() == EFTOIncidentState::Responding ? EFTOAnimAction::Talk : EFTOAnimAction::Dance;
	}
	switch (Incident->GetState())
	{
	case EFTOIncidentState::Unreported:
	case EFTOIncidentState::Reported:
		// On the way to the next shelf, bin or kerb: just walking.
		return GetCurrentSpeed() > 1.f ? EFTOAnimAction::None : DeedFor(Incident->GetInfo().TemplateId);
	case EFTOIncidentState::Responding:
	case EFTOIncidentState::Resolved:
		// A brawl goes on till there are enough officers to pull them apart.
		if (Incident->IsBrawl() && Incident->GetOfficersOnScene() < Incident->GetMinCrew() && !Incident->IsSubdued())
		{
			return GetCurrentSpeed() > 1.f ? EFTOAnimAction::None : EFTOAnimAction::FightIdle;
		}
		return bCriminal ? EFTOAnimAction::HandsUp : EFTOAnimAction::Talk; // it's a fair cop / "thank goodness you're here"
	default:
		return EFTOAnimAction::None;
	}
}

EFTOAimPose AFTOPerp::GetAimPose() const
{
	if (!bArmed || bHandsUp || ArrestState != EFTOPerpArrest::None || (Knockdown && (Knockdown->IsDown() || Knockdown->IsDazed())))
	{
		return EFTOAimPose::None;
	}
	// Levelled at the officers, or at whoever's at the counter before they turn up.
	const bool bHoldingUp = Incident && (Incident->GetState() == EFTOIncidentState::Unreported || Incident->GetState() == EFTOIncidentState::Reported);
	return bShooting || bHoldingUp ? EFTOAimPose::Pistol : EFTOAimPose::None;
}

float AFTOPerp::GetCuffProgress() const
{
	return ArrestState == EFTOPerpArrest::Cuffing ? FMath::Clamp((ServerNow(GetWorld()) - CuffStartTime) / FMath::Max(0.1f, CuffSeconds), 0.f, 1.f) : 0.f;
}

void AFTOPerp::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		TickDeed(DeltaSeconds);
		TickShooting(DeltaSeconds);
		TickArrest(DeltaSeconds);
	}

	// Hot dog day.
	UpdateHotDogSuit();

	// The pistol: in hand and pointing where they aim, or tossed away once they give up.
	const bool bShowGun = GetAimPose() != EFTOAimPose::None;
	if (Gun->IsVisible() != bShowGun)
	{
		Gun->SetVisibility(bShowGun);
	}
	if (bShowGun)
	{
		FTOWeapons::HoldInHand(Gun, Body, EFTOAimPose::Pistol);
	}

	UpdateScuffleCloud(DeltaSeconds);
}

void AFTOPerp::UpdateScuffleCloud(float DeltaSeconds)
{
	const bool bShow = ArrestState == EFTOPerpArrest::Struggling && IsValid(Arrester);
	if (!bShow && ScuffleCloud.IsEmpty())
	{
		return;
	}
	if (ScuffleCloud.IsEmpty())
	{
		// Dust kicked up round a scuffle, made the first time one breaks out.
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, FTOArt::BaseMaterialPath);
		for (int32 i = 0; i < NumScufflePuffs; ++i)
		{
			UStaticMeshComponent* Puff = NewObject<UStaticMeshComponent>(this);
			Puff->SetupAttachment(Capsule);
			Puff->SetStaticMesh(Sphere);
			Puff->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Puff->SetCastShadow(false);
			Puff->SetUsingAbsoluteLocation(true);
			Puff->SetUsingAbsoluteScale(true);
			Puff->SetVisibility(false);
			Puff->RegisterComponent();
			FTOArt::ApplyColor(Puff, Base, FLinearColor(0.42f, 0.37f, 0.3f));
			ScuffleCloud.Add(Puff);
		}
	}
	ScuffleSpin += DeltaSeconds;
	// Dust kicked up round the pair's feet: each puff swells as it rises and drifts out, then fades away and starts
	// again, the puffs staggered so there's always some in the air.
	const FVector Feet = bShow ? (GetActorLocation() + Arrester->GetActorLocation()) * 0.5f - FVector(0.f, 0.f, HalfHeight) : FVector::ZeroVector;
	for (int32 i = 0; i < ScuffleCloud.Num(); ++i)
	{
		UStaticMeshComponent* Puff = ScuffleCloud[i];
		if (Puff->IsVisible() != bShow)
		{
			Puff->SetVisibility(bShow);
		}
		if (!bShow)
		{
			continue;
		}
		const float Life = FMath::Frac(ScuffleSpin * 1.7f + float(i) / NumScufflePuffs);
		const float Angle = i * 2.4f + FMath::FloorToFloat(ScuffleSpin * 1.7f + float(i) / NumScufflePuffs) * 1.3f;
		const float Reach = 45.f + 55.f * Life;
		Puff->SetWorldLocation(Feet + FVector(FMath::Cos(Angle) * Reach, FMath::Sin(Angle) * Reach, 6.f + 40.f * Life));
		Puff->SetWorldScale3D(FVector(0.36f * FMath::Sin(PI * Life)));
	}
}

// ------------------------------------------------------------------------------------------
// The crime
// ------------------------------------------------------------------------------------------

void AFTOPerp::BeginDeed()
{
	DeedStops.Reset();
	DeedStop = 0;
	bDeedWalking = false;
	SmashISM.Reset();
	SmashTime = 0.f;
	if (!Incident)
	{
		return;
	}
	const FFTOIncidentInfo Info = Incident->GetInfo();
	const FName Crime = Info.TemplateId;
	const float Now = GetWorld()->GetTimeSeconds();
	DeedStartTime = Now;
	DeedEndTime = bCriminal && Info.DeedSeconds > 0.f ? Now + Info.DeedSeconds * Rng.FRandRange(0.8f, 1.2f) : 0.f;
	DeedPauseUntil = Now + Rng.FRandRange(1.f, 3.f);
	bHasLoot = bCriminal && (Crime == TEXT("Shoplifting") || Crime == TEXT("Burglary"));
	OnRep_Loot();

	const FVector Fwd = FRotator(0.f, HomeYaw, 0.f).Vector();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Fwd);
	if (Crime == TEXT("Shoplifting") || Crime == TEXT("Burglary"))
	{
		// Working along the shelves (or round the room), filling the sack.
		DeedStops = { Home, Home + Side * 160.f, Home - Side * 160.f };
	}
	else if (Crime == TEXT("Jaywalking") || Crime == TEXT("LostTourist"))
	{
		// Which way's the road? Away from the middle of the block they're on.
		FVector Out = Fwd;
		float Road = 1200.f;
		if (const AFTOCityGenerator* TheCity = FindCity())
		{
			Road = TheCity->GetRoadWidth();
			const FFTOCityBlock* Nearest = nullptr;
			for (const FFTOCityBlock& Block : TheCity->GetBlocks())
			{
				if (!Nearest || FVector::DistSquared2D(Block.Center, Home) < FVector::DistSquared2D(Nearest->Center, Home))
				{
					Nearest = &Block;
				}
			}
			if (Nearest)
			{
				const FVector Off = Home - Nearest->Center;
				Out = FMath::Abs(Off.X) > FMath::Abs(Off.Y) ? FVector(FMath::Sign(Off.X), 0.f, 0.f) : FVector(0.f, FMath::Sign(Off.Y), 0.f);
			}
		}
		if (Crime == TEXT("Jaywalking"))
		{
			DeedStops = { Home, Home + Out * (Road + 300.f) }; // over the road and back, again and again
		}
		else
		{
			const FVector Along = FVector::CrossProduct(FVector::UpVector, Out);
			DeedStops = { Home, Home + Along * 450.f, Home - Along * 350.f }; // up and down the kerb, map out
		}
	}
	else if (Crime == TEXT("Vandalism") && !Incident->IsIndoors())
	{
		DeedStops = { Home }; // from one bit of street furniture to the next (NextDeedStop finds them)
	}
	else if (Crime == TEXT("Graffiti"))
	{
		// Up against the nearest wall, spray can going.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOTagger), false, this);
		Params.AddIgnoredActor(Incident);
		const FVector Eye = Home + FVector(0.f, 0.f, 30.f);
		// A patch of plain wall a tag's width across: every corner of it a solid, flat hit (no window, door or corner).
		auto IsPlainWall = [&](const FHitResult& Hit)
		{
			const FVector Normal = Hit.ImpactNormal.GetSafeNormal2D();
			const FVector Along = FVector::CrossProduct(FVector::UpVector, Normal);
			for (const FVector2D& Corner : { FVector2D(-70.f, -40.f), FVector2D(70.f, -40.f), FVector2D(-70.f, 50.f), FVector2D(70.f, 50.f), FVector2D(0.f, 0.f) })
			{
				const FVector On = Hit.ImpactPoint + Along * Corner.X + FVector(0.f, 0.f, 10.f + Corner.Y);
				FHitResult Probe;
				if (!GetWorld()->LineTraceSingleByChannel(Probe, On + Normal * 40.f, On - Normal * 20.f, ECC_Visibility, Params) ||
					FMath::Abs(Probe.Distance - 40.f) > 15.f || AFTODestruction::KindOf(Probe.GetComponent()) != EFTOBreakKind::None)
				{
					return false;
				}
			}
			return true;
		};
		// The nearest plain patch of wall, else the nearest wall of any sort.
		FHitResult Best;
		FHitResult AnyWall;
		for (int32 i = 0; i < 16; ++i)
		{
			FHitResult Hit;
			const FVector Dir = FRotator(0.f, HomeYaw + i * 22.5f, 0.f).Vector();
			if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Dir * 2500.f, ECC_Visibility, Params) ||
				FMath::Abs(Hit.ImpactNormal.Z) >= 0.3f || Cast<APawn>(Hit.GetActor()) || AFTODestruction::KindOf(Hit.GetComponent()) != EFTOBreakKind::None)
			{
				continue;
			}
			if (!AnyWall.bBlockingHit || Hit.Distance < AnyWall.Distance)
			{
				AnyWall = Hit;
			}
			if ((!Best.bBlockingHit || Hit.Distance < Best.Distance) && IsPlainWall(Hit))
			{
				Best = Hit;
			}
		}
		if (!Best.bBlockingHit)
		{
			Best = AnyWall;
		}
		if (Best.bBlockingHit)
		{
			// Set up at that wall (the scene moves with them: nobody's seen it yet).
			const FVector Normal = Best.ImpactNormal.GetSafeNormal2D();
			Home = FVector(Best.ImpactPoint.X, Best.ImpactPoint.Y, Home.Z) + Normal * 60.f;
			FHitResult Ground;
			if (GetWorld()->LineTraceSingleByChannel(Ground, Home + FVector(0.f, 0.f, 100.f), Home - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params))
			{
				Home.Z = Ground.ImpactPoint.Z + HalfHeight;
			}
			Incident->SetActorLocation(Home - FVector(0.f, 0.f, HalfHeight));
			HomeYaw = (-Normal).Rotation().Yaw;
			TeleportAndHold(Home);
			FaceYaw(HomeYaw);
			Tag = AFTOGraffitiTag::Spray(GetWorld(), Best.ImpactPoint + FVector(0.f, 0.f, 10.f) + Normal * 6.f, Normal, LookSeed);
		}
	}
}

void AFTOPerp::WearLookOf(int32 Seed)
{
	LookSeed = Seed;
	bStreetClothes = true;
	ApplyLook();
}

bool AFTOPerp::IsHidingInBuilding() const
{
	return Incident && Incident->IsHiddenInside() && ArrestState == EFTOPerpArrest::None;
}

bool AFTOPerp::IsDrunkCall() const
{
	return bCriminal && Incident && Incident->GetInfo().TemplateId == TEXT("Drunk");
}

int32 AFTOPerp::DrunkLineFor(int32 Stage) const
{
	// Three of the lines, in an order of their own (the same on every machine: it's all from the look seed).
	TArray<int32> Lines;
	for (int32 i = 0; i < UE_ARRAY_COUNT(DrunkLines); ++i)
	{
		Lines.Add(i);
	}
	FRandomStream LineRng(LookSeed * 7 + 3);
	for (int32 i = Lines.Num() - 1; i > 0; --i)
	{
		Lines.Swap(i, LineRng.RandRange(0, i));
	}
	return Lines[FMath::Clamp(Stage, 0, Lines.Num() - 1)];
}

void AFTOPerp::DrunkOptions(int32 Stage, int32 OutOrder[3]) const
{
	// 0 is the friendly answer, 1 and 2 the ones that wind them up; shuffled per stage.
	OutOrder[0] = 0;
	OutOrder[1] = 1;
	OutOrder[2] = 2;
	FRandomStream OrderRng(LookSeed * 13 + Stage * 101);
	for (int32 i = 2; i > 0; --i)
	{
		Swap(OutOrder[i], OutOrder[OrderRng.RandRange(0, i)]);
	}
}

int32 AFTOPerp::GetDrunkRightAnswer() const
{
	int32 Order[3];
	DrunkOptions(DrunkStage, Order);
	return Order[0] == 0 ? 0 : (Order[1] == 0 ? 1 : 2);
}

FText AFTOPerp::GetTalkTitle() const
{
	if (IsDrunkCall() && ArrestState == EFTOPerpArrest::None && DrunkStage < DrunkStages)
	{
		return FText::FromString(FString::Printf(TEXT("Drunk: \"%s\""), DrunkLines[DrunkLineFor(DrunkStage)].Says));
	}
	return Super::GetTalkTitle();
}

void AFTOPerp::GetTalkOptions(const AFTOCharacter* Officer, TArray<FText>& OutOptions) const
{
	if (IsDrunkCall() && ArrestState == EFTOPerpArrest::None && DrunkStage < DrunkStages)
	{
		const FDrunkLine& Line = DrunkLines[DrunkLineFor(DrunkStage)];
		int32 Order[3];
		DrunkOptions(DrunkStage, Order);
		for (const int32 Answer : Order)
		{
			OutOptions.Add(FText::FromString(Answer == 0 ? Line.Right : Line.Wrong[Answer - 1]));
		}
		OutOptions.Add(INVTEXT("That's all. Stay out of trouble."));
		return;
	}
	Super::GetTalkOptions(Officer, OutOptions);
}

bool AFTOPerp::DrunkAnswer(AFTOCharacter* Officer, int32 Index)
{
	AFTOPlayerController* PC = PCOf(Officer);
	if (Index == 3)
	{
		return false; // (walked away: they're still there to talk round)
	}
	if (Index < 0 || Index > 2)
	{
		return true;
	}
	HoldForTalk(Officer);
	int32 Order[3];
	DrunkOptions(DrunkStage, Order);
	const FDrunkLine& Line = DrunkLines[DrunkLineFor(DrunkStage)];
	Incident->ReportByOfficer();
	if (Order[Index] == 0)
	{
		// The friendly answer: they come round a bit more.
		Officer->PlayTimedAction(EFTOAnimAction::Talk, 1.2f);
		++DrunkStage;
		Incident->SetTalkProgress(float(DrunkStage) / DrunkStages);
		if (DrunkStage >= DrunkStages)
		{
			CalmDown(Officer);
			return false;
		}
		if (PC)
		{
			PC->ClientToast(FText::FromString(Line.Reply), FLinearColor::White);
		}
		return true;
	}
	// Wound up: once is a warning, twice and they swing.
	++DrunkTemper;
	if (DrunkTemper >= 2)
	{
		if (PC)
		{
			PC->ClientToast(INVTEXT("\"Thass IT! Put 'em up!\" They want a fight: punch (LMB), kick (G), grab (F)."), Warning);
		}
		BeginFighting(Officer);
		return false;
	}
	if (PC)
	{
		PC->ClientToast(INVTEXT("\"WHAT did you just say to me?!\" (They're getting worked up: one more like that and they'll swing.)"), Warning);
	}
	return true;
}

void AFTOPerp::CalmDown(AFTOCharacter* Officer)
{
	ToastOfficersNear(INVTEXT("They've calmed down and are waving you off: they'll get a taxi home. Nicely handled!"), GoodNews, 2500.f);
	bChatting = false;
	TalkingWith.Reset();
	FaceToward(Officer->GetActorLocation());
	Incident->HandledPeacefully();
}

bool AFTOPerp::TalkChoice(AFTOCharacter* Officer, int32 Index)
{
	if (IsDrunkCall() && ArrestState == EFTOPerpArrest::None && DrunkStage < DrunkStages && Incident && Incident->IsActive())
	{
		return DrunkAnswer(Officer, Index);
	}
	if (ArrestState != EFTOPerpArrest::Hiding)
	{
		return false; // (only a suspect lying low is up for a chat)
	}
	// Nerves: the questions might be too much for them.
	if ((Index == 0 || Index == 1) && ForcedResponse == EFTOArrestResponse::Roll && !bInCrowd && Rng.FRand() < 0.2f)
	{
		ToastOfficersNear(FText::Format(INVTEXT("They panicked and ran: that's the {0} suspect! Sprint (Shift) and tackle (F)!"), Incident->GetInfo().Title), Warning, 3000.f);
		BeginFleeing(Officer);
		return false;
	}
	return Super::TalkChoice(Officer, Index);
}

FString AFTOPerp::GetSmallTalk()
{
	if (ArrestState != EFTOPerpArrest::Hiding)
	{
		return Super::GetSmallTalk();
	}
	static const TCHAR* Nervous[] =
	{
		TEXT("\"Fine! Great. Normal day. Very normal. Why?\""),
		TEXT("\"Just out for a walk. With this sack. Of... laundry.\""),
		TEXT("\"Officer! Lovely to see you. Is that the time? I must dash.\""),
	};
	return Nervous[Rng.RandRange(0, UE_ARRAY_COUNT(Nervous) - 1)];
}

FString AFTOPerp::AnswerWhatTheySaw()
{
	if (ArrestState != EFTOPerpArrest::Hiding)
	{
		return Super::AnswerWhatTheySaw();
	}
	return Rng.FRand() < 0.5f ? TEXT("\"Seen anything? Me? No. Nothing. Nobody like me, anyway.\"") : TEXT("\"A suspicious... someone? Went that way. Definitely that way. Not me.\"");
}

FString AFTOPerp::Contraband()
{
	if (ArrestState != EFTOPerpArrest::Hiding || !Incident)
	{
		return Super::Contraband();
	}
	if (Incident->GetInfo().TemplateId == TEXT("PettyTheft"))
	{
		return TEXT("four wallets, none of them theirs");
	}
	return FString::Printf(TEXT("the loot from the %s"), *Incident->GetInfo().Title.ToString().ToLower());
}

void AFTOPerp::ArrestForWhatWasFound(AFTOCharacter* Officer)
{
	if (ArrestState != EFTOPerpArrest::Hiding || !Incident)
	{
		Super::ArrestForWhatWasFound(Officer);
		return;
	}
	// Found them: the arrest goes as any other (they might still come quietly, fight or run).
	ToastOfficersNear(FText::Format(INVTEXT("That's the {0} suspect!"), Incident->GetInfo().Title), ArrestBlue, 3000.f);
	TryArrest(Officer);
}

void AFTOPerp::FinishDeedNow()
{
	if (HasAuthority() && bCriminal && ArrestState == EFTOPerpArrest::None && Incident && Incident->IsActive())
	{
		SlipOut();
	}
}

void AFTOPerp::SlipOut()
{
	DeedEndTime = 0.f;
	if (Incident->GetInfo().TemplateId == TEXT("Burglary"))
	{
		if (AFTOCharacter* Guard = DoorGuard())
		{
			CaughtAtTheDoor(Guard);
			return;
		}
	}
	if (Incident->IsHiddenInside())
	{
		const AFTOCityGenerator* TheCity = FindCity();
		if (const FFTOBuilding* Building = TheCity ? TheCity->GetBuilding(Incident->GetBuildingIndex()) : nullptr)
		{
			TeleportAndHold(Building->Room.TransformPosition(FVector(70.f, 0.f, 0.f)) + FVector(0.f, 0.f, HalfHeight));
		}
		bHidingUpstairs = false;
		Incident->SetHiddenInside(false);
		ToastOfficersNear(FText::Format(INVTEXT("{0}: nobody found them, and they've slipped out with the goods!"), Incident->GetInfo().Title), BadNews, 1000000.f);
	}
	GoIntoHiding(false);
}

AFTOCharacter* AFTOPerp::DoorGuard(const AActor* Except) const
{
	const AFTOCityGenerator* TheCity = City ? City.Get() : nullptr;
	if (!TheCity)
	{
		for (TActorIterator<AFTOCityGenerator> It(GetWorld()); It; ++It)
		{
			TheCity = *It;
			break;
		}
	}
	const FFTOBuilding* Building = TheCity && Incident ? TheCity->GetBuilding(Incident->GetBuildingIndex()) : nullptr;
	if (!Building)
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AFTOCharacter* Cop = It->IsValid() ? Cast<AFTOCharacter>((*It)->GetPawn()) : nullptr;
		if (Cop && Cop != Except && Cop->IsReadyForAction() && FVector::Dist2D(Cop->GetActorLocation(), Building->DoorOutside) < 400.f &&
			FMath::Abs(Cop->GetActorLocation().Z - HalfHeight - Building->DoorOutside.Z) < 150.f)
		{
			return Cop;
		}
	}
	return nullptr;
}

void AFTOPerp::CaughtAtTheDoor(AFTOCharacter* Guard)
{
	const AFTOCityGenerator* TheCity = FindCity();
	if (const FFTOBuilding* Building = TheCity ? TheCity->GetBuilding(Incident->GetBuildingIndex()) : nullptr)
	{
		TeleportAndHold(Building->Room.TransformPosition(FVector(70.f, 0.f, 0.f)) + FVector(0.f, 0.f, HalfHeight));
		FaceToward(Building->DoorOutside);
	}
	bHidingUpstairs = false;
	bCornered = false;
	if (Incident->IsHiddenInside())
	{
		Incident->SuspectFound(GetActorLocation() - FVector(0.f, 0.f, HalfHeight), false);
	}
	// Teamwork, with someone else on the shift to have been searching (alone, it's just good policing).
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const bool bTeam = GS && GS->PlayerArray.Num() >= 2;
	if (bTeam)
	{
		FTOScoring::Award(Guard, EFTOScore::Teamwork, GetActorLocation() + FVector(0.f, 0.f, 200.f));
	}
	ToastOfficersNear(bTeam ? INVTEXT("Caught at the door! They ran straight into the officer guarding it. Teamwork!")
		: INVTEXT("Caught at the door! They ran straight into you."), GoodNews, 4000.f);
	GiveUp(Guard->GetController());
}

void AFTOPerp::TickDeed(float DeltaSeconds)
{
	if (!bCriminal || !Incident || !Incident->IsActive() || ArrestState != EFTOPerpArrest::None)
	{
		return;
	}
	// The city holds its breath while the squad votes on overtime: nobody finishes a crime meanwhile.
	if (const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>(); GS && GS->GetShiftPhase() == EFTOShiftPhase::OvertimeVote)
	{
		DeedStartTime += DeltaSeconds;
		DeedEndTime += DeedEndTime > 0.f ? DeltaSeconds : 0.f;
		DeedPauseUntil += DeltaSeconds;
		return;
	}
	if ((Incident->GetState() == EFTOIncidentState::Responding && !Incident->IsHiddenInside()) || Incident->IsSubdued())
	{
		// The police are here: whatever they were up to stops (a hidden burglar's still waiting for their moment).
		if (bDeedWalking)
		{
			bDeedWalking = false;
			Hold();
		}
		SmashTime = 0.f;
		return;
	}
	if (bShooting || bHandsUp || bChatting || (Knockdown && (Knockdown->IsDown() || Knockdown->IsDazed())))
	{
		return;
	}
	DeedCheckAccumulator += DeltaSeconds;
	if (DeedCheckAccumulator < 0.2f)
	{
		return;
	}
	DeedCheckAccumulator = 0.f;
	const float Now = GetWorld()->GetTimeSeconds();

	// Done: off with the goods, before anyone comes (a burglar who was never found slips out of the front door).
	if (DeedEndTime > 0.f && Now >= DeedEndTime)
	{
		SlipOut();
		return;
	}
	// The tag goes up a letter at a time (finished a little before they leave).
	if (Tag.IsValid())
	{
		const float Span = DeedEndTime > 0.f ? DeedEndTime - DeedStartTime : 60.f;
		Tag->SetProgress((Now - DeedStartTime) / FMath::Max(1.f, Span * 0.85f));
	}
	// The vandal's boot connects.
	if (SmashTime > 0.f && Now >= SmashTime)
	{
		SmashTime = 0.f;
		if (AFTODestruction* Wreckage = SmashISM.IsValid() ? AFTODestruction::Get(GetWorld()) : nullptr)
		{
			const FVector Push = (SmashAt - GetActorLocation()).GetSafeNormal2D() * 450.f + FVector(0.f, 0.f, 150.f);
			Wreckage->Break(SmashISM.Get(), SmashInstance, SmashAt, Push, nullptr);
		}
		SmashISM.Reset();
		DeedPauseUntil = Now + Rng.FRandRange(2.f, 4.f);
	}
	// Paused at a stop long enough: on to the next.
	if (!bDeedWalking && DeedStops.Num() > 0 && SmashTime <= 0.f && Now >= DeedPauseUntil)
	{
		NextDeedStop();
	}
}

void AFTOPerp::NextDeedStop()
{
	const FName Crime = Incident->GetInfo().TemplateId;
	if (Crime == TEXT("Vandalism"))
	{
		UInstancedStaticMeshComponent* ISM = nullptr;
		int32 Instance = INDEX_NONE;
		FVector Where;
		if (FindSomethingToSmash(Home, ISM, Instance, Where))
		{
			SmashISM = ISM;
			SmashInstance = Instance;
			SmashAt = Where;
			FVector Stand = Where + (GetActorLocation() - Where).GetSafeNormal2D() * 90.f;
			Stand.Z = Home.Z;
			bDeedWalking = true;
			MoveTo(Stand, 220.f);
		}
		else
		{
			DeedPauseUntil = GetWorld()->GetTimeSeconds() + 4.f; // nothing left standing nearby: have a breather
		}
		return;
	}
	DeedStop = (DeedStop + 1) % DeedStops.Num();
	bDeedWalking = true;
	const bool bSneaking = Crime == TEXT("Shoplifting") || Crime == TEXT("Burglary");
	MoveTo(DeedStops[DeedStop], bSneaking ? 110.f : (Crime == TEXT("Jaywalking") ? 190.f : 140.f));
}

bool AFTOPerp::FindSomethingToSmash(const FVector& Around, UInstancedStaticMeshComponent*& OutISM, int32& OutInstance, FVector& OutWhere) const
{
	const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
	if (!Wreckage)
	{
		return false;
	}
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Types;
	Types.AddObjectTypesToQuery(ECC_WorldStatic);
	Types.AddObjectTypesToQuery(ECC_WorldDynamic);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Around, FQuat::Identity, Types, FCollisionShape::MakeSphere(1000.f));

	// The nearest bin, bench, planter or hydrant that's still standing (not the lamp posts: they're too big to kick).
	float BestDistSq = TNumericLimits<float>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Overlap.GetComponent());
		const EFTOBreakKind Kind = AFTODestruction::KindOf(ISM);
		if (!ISM || (Kind != EFTOBreakKind::KnockOff && Kind != EFTOBreakKind::Smash && Kind != EFTOBreakKind::Burst) ||
			Overlap.ItemIndex == INDEX_NONE || Wreckage->IsBroken(ISM->GetFName(), Overlap.ItemIndex))
		{
			continue;
		}
		FTransform Instance;
		if (!ISM->GetInstanceTransform(Overlap.ItemIndex, Instance, true))
		{
			continue;
		}
		const float DistSq = FVector::DistSquared2D(Instance.GetLocation(), GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			OutISM = ISM;
			OutInstance = Overlap.ItemIndex;
			OutWhere = Instance.GetLocation() + FVector(0.f, 0.f, 40.f);
		}
	}
	return OutISM != nullptr;
}

void AFTOPerp::GoIntoHiding(bool bSeen)
{
	GetWorldTimerManager().ClearTimer(ResumeTimer);
	ReleaseArrester();
	bHandsUp = false;
	bChatting = false;
	bShooting = false;
	AimPitch = 0.f;
	bDeedWalking = false;
	DeedStops.Reset();
	SmashTime = 0.f;
	DeedEndTime = 0.f;

	// Off with whatever they came for, and a crook ditches the striped jumper so they don't stand out.
	const FName Crime = Incident->GetInfo().TemplateId;
	if (Crime == TEXT("ArmedRobbery") || Crime == TEXT("Mugging") || Crime == TEXT("PettyTheft") || Crime == TEXT("Burglary") || Crime == TEXT("Shoplifting"))
	{
		bHasLoot = true;
		OnRep_Loot();
	}
	if (!bStreetClothes && !bDisguised)
	{
		bDisguised = true;
		ApplyLook();
	}

	ArrestState = EFTOPerpArrest::Hiding;
	bInCrowd = false;
	bCornered = false; // (out on the street now)
	if (Incident->IsCrowd())
	{
		Incident->SetCrowd(false);
	}
	bWandering = false;
	bOnSidewalks = false;
	PrevFleeCorner = FIntVector(-1, -1, -1);
	FleeExit.Reset();
	// Walking briskly away from the scene; after a chase, strolling (running would give them away).
	RunSpeed = bSeen ? HideSpeed : LeaveSpeed;
	const FVector Up(0.f, 0.f, HalfHeight);
	const AFTOCityGenerator* TheCity = FindCity();
	const FFTOBuilding* Building = TheCity && Incident->IsIndoors() ? TheCity->GetBuilding(Incident->GetBuildingIndex()) : nullptr;
	if (Building && Building->Contains(GetActorLocation() - Up, 30.f))
	{
		FleeExit.Add(Building->Room.TransformPosition(FVector(70.f, 0.f, 0.f)) + Up);
		FleeExit.Add(Building->DoorOutside + Up);
	}
	Incident->StartSearch(GetActorLocation() - Up);
	RunToNextWaypoint();
}

void AFTOPerp::JoinCrowd(const FVector& At, float Yaw)
{
	check(HasAuthority());
	Home = At;
	HomeYaw = Yaw;
	TeleportAndHold(Home);
	FaceYaw(HomeYaw);
	DeedStops.Reset();
	DeedEndTime = 0.f;
	bHasLoot = false; // (the wallets are in their pockets)
	OnRep_Loot();
	ArrestState = EFTOPerpArrest::Hiding;
	bInCrowd = true;
	NextShuffle = GetWorld()->GetTimeSeconds() + Rng.FRandRange(3.f, 10.f);
}

void AFTOPerp::HideInBuilding()
{
	FVector Where;
	bool bUpstairs = false;
	if (!FindHidingPlace(Where, bUpstairs))
	{
		return; // nowhere better: they're where the call came from
	}
	Home = Where;
	TeleportAndHold(Home);
	FaceYaw(HomeYaw);
	DeedStops.Reset();
	bHidingUpstairs = bUpstairs;
	Incident->SetHiddenInside(true);
}

bool AFTOPerp::FindHidingPlace(FVector& OutWhere, bool& bOutUpstairs) const
{
	AFTOCityGenerator* TheCity = City ? City.Get() : nullptr;
	if (!TheCity)
	{
		for (TActorIterator<AFTOCityGenerator> It(GetWorld()); It; ++It)
		{
			TheCity = *It;
			break;
		}
	}
	const int32 Index = Incident ? Incident->GetBuildingIndex() : INDEX_NONE;
	const FFTOBuilding* Building = TheCity ? TheCity->GetBuilding(Index) : nullptr;
	if (!Building)
	{
		return false;
	}
	const FFTOStructure* Structure = nullptr;
	for (const FFTOStructure& S : TheCity->GetStructures())
	{
		if (S.Building == Index)
		{
			Structure = &S;
			break;
		}
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOHide), false, this);
	Params.AddIgnoredActor(Incident);
	// Somewhere to stand: floor under it and a clear line to it from where the way in is.
	auto Stand = [&](const FVector& From, FVector Spot, FVector& Out)
	{
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, From, Spot, ECC_Visibility, Params))
		{
			Spot = From + (Spot - From).GetSafeNormal() * FMath::Max(0.f, Hit.Distance - 60.f);
		}
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Spot + FVector(0.f, 0.f, 20.f), Spot - FVector(0.f, 0.f, 260.f), ECC_Visibility, Params))
		{
			return false;
		}
		Out = Hit.ImpactPoint + FVector(0.f, 0.f, HalfHeight);
		return true;
	};
	TArray<FVector> Upstairs;
	if (Structure && Structure->Floors > 0)
	{
		auto InFootprint = [Structure](const FVector& P)
		{
			return FMath::Abs(P.X - Structure->Center.X) < Structure->HalfX + 200.f && FMath::Abs(P.Y - Structure->Center.Y) < Structure->HalfY + 200.f;
		};
		// A few metres in from where the lift lets out, on any floor above...
		for (TActorIterator<AFTOLift> It(GetWorld()); It; ++It)
		{
			const FVector Arrive = It->GetArrivalPoint();
			if (It->GetFloor() > 0 && InFootprint(Arrive))
			{
				const FVector In = FVector(Structure->Center.X, Structure->Center.Y, Arrive.Z) - Arrive;
				FVector Spot;
				if (Stand(Arrive, Arrive + In.GetSafeNormal2D() * FMath::Min(In.Size2D() * 0.7f, 600.f) + It->GetActorRightVector() * Rng.FRandRange(-250.f, 250.f), Spot))
				{
					Upstairs.Add(Spot);
				}
			}
		}
		// ...or in the room at the top of a house's outside stairs.
		for (const FTransform& Top : TheCity->GetOutsideStairTops())
		{
			const FVector Door = Top.GetLocation() + FVector(0.f, 0.f, HalfHeight);
			FVector Spot;
			if (InFootprint(Door) && Stand(Door, Door + Top.GetRotation().Vector() * 300.f, Spot))
			{
				Upstairs.Add(Spot);
			}
		}
	}
	if (Upstairs.Num() > 0 && Rng.FRand() < 0.7f)
	{
		OutWhere = Upstairs[Rng.RandRange(0, Upstairs.Num() - 1)];
		bOutUpstairs = true;
		return true;
	}
	// Downstairs: the far end of the room from the door, behind the furniture.
	TArray<FVector> Down;
	for (const TArray<FFTOSpot>* Spots : { &Building->VisitSpots, &Building->WorkSpots })
	{
		for (const FFTOSpot& Spot : *Spots)
		{
			if (!Spot.bSeated && Building->Room.InverseTransformPosition(Spot.Transform.GetLocation()).X > Building->Depth * 0.5f)
			{
				Down.Add(Spot.Transform.GetLocation() + FVector(0.f, 0.f, HalfHeight));
			}
		}
	}
	if (Down.Num() == 0)
	{
		return false;
	}
	OutWhere = Down[Rng.RandRange(0, Down.Num() - 1)];
	bOutUpstairs = false;
	return true;
}

void AFTOPerp::Found(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (!Incident || !Incident->IsHiddenInside() || !Officer)
	{
		return;
	}
	const bool bUpstairs = bHidingUpstairs;
	bHidingUpstairs = false;
	bCornered = bUpstairs;
	Incident->SuspectFound(GetActorLocation() - FVector(0.f, 0.f, HalfHeight), bUpstairs);
	FaceToward(Officer->GetActorLocation());
	ToastOfficersNear(FText::Format(INVTEXT("Found the {0} suspect!"), Incident->GetInfo().Title), ArrestBlue, 3000.f);
	// Cornered: give in, or have a go (upstairs there's nowhere to run).
	EFTOArrestResponse Response = ForcedResponse;
	if (Response == EFTOArrestResponse::Roll)
	{
		const float Roll = Rng.FRand();
		Response = Roll < 0.45f ? EFTOArrestResponse::Comply : (bUpstairs || Roll < 0.75f ? EFTOArrestResponse::Fight : EFTOArrestResponse::Bolt);
	}
	switch (Response)
	{
	case EFTOArrestResponse::Fight:
		BeginFighting(Officer);
		break;
	case EFTOArrestResponse::Bolt:
		ToastOfficersNear(INVTEXT("They're making a run for it! Sprint after them (Shift) and tackle (F)!"), Warning, 3000.f);
		BeginFleeing(Officer);
		break;
	case EFTOArrestResponse::Struggle:
		BeginStruggle(Officer);
		break;
	default:
		if (AFTOPlayerController* PC = PCOf(Officer))
		{
			PC->ClientToast(INVTEXT("\"Alright, you got me!\" Cuff them (E)."), FLinearColor::White);
		}
		GiveUp(Officer->GetController());
		break;
	}
}

void AFTOPerp::ContinueHiding()
{
	// Clear of the scene first (out of the door, round the first corner)...
	if (FleeExit.Num() > 0 || !bOnSidewalks)
	{
		RunToNextWaypoint();
		return;
	}
	// ...then strolling round the blocks like anyone else.
	if (!bWandering)
	{
		bWandering = true;
		BlockX = FleeCorner.X;
		BlockY = FleeCorner.Y;
		Corner = FleeCorner.Z;
		WalkSpeed = HideSpeed;
		Direction = Rng.FRand() < 0.5f ? 1 : -1;
	}
	WalkToNextCorner();
}

// ------------------------------------------------------------------------------------------
// Shooting
// ------------------------------------------------------------------------------------------

bool AFTOPerp::IsStillFighting() const
{
	if (!Incident || !Incident->IsActive())
	{
		return false;
	}
	// Outnumbered or half talked down, they give up.
	if (Incident->GetState() == EFTOIncidentState::Responding)
	{
		return Incident->GetOfficersOnScene() < 2 && Incident->GetProgress() < 0.5f;
	}
	return true;
}

AActor* AFTOPerp::FindTarget() const
{
	// The nearest officer on foot and on their feet, in range, and in plain sight.
	AActor* Best = nullptr;
	float BestDistSq = FMath::Square(FiringRange);
	const FVector Eye = GetActorLocation() + FVector(0.f, 0.f, 60.f);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AFTOCharacter* Officer = It->IsValid() ? Cast<AFTOCharacter>((*It)->GetPawn()) : nullptr;
		if (!Officer || Officer->GetCurrentVehicle() || (Officer->GetKnockdown() && Officer->GetKnockdown()->IsDown()))
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(Officer->GetActorLocation(), GetActorLocation());
		if (DistSq >= BestDistSq)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOPerpSight), false, this);
		Params.AddIgnoredActor(Officer);
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, Officer->GetActorLocation() + FVector(0.f, 0.f, 40.f), ECC_Visibility, Params))
		{
			Best = Officer;
			BestDistSq = DistSq;
		}
	}
	return Best;
}

void AFTOPerp::TickShooting(float DeltaSeconds)
{
	ShootCheckAccumulator += DeltaSeconds;
	if (ShootCheckAccumulator < 0.2f)
	{
		return;
	}
	ShootCheckAccumulator = 0.f;

	const bool bCanFight = bArmed && !bHandsUp && ArrestState == EFTOPerpArrest::None && IsStillFighting() && Knockdown && !Knockdown->IsDown() && !Knockdown->IsDazed();
	AActor* Officer = bCanFight ? FindTarget() : nullptr;
	if (!Officer)
	{
		if (bShooting)
		{
			bShooting = false;
			AimPitch = 0.f;
			if (ArrestState == EFTOPerpArrest::None)
			{
				FaceYaw(HomeYaw);
			}
		}
		Target.Reset();
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (!bShooting || Target.Get() != Officer)
	{
		// A moment to draw a bead (and for the officer to dive for cover).
		bShooting = true;
		NextShotTime = FMath::Max(NextShotTime, Now + DrawSeconds);
	}
	Target = Officer;
	const FVector Chest = Officer->GetActorLocation() + FVector(0.f, 0.f, 30.f);
	FaceToward(Chest);
	AimPitch = FMath::Clamp((Chest - (GetActorLocation() + FVector(0.f, 0.f, 50.f))).Rotation().Pitch, -45.f, 45.f);
	if (Now >= NextShotTime)
	{
		Shoot();
		NextShotTime = Now + Rng.FRandRange(1.1f, 1.8f);
	}
}

void AFTOPerp::Shoot()
{
	AActor* Officer = Target.Get();
	if (!Officer)
	{
		return;
	}
	// Perps are wild shots: their aim wanders well off the officer (and the pistol's hip spread goes on top), so
	// standing in the open is risky rather than fatal. The wobble is baked into the aim everyone flies.
	const FVector Muzzle = Gun->GetStaticMesh() ? Gun->GetSocketLocation(TEXT("Muzzle")) : GetActorLocation() + FVector(0.f, 0.f, 50.f);
	const int32 Seed = Rng.RandRange(1, MAX_int32 - 1);
	const FVector Aim = FTOWeapons::RoundDirection(Officer->GetActorLocation() + FVector(0.f, 0.f, 20.f) - Muzzle, WildAim, Seed, 99);
	if (UFTOBallistics* Ballistics = UFTOBallistics::Get(GetWorld()))
	{
		Ballistics->Fire(this, EFTOWeapon::Pistol, Muzzle, Aim, Seed, false, true, GetNetMode() != NM_DedicatedServer);
	}
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastShot(this, EFTOWeapon::Pistol, Muzzle, Aim, Seed, false);
	}
}

// ------------------------------------------------------------------------------------------
// Arrests
// ------------------------------------------------------------------------------------------

bool AFTOPerp::CanInteract(const AFTOCharacter* Officer) const
{
	if (!Officer || !bCriminal || !Incident || !Incident->IsActive() || !Officer->IsReadyForAction())
	{
		return false;
	}
	switch (ArrestState)
	{
	case EFTOPerpArrest::None:
		// Not while they've got a gun on someone (that's what the taser's for), or while they're on the floor.
		return !bShooting && !(Knockdown && Knockdown->IsDown());
	case EFTOPerpArrest::Surrendered:
		return true;
	case EFTOPerpArrest::Struggling:
		return Officer != Arrester; // pile in and help
	case EFTOPerpArrest::Hiding:
		return !(Knockdown && Knockdown->IsDown()); // any officer can stop anyone for a word
	case EFTOPerpArrest::Fighting:
		return true; // (only to be told: put them down first)
	default:
		return false;
	}
}

FText AFTOPerp::GetInteractPrompt(const AFTOCharacter* Officer) const
{
	switch (ArrestState)
	{
	case EFTOPerpArrest::Surrendered: return INVTEXT("Cuff the suspect");
	case EFTOPerpArrest::Struggling:  return INVTEXT("Help your partner! (mash)");
	case EFTOPerpArrest::Fighting:    return INVTEXT("Put them down first! (punch, kick, grab)");
	case EFTOPerpArrest::Hiding:      return INVTEXT("Talk to citizen"); // they don't look any different
	default:                          return INVTEXT("Arrest the suspect");
	}
}

void AFTOPerp::Interact(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (!CanInteract(Officer))
	{
		return;
	}
	switch (ArrestState)
	{
	case EFTOPerpArrest::Surrendered: BeginCuffing(Officer); break;
	case EFTOPerpArrest::Fighting:
		if (AFTOPlayerController* PC = PCOf(Officer))
		{
			PC->ClientToast(INVTEXT("They won't come quietly: put them down first! Punch (LMB), kick (G), or grab and throw them (F)."), Warning);
		}
		break;
	case EFTOPerpArrest::Struggling:  Mash(Officer); break;
	case EFTOPerpArrest::Hiding:
		// Just a word with a passer-by, as far as they're concerned (the officer might know better).
		Super::Interact(Officer);
		break;
	default:
		if (Incident->IsHiddenInside())
		{
			Found(Officer);
		}
		else if (IsDrunkCall() && DrunkStage < DrunkStages)
		{
			Super::Interact(Officer); // a word first: they might be talked round
			if (AFTOPlayerController* PC = PCOf(Officer))
			{
				PC->ClientToast(FText::FromString(FString::Printf(TEXT("\"%s\""), DrunkLines[DrunkLineFor(DrunkStage)].Says)), FLinearColor::White);
			}
		}
		else
		{
			TryArrest(Officer);
		}
		break;
	}
}

EFTOArrestResponse AFTOPerp::RollResponse(const AFTOCharacter* Officer)
{
	// Petty crooks mostly come quietly and the big fish take their chances; company and a good talking-to help.
	const FFTOIncidentInfo Info = Incident->GetInfo();
	static const float BaseComply[] = { 0.75f, 0.6f, 0.45f, 0.35f };
	int32 Officers = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AFTOCharacter* Cop = It->IsValid() ? Cast<AFTOCharacter>((*It)->GetPawn()) : nullptr;
		Officers += Cop && FVector::DistSquared2D(Cop->GetActorLocation(), GetActorLocation()) < FMath::Square(800.f) ? 1 : 0;
	}
	const float Comply = BaseComply[FMath::Clamp(int32(Info.Tier), 0, 3)] + 0.15f * FMath::Max(0, Officers - 1) + 0.4f * Incident->GetProgress();
	if (Info.Twist == TEXT("Fleeing") && ArrestState != EFTOPerpArrest::Hiding)
	{
		return EFTOArrestResponse::Bolt; // on the move, and staying that way
	}
	if (Rng.FRand() < Comply)
	{
		return EFTOArrestResponse::Comply;
	}
	// Brawlers (and drunks) would rather settle it with their fists.
	const FName Crime = Info.TemplateId;
	const bool bDrunk = Info.Twist == TEXT("Drunk") || Crime == TEXT("Drunk");
	const bool bBrawler = Crime == TEXT("BarFight") || Crime == TEXT("Riot") || Crime == TEXT("DomesticDispute") || Crime == TEXT("Vandalism") ||
		Crime == TEXT("Mugging") || bDrunk;
	if (bBrawler && Rng.FRand() < 0.55f)
	{
		return EFTOArrestResponse::Fight;
	}
	// "Keeps trying to hug the officers": a drunk wrestles, never runs.
	return bDrunk || Rng.FRand() < 0.5f ? EFTOArrestResponse::Struggle : EFTOArrestResponse::Bolt;
}

void AFTOPerp::TryArrest(AFTOCharacter* Officer)
{
	// Caught at it counts as caught in the act.
	Incident->ReportByOfficer();
	switch (ForcedResponse != EFTOArrestResponse::Roll ? ForcedResponse : RollResponse(Officer))
	{
	case EFTOArrestResponse::Struggle:
		BeginStruggle(Officer);
		break;
	case EFTOArrestResponse::Fight:
		BeginFighting(Officer);
		break;
	case EFTOArrestResponse::Bolt:
		ToastOfficersNear(INVTEXT("They're making a run for it! Sprint after them (Shift) and tackle (F)!"), Warning, 3000.f);
		BeginFleeing(Officer);
		break;
	default:
		if (AFTOPlayerController* PC = PCOf(Officer))
		{
			PC->ClientToast(INVTEXT("\"Alright, alright! I'll come quietly.\""), FLinearColor::White);
		}
		BeginCuffing(Officer);
		break;
	}
}

void AFTOPerp::BeginCuffing(AFTOCharacter* Officer)
{
	// Up off the floor if they were still on it (the officer hauls them onto their knees), facing away from the officer.
	if (Knockdown && Knockdown->IsDown())
	{
		Knockdown->Recover();
	}
	GetWorldTimerManager().ClearTimer(ResumeTimer);
	bHandsUp = false;
	bChatting = false;
	bShooting = false;
	AimPitch = 0.f;
	Incident->Subdue(Officer->GetController());

	const FVector Here = GetActorLocation();
	FVector Away = (Here - Officer->GetActorLocation()).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = GetActorForwardVector();
	}
	Hold();
	FaceYaw(Away.Rotation().Yaw);
	ArrestState = EFTOPerpArrest::Cuffing;
	Arrester = Officer;
	CuffStartTime = GetWorld()->GetTimeSeconds();
	bInCrowd = false;
	Officer->BeginSyncedAction(EFTOAnimAction::Cuffing, Here - FVector(0.f, 0.f, HalfHeight) - Away * CuffDistance, Away.Rotation().Yaw, this);
	GetWorldTimerManager().SetTimer(CuffTimer, this, &AFTOPerp::FinishCuffing, CuffSeconds, false);
}

void AFTOPerp::FinishCuffing()
{
	AFTOCharacter* Officer = Arrester;
	if (ArrestState != EFTOPerpArrest::Cuffing || !IsArresterWithUs())
	{
		return; // interrupted (TickArrest has already put us back on our knees)
	}
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastPlaySound(AFTOGameState::Sounds().Cuffs, GetActorLocation(), 1.f);
	}
	Officer->EndSyncedAction();
	Arrester = nullptr;
	// Handled: the director swaps us for a cuffed arrestee who follows this officer (so nothing after this line).
	Incident->CompleteArrest(Officer);
}

void AFTOPerp::Provoked(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (!bCriminal || !Officer || (Knockdown && Knockdown->IsDown()) ||
		(ArrestState != EFTOPerpArrest::None && ArrestState != EFTOPerpArrest::Hiding))
	{
		return; // already fighting, running, kneeling or being cuffed
	}
	if (Incident)
	{
		Incident->ReportByOfficer();
	}
	// Hit first, asked later: mostly they hit back.
	EFTOArrestResponse Response = ForcedResponse;
	if (Response == EFTOArrestResponse::Roll)
	{
		const float Roll = Rng.FRand();
		Response = Roll < 0.55f ? EFTOArrestResponse::Fight : Roll < 0.8f ? EFTOArrestResponse::Bolt : EFTOArrestResponse::Comply;
	}
	switch (Response)
	{
	case EFTOArrestResponse::Bolt:
		ToastOfficersNear(INVTEXT("They're making a run for it! Sprint after them (Shift) and tackle (F)!"), Warning, 3000.f);
		BeginFleeing(Officer);
		break;
	case EFTOArrestResponse::Comply:
		if (AFTOPlayerController* PC = PCOf(Officer))
		{
			PC->ClientToast(INVTEXT("\"Ow! Alright, alright, I give up!\""), FLinearColor::White);
		}
		GiveUp(Officer->GetController());
		break;
	default:
		BeginFighting(Officer);
		break;
	}
}

void AFTOPerp::BeginFighting(AFTOCharacter* Officer)
{
	check(HasAuthority());
	GetWorldTimerManager().ClearTimer(ResumeTimer);
	bHandsUp = false;
	bChatting = false;
	bShooting = false;
	AimPitch = 0.f;
	Hold();
	FaceToward(Officer->GetActorLocation());
	ArrestState = EFTOPerpArrest::Fighting;
	bInCrowd = false;
	for (TActorIterator<AFTOCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetTalkingTo() == this)
		{
			It->EndTalk();
		}
	}
	FightTarget = Officer;
	FightStartTime = GetWorld()->GetTimeSeconds();
	NextSwing = FightStartTime + Rng.FRandRange(0.4f, 0.9f);
	LastReachable = FightStartTime;
	NextChase = 0.f;
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastPlaySound(AFTOGameState::Sounds().Scuffle, GetActorLocation(), 0.8f);
	}
	if (AFTOPlayerController* PC = PCOf(Officer))
	{
		PC->ClientToast(INVTEXT("They want a fight! Punch (LMB), kick (G), or grab and throw them (F)!"), Warning);
	}
	ToastOfficersNear(INVTEXT("Your partner's in a fist fight: get stuck in!"), Warning, 2500.f, Officer);
}

void AFTOPerp::TickFighting(float DeltaSeconds)
{
	if (Knockdown && (Knockdown->IsDown() || Knockdown->IsDazed()))
	{
		return; // (put on the floor by the police: Subdued has them giving up)
	}
	const float Now = GetWorld()->GetTimeSeconds();
	// Out of puff after a while.
	if (Now - FightStartTime > 30.f)
	{
		ToastOfficersNear(INVTEXT("The suspect's had enough. Cuff them (E)!"), ArrestBlue, 3000.f);
		GiveUp(nullptr);
		return;
	}
	// Whoever we were fighting is down, gone or driving off: the next officer near will do, or it's time to run.
	auto Fit = [this](const AFTOCharacter* Who)
	{
		return Who && !Who->GetCurrentVehicle() && Who->IsReadyForAction() && FVector::Dist2D(Who->GetActorLocation(), GetActorLocation()) < 1200.f;
	};
	AFTOCharacter* Officer = FightTarget.Get();
	if (!Fit(Officer))
	{
		Officer = nullptr;
		for (TActorIterator<AFTOCharacter> It(GetWorld()); It; ++It)
		{
			if (Fit(*It) && (!Officer || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Officer->GetActorLocation(), GetActorLocation())))
			{
				Officer = *It;
			}
		}
		if (!Officer)
		{
			ToastOfficersNear(INVTEXT("The suspect's made a run for it! Sprint (Shift) and tackle (F)!"), Warning, 3000.f);
			BeginFleeing(FightTarget.Get());
			return;
		}
		FightTarget = Officer;
	}
	if (Knockdown && Knockdown->IsBusy())
	{
		return; // mid-swing, or reeling
	}
	// In close (just out of arm's length), then swinging every so often. Only straight at them on the level with
	// nothing in the way (they don't walk through walls or float up stairs), and only now and then (not every frame).
	const FVector To = Officer->GetActorLocation() - GetActorLocation();
	const float Distance = To.Size2D();
	if (Distance > 125.f)
	{
		const bool bReachable = FMath::Abs(To.Z) < 60.f && CanSee(Officer);
		const FVector Goal = Officer->GetActorLocation() - To.GetSafeNormal2D() * 95.f;
		if (bReachable && (Now >= NextChase || FVector::DistSquared2D(Goal, ChaseGoal) > FMath::Square(80.f)))
		{
			ChaseGoal = Goal;
			NextChase = Now + 0.4f;
			MoveTo(FVector(Goal.X, Goal.Y, GetActorLocation().Z), 380.f);
			LastReachable = Now;
		}
		else if (!bReachable)
		{
			Hold();
			FaceToward(Officer->GetActorLocation());
			// Out of reach a good while (up the stairs, round a wall): off they go.
			if (Now - LastReachable > 6.f)
			{
				ToastOfficersNear(INVTEXT("The suspect's made a run for it! Sprint (Shift) and tackle (F)!"), Warning, 3000.f);
				BeginFleeing(Officer);
				return;
			}
		}
	}
	else
	{
		LastReachable = Now;
		if (GetCurrentSpeed() > 1.f)
		{
			Hold();
		}
		FaceToward(Officer->GetActorLocation());
	}
	if (Distance < 150.f && Now >= NextSwing && FTOFighting::CanSwing(this))
	{
		FaceToward(Officer->GetActorLocation());
		const EFTOMove Move = FTOFighting::PickBrawlerMove(Rng);
		FTOFighting::Swing(this, Move, nullptr, Officer);
		const bool bDrunk = Incident && Incident->GetInfo().Twist == TEXT("Drunk");
		NextSwing = Now + FTOFighting::Spec(Move).Length + Rng.FRandRange(0.6f, 1.4f) * (bDrunk ? 1.5f : 1.f);
	}
}

void AFTOPerp::TickBrawl(float DeltaSeconds)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextSwing || !FTOFighting::CanSwing(this) || GetCurrentSpeed() > 1.f)
	{
		return;
	}
	NextSwing = Now + Rng.FRandRange(1.2f, 2.4f);
	// Whichever brawler's nearest takes a swing (all for show: it rocks them, never floors them).
	AFTOCrimeExtra* Nearest = nullptr;
	for (TActorIterator<AFTOCrimeExtra> It(GetWorld()); It; ++It)
	{
		if (It->GetRole() == EFTOExtraRole::Brawler && FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < 230.f && FTOFighting::CanSwing(*It) &&
			(!Nearest || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), GetActorLocation())))
		{
			Nearest = *It;
		}
	}
	if (Nearest)
	{
		FaceToward(Nearest->GetActorLocation());
		FTOFighting::Swing(this, FTOFighting::PickBrawlerMove(Rng), nullptr, Nearest, true);
	}
}

void AFTOPerp::BeginStruggle(AFTOCharacter* Officer)
{
	GetWorldTimerManager().ClearTimer(ResumeTimer);
	bHandsUp = false;
	bChatting = false;
	bShooting = false;
	AimPitch = 0.f;

	// Squared up, face to face.
	const FVector Here = GetActorLocation();
	FVector ToOfficer = (Officer->GetActorLocation() - Here).GetSafeNormal2D();
	if (ToOfficer.IsNearlyZero())
	{
		ToOfficer = GetActorForwardVector();
	}
	Hold();
	FaceYaw(ToOfficer.Rotation().Yaw);
	ArrestState = EFTOPerpArrest::Struggling;
	bInCrowd = false;
	Arrester = Officer;
	StruggleMeter = StruggleStart;
	StruggleEndTime = GetWorld()->GetTimeSeconds() + StruggleMaxSeconds;
	Officer->BeginSyncedAction(EFTOAnimAction::Struggle, Here - FVector(0.f, 0.f, HalfHeight) + ToOfficer * StruggleDistance, (-ToOfficer).Rotation().Yaw, this);

	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastPlaySound(AFTOGameState::Sounds().Scuffle, Here, 1.f);
	}
	if (AFTOPlayerController* PC = PCOf(Officer))
	{
		PC->ClientToast(INVTEXT("They're fighting back! Mash E to wrestle them down!"), Warning);
	}
	ToastOfficersNear(INVTEXT("Your partner's wrestling a suspect: pile in (E)!"), Warning, 2500.f, Officer);
}

void AFTOPerp::Mash(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (ArrestState != EFTOPerpArrest::Struggling || !Officer)
	{
		return;
	}
	// The officer in the thick of it heaves hardest; a partner piling in helps too (and is seen to).
	const bool bPartner = Officer != Arrester;
	StruggleMeter = FMath::Min(1.f, StruggleMeter + StruggleHeave * (bPartner ? 0.75f : 1.f));
	if (bPartner)
	{
		Officer->PlayTimedAction(EFTOAnimAction::Struggle, 0.5f);
	}
	if (StruggleMeter >= 1.f)
	{
		EndStruggle(true);
	}
}

void AFTOPerp::EndStruggle(bool bOfficersWon)
{
	AFTOCharacter* Officer = Arrester;
	if (bOfficersWon && IsArresterWithUs())
	{
		// Wrestled down: turned round, onto their knees, and the cuffs go straight on.
		ToastOfficersNear(INVTEXT("Got 'em! On with the cuffs."), GoodNews, 2500.f);
		BeginCuffing(Officer);
		return;
	}

	// They broke free: the officer's shoved over and the suspect legs it.
	ReleaseArrester();
	if (Officer && Officer->GetKnockdown() && !Officer->GetKnockdown()->IsDown())
	{
		const FVector Shove = (Officer->GetActorLocation() - GetActorLocation()).GetSafeNormal2D() * 420.f + FVector(0.f, 0.f, 260.f);
		Officer->GetKnockdown()->Knockdown(Shove, 1.2f);
		if (AFTOPlayerController* PC = PCOf(Officer))
		{
			PC->ClientToast(INVTEXT("They shoved you over and ran! After them: sprint (Shift) and tackle (F)!"), BadNews);
		}
	}
	ToastOfficersNear(INVTEXT("The suspect broke free and ran! Tackle them (F)!"), Warning, 3000.f, Officer);
	BeginFleeing(Officer);
}

void AFTOPerp::BeginFleeing(const AActor* From)
{
	// A burglar bolting for the front door (still in the building) runs into whoever's guarding it.
	if (Incident && Incident->GetInfo().TemplateId == TEXT("Burglary") && Incident->GetBuildingIndex() != INDEX_NONE)
	{
		const AFTOCityGenerator* TheCity = FindCity();
		const FFTOBuilding* Building = TheCity ? TheCity->GetBuilding(Incident->GetBuildingIndex()) : nullptr;
		AFTOCharacter* Guard = Building && Building->Contains(GetActorLocation() - FVector(0.f, 0.f, HalfHeight), 30.f) ? DoorGuard(From) : nullptr;
		if (Guard)
		{
			CaughtAtTheDoor(Guard);
			return;
		}
	}
	if (bCornered)
	{
		// Found upstairs: there's no way out but past the police. They give up.
		ToastOfficersNear(INVTEXT("Nowhere to run up here: they've given up. Cuff them (E)!"), ArrestBlue, 3000.f);
		GiveUp(nullptr);
		return;
	}
	for (TActorIterator<AFTOCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetTalkingTo() == this)
		{
			It->EndTalk();
		}
	}
	GetWorldTimerManager().ClearTimer(ResumeTimer);
	ReleaseArrester();
	bHandsUp = false;
	bChatting = false;
	bShooting = false;
	AimPitch = 0.f;
	ArrestState = EFTOPerpArrest::Fleeing;
	bInCrowd = false;
	if (Incident && Incident->IsCrowd())
	{
		Incident->SetCrowd(false);
	}
	bHidingUpstairs = false;
	if (Incident && Incident->IsHiddenInside())
	{
		Incident->SuspectFound(GetActorLocation() - FVector(0.f, 0.f, HalfHeight), false);
	}
	FleeStartTime = GetWorld()->GetTimeSeconds();
	FarFromOfficersTime = 0.f;
	RunSpeed = FleeSpeed;
	bWandering = false;
	bDeedWalking = false;
	DeedStops.Reset();
	bOnSidewalks = false;
	PrevFleeCorner = FIntVector(-1, -1, -1);
	FleeExit.Reset();

	// Indoors, out through the front door first.
	const FVector Up(0.f, 0.f, HalfHeight);
	const AFTOCityGenerator* TheCity = FindCity();
	const FFTOBuilding* Building = TheCity && Incident->IsIndoors() ? TheCity->GetBuilding(Incident->GetBuildingIndex()) : nullptr;
	if (Building && Building->Contains(GetActorLocation() - Up, 30.f))
	{
		FleeExit.Add(Building->Room.TransformPosition(FVector(70.f, 0.f, 0.f)) + Up);
		FleeExit.Add(Building->DoorOutside + Up);
	}
	Incident->StartFootChase();
	RunToNextWaypoint();
}

void AFTOPerp::RunToNextWaypoint()
{
	const FVector Up(0.f, 0.f, HalfHeight);
	if (FleeExit.Num() > 0)
	{
		FleeTarget = FleeExit[0];
		FleeExit.RemoveAt(0);
		MoveTo(FleeTarget, RunSpeed);
		return;
	}
	AFTOCityGenerator* TheCity = FindCity();
	if (!TheCity)
	{
		Hold();
		return;
	}

	// Round the sidewalk corners (along the block, or over the road at the crossing), always the way that ends up
	// furthest from the nearest officer, never straight back where we came from, and never back past them.
	float OfficerDistance = 0.f;
	const AActor* Chaser = FindNearestOfficer(OfficerDistance);
	const FVector Here = GetActorLocation();
	const FVector Threat = Chaser ? Chaser->GetActorLocation() : Here - GetActorForwardVector() * 1000.f;

	TArray<FIntVector> Options;
	if (!bOnSidewalks)
	{
		for (const FFTOCityBlock& Block : TheCity->GetBlocks())
		{
			for (int32 Side = 0; Side < 4; ++Side)
			{
				Options.Add(FIntVector(Block.X, Block.Y, Side));
			}
		}
	}
	else
	{
		static const int32 MirrorX[] = { 1, 0, 3, 2 };
		static const int32 MirrorY[] = { 3, 2, 1, 0 };
		const FIntVector At = FleeCorner;
		Options.Add(FIntVector(At.X, At.Y, (At.Z + 1) % 4));
		Options.Add(FIntVector(At.X, At.Y, (At.Z + 3) % 4));
		const int32 DX = (At.Z == 1 || At.Z == 2) ? 1 : -1;
		const int32 DY = (At.Z == 2 || At.Z == 3) ? 1 : -1;
		if (At.X + DX >= 0 && At.X + DX <= TheCity->NumIntersectionsX() - 2)
		{
			Options.Add(FIntVector(At.X + DX, At.Y, MirrorX[At.Z]));
		}
		if (At.Y + DY >= 0 && At.Y + DY <= TheCity->NumIntersectionsY() - 2)
		{
			Options.Add(FIntVector(At.X, At.Y + DY, MirrorY[At.Z]));
		}
	}

	const FVector ToThreat = (Threat - Here).GetSafeNormal2D();
	const float ThreatDistance = FVector::Dist2D(Here, Threat);
	FIntVector Best = Options.Num() > 0 ? Options[0] : FleeCorner;
	float BestScore = -TNumericLimits<float>::Max();
	for (const FIntVector& Option : Options)
	{
		const FVector Spot = TheCity->GetSidewalkCorner(Option.X, Option.Y, Option.Z);
		const float Travel = FVector::Dist2D(Spot, Here);
		// Getting onto the sidewalks, the nearest way out that's away from the officer; after that, whichever corner
		// leaves them furthest behind.
		float Score = FVector::Dist2D(Spot, Threat) - (bOnSidewalks ? 0.f : Travel * 1.5f) + Rng.FRandRange(0.f, 150.f);
		if (FVector::DotProduct((Spot - Here).GetSafeNormal2D(), ToThreat) > 0.6f && Travel > ThreatDistance * 0.5f)
		{
			Score -= 5000.f; // that's back past them
		}
		if (Option == PrevFleeCorner)
		{
			Score -= 3000.f;
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Option;
		}
	}
	PrevFleeCorner = bOnSidewalks ? FleeCorner : FIntVector(-1, -1, -1);
	FleeCorner = Best;
	bOnSidewalks = true;
	FleeTarget = TheCity->GetSidewalkCorner(Best.X, Best.Y, Best.Z) + Up;
	MoveTo(FleeTarget, RunSpeed);
}

void AFTOPerp::OnArrived()
{
	if (ArrestState == EFTOPerpArrest::Fleeing)
	{
		RunToNextWaypoint();
	}
	else if (ArrestState == EFTOPerpArrest::Hiding)
	{
		if (bInCrowd)
		{
			FaceYaw(HomeYaw + Rng.FRandRange(-60.f, 60.f));
		}
		else
		{
			ContinueHiding();
		}
	}
	else if (ArrestState == EFTOPerpArrest::None)
	{
		// At the next stop of the crime: square up to the bin, or pause at the shelf (facing the way the crime does).
		const bool bWasWalking = bDeedWalking;
		bDeedWalking = false;
		if (bWasWalking && SmashISM.IsValid())
		{
			FaceToward(SmashAt);
			SmashTime = GetWorld()->GetTimeSeconds() + 1.2f;
		}
		else if (bWasWalking && DeedStops.Num() > 1 && Incident && Incident->GetInfo().TemplateId == TEXT("Jaywalking"))
		{
			FaceToward(DeedStops[(DeedStop + 1) % DeedStops.Num()]);
			DeedPauseUntil = GetWorld()->GetTimeSeconds() + Rng.FRandRange(1.5f, 3.f);
		}
		else
		{
			FaceYaw(HomeYaw); // deliberately not the pedestrian's stroll to the next corner
			DeedPauseUntil = GetWorld()->GetTimeSeconds() + Rng.FRandRange(2.5f, 5.f);
		}
	}
}

void AFTOPerp::Subdued(AController* ByPolice)
{
	check(HasAuthority());
	if (!bCriminal)
	{
		return; // a citizen knocked flat at a call isn't arrested (FTOImpact has already scolded whoever did it)
	}
	// Whatever was going on (a struggle, being cuffed, a chase) is over: once they're up they kneel for the cuffs.
	if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(ByPolice); PC && ArrestState != EFTOPerpArrest::Surrendered)
	{
		PC->ClientToast(INVTEXT("Suspect down! Cuff them (E)."), ArrestBlue);
	}
	GiveUp(ByPolice);
}

void AFTOPerp::GiveUp(AController* ByPolice)
{
	check(HasAuthority());
	GetWorldTimerManager().ClearTimer(ResumeTimer);
	GetWorldTimerManager().ClearTimer(CuffTimer);
	ReleaseArrester();
	ArrestState = EFTOPerpArrest::Surrendered;
	bInCrowd = false;
	bShooting = false;
	bHandsUp = false;
	bChatting = false;
	AimPitch = 0.f;
	AloneTime = 0.f;
	FleeExit.Reset();
	bDeedWalking = false;
	DeedStops.Reset();
	if (!(Knockdown && Knockdown->IsDown()))
	{
		Hold(); // on their knees right here (a body on the floor kneels where it comes to rest)
	}
	if (Incident)
	{
		Incident->Subdue(ByPolice);
	}
}

void AFTOPerp::TickArrest(float DeltaSeconds)
{
	if (!Incident || !Incident->IsActive())
	{
		// The call's over (gone cold, say) mid-arrest: let the officer go rather than leave them locked to us.
		if (ArrestState == EFTOPerpArrest::Cuffing || ArrestState == EFTOPerpArrest::Struggling || ArrestState == EFTOPerpArrest::Fighting)
		{
			GetWorldTimerManager().ClearTimer(CuffTimer);
			ReleaseArrester();
			ArrestState = EFTOPerpArrest::Surrendered;
		}
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	switch (ArrestState)
	{
	case EFTOPerpArrest::Fighting:
		TickFighting(DeltaSeconds);
		return;

	case EFTOPerpArrest::None:
		// A bar fight or a street brawl before the police get there: trading blows with the others.
		if (Incident->GetState() == EFTOIncidentState::Unreported || Incident->GetState() == EFTOIncidentState::Reported)
		{
			if (Incident->IsBrawl())
			{
				TickBrawl(DeltaSeconds);
			}
		}
		else if (Incident->IsBrawl() && !Incident->IsSubdued() && Incident->GetOfficersOnScene() < Incident->GetMinCrew())
		{
			TickBrawl(DeltaSeconds); // one officer isn't enough to break it up
		}
		break;

	case EFTOPerpArrest::Struggling:
		if (!IsArresterWithUs())
		{
			// The officer was knocked flat (or left): free, and off.
			BeginFleeing(nullptr);
			return;
		}
		StruggleMeter = FMath::Max(0.f, StruggleMeter - StruggleDrain * DeltaSeconds);
		if (StruggleMeter <= 0.f || Now >= StruggleEndTime)
		{
			EndStruggle(false);
		}
		return;

	case EFTOPerpArrest::Cuffing:
		if (!IsArresterWithUs())
		{
			// Interrupted: back to waiting on our knees.
			GetWorldTimerManager().ClearTimer(CuffTimer);
			Arrester = nullptr;
			ArrestState = EFTOPerpArrest::Surrendered;
			AloneTime = 0.f;
		}
		return;

	default:
		break;
	}

	// The rest is checked a few times a second.
	ArrestCheckAccumulator += DeltaSeconds;
	if (ArrestCheckAccumulator < 0.25f)
	{
		return;
	}
	const float Step = ArrestCheckAccumulator;
	ArrestCheckAccumulator = 0.f;
	float OfficerDistance = 0.f;
	AActor* Officer = FindNearestOfficer(OfficerDistance);

	switch (ArrestState)
	{
	case EFTOPerpArrest::None:
		// Hiding in the building: an officer who gets a look at us has found us.
		if (Incident->IsHiddenInside())
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				AFTOCharacter* Cop = It->IsValid() ? Cast<AFTOCharacter>((*It)->GetPawn()) : nullptr;
				if (Cop && !Cop->GetCurrentVehicle() && FMath::Abs(Cop->GetActorLocation().Z - GetActorLocation().Z) < 180.f &&
					FVector::Dist(Cop->GetActorLocation(), GetActorLocation()) < 750.f && CanSee(Cop))
				{
					Found(Cop);
					break;
				}
			}
			break;
		}
		// Some make a run for it the moment they see the police coming.
		if (bCriminal && bWillRun && Officer && OfficerDistance < SpookDistance && ForcedResponse == EFTOArrestResponse::Roll &&
			!(Knockdown && (Knockdown->IsDown() || Knockdown->IsDazed())) && CanSee(Officer))
		{
			ToastOfficersNear(INVTEXT("The suspect's spotted you and bolted! Sprint (Shift) and tackle (F)!"), Warning, 3000.f);
			BeginFleeing(Officer);
		}
		break;

	case EFTOPerpArrest::Hiding:
		// In a pickpocket's crowd: milling about like everyone else.
		if (bInCrowd && !bChatting && !bHandsUp && GetCurrentSpeed() < 1.f && Now >= NextShuffle && !(Knockdown && (Knockdown->IsDown() || Knockdown->IsDazed())))
		{
			NextShuffle = Now + Rng.FRandRange(5.f, 12.f);
			MoveTo(Home + FVector(FMath::RandPointInCircle(60.f), 0.f), 110.f);
		}
		// Lying low. An officer getting too close makes them nervous, and nerves make people run (in a crowd they
		// keep their cool better: that's the point of a crowd).
		if (Officer && !bInCrowd && OfficerDistance < NervousDistance && ForcedResponse == EFTOArrestResponse::Roll && !(Knockdown && Knockdown->IsDown()) &&
			Rng.FRand() < BoltChancePerSecond * Step && CanSee(Officer))
		{
			ToastOfficersNear(FText::Format(INVTEXT("That's the {0} suspect! They're running! Sprint (Shift) and tackle (F)!"), Incident->GetInfo().Title), Warning, 3000.f);
			BeginFleeing(Officer);
		}
		break;

	case EFTOPerpArrest::Surrendered:
		// Left kneeling with nobody about, they slip away.
		AloneTime = OfficerDistance > 2000.f ? AloneTime + Step : 0.f;
		if (AloneTime >= WaitForCuffsSeconds)
		{
			GetAway();
		}
		break;

	case EFTOPerpArrest::Fleeing:
		if (Knockdown && Knockdown->IsDown())
		{
			break;
		}
		FarFromOfficersTime = OfficerDistance > GetAwayDistance ? FarFromOfficersTime + Step : 0.f;
		if (FarFromOfficersTime >= GetAwaySeconds)
		{
			GetAway();
		}
		else if (Now - FleeStartTime >= FleeSeconds)
		{
			ToastOfficersNear(INVTEXT("The suspect's given up, out of puff. Cuff them (E)!"), ArrestBlue, 4000.f);
			GiveUp(nullptr);
		}
		break;

	default:
		break;
	}
}

void AFTOPerp::GetAway()
{
	// Clean away: lying low in the crowd, and the squad's got a search on its hands.
	ToastOfficersNear(INVTEXT("The suspect got away! They'll be lying low nearby: find them by the description."), BadNews, 1000000.f);
	GoIntoHiding(true);
}

bool AFTOPerp::CanSee(const AActor* Other) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOPerpSees), false, this);
	Params.AddIgnoredActor(Other);
	if (Incident)
	{
		Params.AddIgnoredActor(Incident);
	}
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation() + FVector(0.f, 0.f, 60.f), Other->GetActorLocation() + FVector(0.f, 0.f, 40.f), ECC_Visibility, Params);
}

void AFTOPerp::ReleaseArrester()
{
	if (IsArresterWithUs())
	{
		Arrester->EndSyncedAction();
	}
	Arrester = nullptr;
}

bool AFTOPerp::IsArresterWithUs() const
{
	return IsValid(Arrester) && Arrester->IsInSyncedAction() && Arrester->GetSyncedPartner() == this;
}

AActor* AFTOPerp::FindNearestOfficer(float& OutDistance) const
{
	// Officers on foot, and drivers (whose pawn is the cruiser).
	AActor* Best = nullptr;
	OutDistance = TNumericLimits<float>::Max();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Pawn = It->IsValid() ? (*It)->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		const float Distance = FVector::Dist2D(Pawn->GetActorLocation(), GetActorLocation());
		if (Distance < OutDistance)
		{
			OutDistance = Distance;
			Best = Pawn;
		}
	}
	return Best;
}

void AFTOPerp::ToastOfficersNear(const FText& Message, const FLinearColor& Color, float Radius, const AActor* Except) const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AFTOPlayerController* PC = Cast<AFTOPlayerController>(It->Get());
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (Pawn && Pawn != Except && FVector::DistSquared2D(Pawn->GetActorLocation(), GetActorLocation()) <= FMath::Square(Radius))
		{
			PC->ClientToast(Message, Color);
		}
	}
}

// ------------------------------------------------------------------------------------------
// Pedestrian overrides
// ------------------------------------------------------------------------------------------

void AFTOPerp::FaceOfficer(const AActor* Officer)
{
	// A whistle turns heads, but not someone on their knees or mid-arrest.
	if (ArrestState == EFTOPerpArrest::None || ArrestState == EFTOPerpArrest::Fleeing || ArrestState == EFTOPerpArrest::Hiding)
	{
		Super::FaceOfficer(Officer);
	}
}

void AFTOPerp::Resume()
{
	bChatting = false;
	bHandsUp = false;
	switch (ArrestState)
	{
	case EFTOPerpArrest::Fleeing:
		MoveTo(FleeTarget, RunSpeed); // a whistle only stops them for a moment
		return;
	case EFTOPerpArrest::Hiding:
		if (!bInCrowd)
		{
			ContinueHiding();
		}
		return;
	case EFTOPerpArrest::None:
		break;
	default:
		return; // kneeling, wrestling or being cuffed: staying put
	}

	// A crime that moves about carries on from wherever they are (unless they've been knocked a long way off).
	if (DeedStops.Num() > 0 && FVector::Dist(GetActorLocation(), Home) < 2500.f)
	{
		bDeedWalking = false;
		SmashISM.Reset();
		SmashTime = 0.f;
		DeedPauseUntil = GetWorld()->GetTimeSeconds() + 1.f;
		return;
	}

	// Back to the scene of the crime.
	const float Distance = FVector::Dist(GetActorLocation(), Home);
	if (Distance > 400.f)
	{
		TeleportAndHold(Home);
		FaceYaw(HomeYaw);
	}
	else if (Distance > 5.f)
	{
		MoveTo(Home, 160.f);
	}
	else
	{
		FaceYaw(HomeYaw);
	}
}
