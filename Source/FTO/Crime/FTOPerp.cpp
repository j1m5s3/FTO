#include "Crime/FTOPerp.h"
#include "Crime/FTOIncident.h"
#include "Art/FTOArt.h"
#include "City/FTOCityGenerator.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Physics/FTOKnockdownComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/FTOBallistics.h"

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
		if (Crime == TEXT("BarFight") || Crime == TEXT("Riot") || Crime == TEXT("Vandalism"))
		{
			return EFTOAnimAction::Punch;
		}
		if (Crime == TEXT("NoiseComplaint"))
		{
			return EFTOAnimAction::Dance;
		}
		if (Crime == TEXT("DomesticDispute") || Crime == TEXT("CatInTree"))
		{
			return EFTOAnimAction::Talk; // rowing, or pleading with the cat
		}
		if (Crime == TEXT("Shoplifting") || Crime == TEXT("Burglary") || Crime == TEXT("TerrorPlot") || Crime == TEXT("StolenGoods") ||
			Crime == TEXT("Graffiti") || Crime == TEXT("PettyTheft"))
		{
			return EFTOAnimAction::Work; // rummaging, spraying, fiddling with a ticking thing
		}
		if (IsHoldUp(Crime))
		{
			return EFTOAnimAction::None; // the gun says it all
		}
		return EFTOAnimAction::Interact; // squinting at a map, up to no good
	}

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

	// Perps stay put and turn to face officers quickly.
	TurnRate = 720.f;
}

void AFTOPerp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOPerp, Incident);
	DOREPLIFETIME(AFTOPerp, bCriminal);
	DOREPLIFETIME(AFTOPerp, bArmed);
	DOREPLIFETIME(AFTOPerp, bShooting);
	DOREPLIFETIME(AFTOPerp, AimPitch);
	DOREPLIFETIME(AFTOPerp, ArrestState);
	DOREPLIFETIME(AFTOPerp, Arrester);
	DOREPLIFETIME(AFTOPerp, StruggleMeter);
	DOREPLIFETIME(AFTOPerp, CuffStartTime);
}

void AFTOPerp::Setup(AFTOIncident* InIncident, bool bInCriminal, bool bInArmed, int32 Seed)
{
	check(HasAuthority());
	Incident = InIncident;
	bCriminal = bInCriminal;
	bArmed = bInArmed;
	Rng.Initialize(Seed);
	LookSeed = Seed;
	ApplyLook();
	OnRep_Armed();

	Home = GetActorLocation();
	HomeYaw = GetActorRotation().Yaw;
	TeleportAndHold(Home);
	FaceYaw(HomeYaw);
}

void AFTOPerp::ApplyLook()
{
	if (bCriminal && SuspectLook)
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
	case EFTOPerpArrest::Surrendered:
		// Seeing stars first, if they were put on the floor.
		return Knockdown && Knockdown->IsDazed() ? EFTOAnimAction::Dazed : EFTOAnimAction::Kneel;
	case EFTOPerpArrest::Fleeing:
		return bHandsUp ? EFTOAnimAction::HandsUp : EFTOAnimAction::None; // a whistle stops them for a moment
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
	switch (Incident->GetState())
	{
	case EFTOIncidentState::Unreported:
	case EFTOIncidentState::Reported:
		return DeedFor(Incident->GetInfo().TemplateId);
	case EFTOIncidentState::Responding:
	case EFTOIncidentState::Resolved:
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
		TickShooting(DeltaSeconds);
		TickArrest(DeltaSeconds);
	}

	// The pistol: in hand and pointing where they aim, or tossed away once they give up.
	const bool bShowGun = GetAimPose() != EFTOAimPose::None;
	if (Gun->IsVisible() != bShowGun)
	{
		Gun->SetVisibility(bShowGun);
	}
	if (bShowGun)
	{
		FTOWeapons::HoldInHand(Gun, Body, FRotator(AimPitch, GetActorRotation().Yaw, 0.f));
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
	case EFTOPerpArrest::Struggling:  Mash(Officer); break;
	default:                          TryArrest(Officer); break;
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
	if (Info.Twist == TEXT("Fleeing"))
	{
		return EFTOArrestResponse::Bolt; // on the move, and staying that way
	}
	if (Rng.FRand() < Comply)
	{
		return EFTOArrestResponse::Comply;
	}
	// "Keeps trying to hug the officers": a drunk wrestles, never runs.
	return Info.Twist == TEXT("Drunk") || Rng.FRand() < 0.5f ? EFTOArrestResponse::Struggle : EFTOArrestResponse::Bolt;
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
	GetWorldTimerManager().ClearTimer(ResumeTimer);
	ReleaseArrester();
	bHandsUp = false;
	bChatting = false;
	bShooting = false;
	AimPitch = 0.f;
	ArrestState = EFTOPerpArrest::Fleeing;
	FleeStartTime = GetWorld()->GetTimeSeconds();
	FarFromOfficersTime = 0.f;
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
		MoveTo(FleeTarget, FleeSpeed);
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
	MoveTo(FleeTarget, FleeSpeed);
}

void AFTOPerp::OnArrived()
{
	if (ArrestState == EFTOPerpArrest::Fleeing)
	{
		RunToNextWaypoint();
	}
	else if (ArrestState == EFTOPerpArrest::None)
	{
		FaceYaw(HomeYaw); // deliberately not the pedestrian's stroll to the next corner
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
	bShooting = false;
	bHandsUp = false;
	bChatting = false;
	AimPitch = 0.f;
	AloneTime = 0.f;
	FleeExit.Reset();
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
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	switch (ArrestState)
	{
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
		// On the move (the Fleeing twist): they bolt the moment an officer comes near.
		if (bCriminal && Officer && OfficerDistance < SpookDistance && ForcedResponse == EFTOArrestResponse::Roll &&
			Incident->GetInfo().Twist == TEXT("Fleeing") && !(Knockdown && (Knockdown->IsDown() || Knockdown->IsDazed())))
		{
			ToastOfficersNear(INVTEXT("The suspect's spotted you and bolted! Sprint (Shift) and tackle (F)!"), Warning, 3000.f);
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
	ReleaseArrester();
	ToastOfficersNear(INVTEXT("The suspect got away!"), BadNews, 1000000.f);
	Incident->PerpGotAway();
	// Off round the corner (the incident tidies us away shortly).
	if (ArrestState != EFTOPerpArrest::Fleeing)
	{
		ArrestState = EFTOPerpArrest::Fleeing;
		bOnSidewalks = false;
		RunToNextWaypoint();
	}
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
	if (ArrestState == EFTOPerpArrest::None || ArrestState == EFTOPerpArrest::Fleeing)
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
		MoveTo(FleeTarget, FleeSpeed); // a whistle only stops them for a moment
		return;
	case EFTOPerpArrest::None:
		break;
	default:
		return; // kneeling, wrestling or being cuffed: staying put
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
