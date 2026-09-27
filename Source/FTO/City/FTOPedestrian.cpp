#include "City/FTOPedestrian.h"
#include "Audio/FTOFootsteps.h"
#include "City/FTOCityGenerator.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Crime/FTOIncident.h"
#include "Crime/FTOArrestee.h"
#include "Crime/FTOCrimeDirector.h"
#include "Crime/FTOPerp.h"
#include "Core/FTOGameMode.h"
#include "Scoring/FTOScoring.h"
#include "Components/CapsuleComponent.h"
#include "Animation/FTOCharacterAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Physics/FTOKnockdownComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Art/FTOArt.h"

namespace
{
	const TCHAR* SmallTalk[] =
	{
		TEXT("\"Lovely day for it, officer.\""),
		TEXT("\"I didn't do it. Whatever it was.\""),
		TEXT("\"Have you tried the donut place on 5th?\""),
		TEXT("\"My cousin's a cop. Well, a mall cop.\""),
		TEXT("\"Is it true you get a free hat?\""),
		TEXT("\"I've never jaywalked in my life. Today.\""),
	};

	/** What sets each of Tools/Blender/build_civilians.py's variants apart, in SK_Civilian_01..08 order. */
	const TCHAR* LookNotes[] =
	{
		TEXT("short brown hair, jeans"),
		TEXT("black hair in a bun, glasses"),
		TEXT("bald, with a beard"),
		TEXT("blonde bob, red trousers, a shoulder bag"),
		TEXT("spiky black hair, red shoes"),
		TEXT("baseball cap, ginger beard"),
		TEXT("grey beanie, glasses, green trousers"),
		TEXT("pink bob, a shoulder bag"),
	};

	/** The shirt tint (FLinearColor::MakeFromHSV8 hue) in words. */
	const TCHAR* HueWord(uint8 Hue)
	{
		if (Hue < 12 || Hue >= 244) return TEXT("red");
		if (Hue < 32) return TEXT("orange");
		if (Hue < 54) return TEXT("yellow");
		if (Hue < 110) return TEXT("green");
		if (Hue < 145) return TEXT("light blue");
		if (Hue < 190) return TEXT("blue");
		if (Hue < 222) return TEXT("purple");
		return TEXT("pink");
	}

	const TCHAR* CompassWord(const FVector& Dir)
	{
		const float Yaw = Dir.Rotation().Yaw;
		if (Yaw >= -45.f && Yaw < 45.f) return TEXT("to the north");
		if (Yaw >= 45.f && Yaw < 135.f) return TEXT("to the east");
		if (Yaw >= -135.f && Yaw < -45.f) return TEXT("to the west");
		return TEXT("to the south");
	}
}

AFTOPedestrian::AFTOPedestrian()
{
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(35.f, HalfHeight);
	Capsule->SetCollisionProfileName(TEXT("Pawn"));
	RootComponent = Capsule;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMat(FTOArt::BaseMaterialPath);
	BaseMaterial = BaseMat.Object;

	for (int32 Index = 1; Index <= 8; ++Index)
	{
		const FString Path = FString::Printf(TEXT("/Game/FTO/Characters/Civilians/SK_Civilian_%02d.SK_Civilian_%02d"), Index, Index);
		ConstructorHelpers::FObjectFinder<USkeletalMesh> Look(*Path);
		if (Look.Succeeded())
		{
			Looks.Add(Look.Object);
		}
	}

	// Blender models face +Y with their feet at the origin.
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Capsule);
	Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -HalfHeight), FRotator(0.f, -90.f, 0.f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Body->SetAnimInstanceClass(UFTOCharacterAnimInstance::StaticClass());
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	Knockdown = CreateDefaultSubobject<UFTOKnockdownComponent>(TEXT("Knockdown"));

	// Softer steps than an officer's, and only close to (a crowd's worth of shoes would be a din).
	Footsteps = CreateDefaultSubobject<UFTOFootsteps>(TEXT("Footsteps"));
	Footsteps->Volume = 0.4f;
	Footsteps->CrowdRange = 1800.f;

	TurnRate = 540.f;
}

void AFTOPedestrian::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOPedestrian, LookSeed);
	DOREPLIFETIME(AFTOPedestrian, bChatting);
	DOREPLIFETIME(AFTOPedestrian, bHandsUp);
	DOREPLIFETIME(AFTOPedestrian, bSearched);
	DOREPLIFETIME(AFTOPedestrian, Found);
}

void AFTOPedestrian::BeginPlay()
{
	Super::BeginPlay();
	Knockdown->OnRecovered.AddUObject(this, &AFTOPedestrian::HandleRecovered);
	OnRep_Look();
}

void AFTOPedestrian::OnRep_Look()
{
	ApplyLook();
}

void AFTOPedestrian::ApplyLook()
{
	if (Looks.Num() == 0)
	{
		return;
	}

	FRandomStream LookRng(LookSeed);
	Body->SetSkeletalMeshAsset(Looks[LookRng.RandRange(0, Looks.Num() - 1)]);

	// Every variant tints its shirt (vertex alpha 1) with a random cheerful colour.
	PaintBody(FLinearColor::MakeFromHSV8(uint8(LookRng.RandRange(0, 255)), 170, 235));
}

FString AFTOPedestrian::DescribeLook() const
{
	// The same rolls as ApplyLook.
	FRandomStream LookRng(LookSeed);
	const int32 Variant = LookRng.RandRange(0, FMath::Max(0, Looks.Num() - 1));
	const uint8 Hue = uint8(LookRng.RandRange(0, 255));
	const TCHAR* Notes = Looks.Num() == UE_ARRAY_COUNT(LookNotes) ? LookNotes[Variant] : TEXT("");
	return *Notes ? FString::Printf(TEXT("%s top, %s"), HueWord(Hue), Notes) : FString::Printf(TEXT("%s top"), HueWord(Hue));
}

void AFTOPedestrian::PaintBody(const FLinearColor& Color)
{
	BodyMaterial = FTOArt::ApplyColor(Body, BaseMaterial, Color);
	for (int32 Slot = 1; Slot < Body->GetNumMaterials(); ++Slot)
	{
		Body->SetMaterial(Slot, BodyMaterial);
	}
}

void AFTOPedestrian::StartWandering(AFTOCityGenerator* InCity, int32 InBlockX, int32 InBlockY, int32 InCorner, int32 InSeed)
{
	check(HasAuthority());
	City = InCity;
	BlockX = InBlockX;
	BlockY = InBlockY;
	Corner = InCorner;
	Rng.Initialize(InSeed);
	LookSeed = InSeed;
	OnRep_Look();

	Direction = Rng.FRand() < 0.5f ? 1 : -1;
	WalkSpeed = Rng.FRandRange(110.f, 190.f);

	const FVector Start = City->GetSidewalkCorner(BlockX, BlockY, Corner) + FVector(0.f, 0.f, HalfHeight);
	Segment.From = Start;
	Segment.To = Start;
	SetActorLocation(Start);
	WalkToNextCorner();
}

void AFTOPedestrian::Resume()
{
	WalkToNextCorner();
}

void AFTOPedestrian::FaceOfficer(const AActor* Officer)
{
	if (Officer)
	{
		FaceToward(Officer->GetActorLocation());
	}
}

FString AFTOPedestrian::GetSmallTalk()
{
	return SmallTalk[Rng.RandRange(0, int32(UE_ARRAY_COUNT(SmallTalk)) - 1)];
}

void AFTOPedestrian::WalkToNextCorner()
{
	bChatting = false;
	bHandsUp = false;

	if (!City)
	{
		return;
	}

	// Mostly stroll around the block; sometimes cross the street to the next block.
	const float Roll = Rng.FRand();
	if (Roll < 0.2f)
	{
		// Cross towards the neighbour across the road from this corner.
		const int32 DX = (Corner == 1 || Corner == 2) ? 1 : -1;
		const int32 DY = (Corner == 2 || Corner == 3) ? 1 : -1;
		const bool bCrossX = Rng.FRand() < 0.5f;
		const int32 NewX = BlockX + (bCrossX ? DX : 0);
		const int32 NewY = BlockY + (bCrossX ? 0 : DY);

		const int32 MaxX = City->NumIntersectionsX() - 2;
		const int32 MaxY = City->NumIntersectionsY() - 2;
		if (NewX >= 0 && NewX <= MaxX && NewY >= 0 && NewY <= MaxY)
		{
			// The mirrored corner on the neighbouring block.
			static const int32 MirrorX[] = { 1, 0, 3, 2 };
			static const int32 MirrorY[] = { 3, 2, 1, 0 };
			BlockX = NewX;
			BlockY = NewY;
			Corner = bCrossX ? MirrorX[Corner] : MirrorY[Corner];
			MoveTo(City->GetSidewalkCorner(BlockX, BlockY, Corner) + FVector(0.f, 0.f, HalfHeight), WalkSpeed * 1.3f);
			return;
		}
	}

	Corner = (Corner + Direction + 4) % 4;
	MoveTo(City->GetSidewalkCorner(BlockX, BlockY, Corner) + FVector(0.f, 0.f, HalfHeight), WalkSpeed);
}

void AFTOPedestrian::OnArrived()
{
	// Occasionally stop to look at a pigeon.
	if (Rng.FRand() < 0.15f)
	{
		Hold();
		GetWorldTimerManager().SetTimer(ResumeTimer, this, &AFTOPedestrian::Resume, Rng.FRandRange(1.5f, 5.f), false);
		return;
	}
	WalkToNextCorner();
}

void AFTOPedestrian::FreezeFor(const AActor* Officer, float Seconds)
{
	check(HasAuthority());
	Hold();
	bHandsUp = true;
	FaceOfficer(Officer);
	GetWorldTimerManager().SetTimer(ResumeTimer, this, &AFTOPedestrian::Resume, Seconds * Rng.FRandRange(0.8f, 1.2f), false);
}

bool AFTOPedestrian::CanInteract(const AFTOCharacter* Officer) const
{
	return Officer != nullptr && !(Knockdown && Knockdown->IsDown());
}

FText AFTOPedestrian::GetInteractPrompt(const AFTOCharacter* Officer) const
{
	return INVTEXT("Talk to citizen");
}

void AFTOPedestrian::Interact(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (!Officer)
	{
		return;
	}
	Officer->BeginTalk(this);
	HoldForTalk(Officer);
}

void AFTOPedestrian::HoldForTalk(AFTOCharacter* Officer)
{
	TalkingWith = Officer;
	GetWorldTimerManager().ClearTimer(ResumeTimer);
	Hold();
	bChatting = true;
	FaceOfficer(Officer);
}

void AFTOPedestrian::TalkEnded(AFTOCharacter* Officer)
{
	if (TalkingWith.Get() != Officer)
	{
		return;
	}
	TalkingWith.Reset();
	GetWorldTimerManager().ClearTimer(SearchPoseTimer);
	bHandsUp = false;
	bChatting = false;
	GetWorldTimerManager().SetTimer(ResumeTimer, this, &AFTOPedestrian::Resume, 1.f, false);
}

FText AFTOPedestrian::GetTalkTitle() const
{
	return FText::FromString(FString::Printf(TEXT("Citizen: %s"), *DescribeLook()));
}

void AFTOPedestrian::GetTalkOptions(const AFTOCharacter* Officer, TArray<FText>& OutOptions) const
{
	OutOptions.Add(INVTEXT("Seen anything unusual round here?"));
	OutOptions.Add(INVTEXT("How's your day going?"));
	if (!bSearched)
	{
		OutOptions.Add(INVTEXT("I'm going to search you."));
	}
	else
	{
		OutOptions.Add(Found.IsEmpty() ? INVTEXT("You're under arrest. (They're clean: the city won't like it.)") : INVTEXT("You're under arrest."));
	}
	OutOptions.Add(INVTEXT("That's all, have a good day."));
}

bool AFTOPedestrian::TalkChoice(AFTOCharacter* Officer, int32 Index)
{
	check(HasAuthority());
	AFTOPlayerController* PC = Officer ? Cast<AFTOPlayerController>(Officer->GetController()) : nullptr;
	if (!PC)
	{
		return false;
	}
	HoldForTalk(Officer);
	switch (Index)
	{
	case 0:
		Officer->PlayTimedAction(EFTOAnimAction::Interact, 1.2f);
		PC->ClientToast(FText::FromString(AnswerWhatTheySaw()), FLinearColor(1.f, 0.85f, 0.3f));
		return true;

	case 1:
		Officer->PlayTimedAction(EFTOAnimAction::Talk, 1.5f);
		PC->ClientToast(FText::FromString(GetSmallTalk()), FLinearColor::White);
		return true;

	case 2:
		if (!bSearched)
		{
			// Hands up for the pat-down.
			bSearched = true;
			Found = Contraband();
			Officer->PlayTimedAction(EFTOAnimAction::Interact, 2.2f);
			bHandsUp = true;
			GetWorldTimerManager().SetTimer(SearchPoseTimer, this, &AFTOPedestrian::EndSearchPose, 2.2f, false);
			if (Found.IsEmpty())
			{
				// Stopped and searched for nothing: people notice.
				if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
				{
					GS->AddChaos(CleanSearchChaos);
				}
				PC->ClientToast(INVTEXT("They're clean. And not happy about being searched."), FLinearColor(0.85f, 0.85f, 0.85f));
			}
			else
			{
				PC->ClientToast(FText::FromString(FString::Printf(TEXT("Found %s!"), *Found)), FLinearColor(1.f, 0.6f, 0.25f));
			}
			return true;
		}
		// Arrest: for what turned up, or (if nothing did) for nothing at all.
		if (Found.IsEmpty())
		{
			WrongfulArrest(Officer);
		}
		else
		{
			ArrestForWhatWasFound(Officer);
		}
		return false;

	default:
		return false;
	}
}

void AFTOPedestrian::EndSearchPose()
{
	bHandsUp = false;
}

FString AFTOPedestrian::AnswerWhatTheySaw()
{
	const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
	if (!GS)
	{
		return TEXT("\"Nothing, officer.\"");
	}
	// A suspect lying low who passed this way: they'll have noticed (and the search moves to where they saw them).
	for (AFTOIncident* Incident : GS->GetIncidents())
	{
		const AFTOPedestrian* Suspect = Incident && Incident->IsSearching() ? Incident->GetPerp() : nullptr;
		if (Suspect && Suspect != this && FVector::Dist2D(Suspect->GetActorLocation(), GetActorLocation()) < 2500.f)
		{
			Incident->ReportSighting(Suspect->GetActorLocation());
			const FVector Dir = (Suspect->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
			return FString::Printf(TEXT("\"Someone like that? %s? Yes, they went %s, not long ago.\""), *Incident->GetInfo().SuspectDescription.ToString(), CompassWord(Dir));
		}
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= ChatCooldownUntil)
	{
		ChatCooldownUntil = Now + 30.f;
		// Tip-off: the nearest crime nobody has called in yet.
		AFTOIncident* Best = nullptr;
		float BestDist = 6000.f;
		for (AFTOIncident* Incident : GS->GetIncidents())
		{
			if (Incident && Incident->GetState() == EFTOIncidentState::Unreported)
			{
				const float Dist = FVector::Dist2D(Incident->GetActorLocation(), GetActorLocation());
				if (Dist < BestDist)
				{
					Best = Incident;
					BestDist = Dist;
				}
			}
		}
		if (Best)
		{
			Best->ForceReport();
			const FVector Dir = (Best->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
			const int32 Meters = FMath::RoundToInt(BestDist / 100.f);
			return Rng.FRand() < 0.5f
				? FString::Printf(TEXT("\"Psst... something shady going on %s, about %dm away.\""), CompassWord(Dir), Meters)
				: FString::Printf(TEXT("\"I saw someone acting weird %s. Like, %dm that way.\""), CompassWord(Dir), Meters);
		}
	}
	return TEXT("\"Nothing, officer. Quiet day. Suspiciously quiet.\"");
}

FString AFTOPedestrian::Contraband()
{
	// One in eight is carrying something they shouldn't (the same one every time you ask).
	static const TCHAR* Items[] =
	{
		TEXT("a stolen wallet (it isn't even theirs, the photo's of a dog)"),
		TEXT("a bag of someone else's phones"),
		TEXT("a crowbar and a map of the bank"),
		TEXT("forty-seven fake designer watches"),
		TEXT("a very illegal amount of fireworks"),
		TEXT("a flick knife"),
	};
	FRandomStream Pockets(LookSeed * 31 + 7);
	return Pockets.FRand() < 0.125f ? FString(Items[Pockets.RandRange(0, UE_ARRAY_COUNT(Items) - 1)]) : FString();
}

void AFTOPedestrian::ArrestForWhatWasFound(AFTOCharacter* Officer)
{
	AFTOGameMode* GM = GetWorld()->GetAuthGameMode<AFTOGameMode>();
	if (!GM || !Officer)
	{
		return;
	}
	// A crime right here, with the same face as its perp, caught red-handed; then the arrest goes as any other would.
	const FTransform Where(FRotator(0.f, (Officer->GetActorLocation() - GetActorLocation()).Rotation().Yaw, 0.f), GetActorLocation() - FVector(0.f, 0.f, HalfHeight));
	AFTOIncident* Caught = GM->GetCrimeDirector()->SpawnIncidentAt(TEXT("StolenGoods"), Where, GetBuildingForCrime(), true);
	if (!Caught)
	{
		return;
	}
	Caught->ReportByOfficer();
	if (AFTOPerp* Perp = Caught->GetPerp())
	{
		Perp->WearLookOf(LookSeed, Found);
		Destroy();
		Perp->Interact(Officer);
		return;
	}
	Destroy();
}

void AFTOPedestrian::WrongfulArrest(AFTOCharacter* Officer)
{
	AFTOPlayerController* PC = Officer ? Cast<AFTOPlayerController>(Officer->GetController()) : nullptr;
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->AddChaos(WrongfulArrestChaos);
	}
	FTOScoring::Award(Officer, EFTOScore::WrongfulArrest, GetActorLocation());
	if (PC)
	{
		PC->ClientToast(INVTEXT("Arrested for... nothing? That's a wrongful arrest: the city's not happy."), FLinearColor(1.f, 0.4f, 0.3f));
	}
	// Cuffed all the same, and walked to the cells (booking them does the city no good).
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AFTOArrestee* Cuffed = GetWorld()->SpawnActor<AFTOArrestee>(AFTOArrestee::StaticClass(), GetActorLocation(), GetActorRotation(), Params))
	{
		Cuffed->Init(Officer, 0.f, INVTEXT("Wrongful arrest"));
		if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
		{
			GS->MulticastPlaySound(AFTOGameState::Sounds().Cuffs, GetActorLocation(), 1.f);
		}
	}
	Destroy();
}

EFTOAnimAction AFTOPedestrian::GetAnimAction() const
{
	if (Knockdown && Knockdown->IsDazed())
	{
		return EFTOAnimAction::Dazed;
	}
	return bHandsUp ? EFTOAnimAction::Cheer : (bChatting ? EFTOAnimAction::Interact : EFTOAnimAction::None);
}

bool AFTOPedestrian::IsMovementFrozen() const
{
	return Knockdown && Knockdown->IsDown();
}

void AFTOPedestrian::HandleRecovered()
{
	if (!HasAuthority())
	{
		return;
	}
	// Dust ourselves off where we landed, then carry on.
	TeleportAndHold(GetActorLocation());
	bChatting = false;
	bHandsUp = false;
	GetWorldTimerManager().SetTimer(ResumeTimer, this, &AFTOPedestrian::Resume, 1.6f, false);
}
