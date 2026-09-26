#include "Crime/FTOPerp.h"
#include "Crime/FTOIncident.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Physics/FTOKnockdownComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/FTOBallistics.h"

namespace
{
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

EFTOAnimAction AFTOPerp::GetAnimAction() const
{
	if (Knockdown && Knockdown->IsDown())
	{
		return EFTOAnimAction::None;
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
	if (!bArmed || bHandsUp || (Knockdown && (Knockdown->IsDown() || Knockdown->IsDazed())))
	{
		return EFTOAimPose::None;
	}
	// Levelled at the officers, or at whoever's at the counter before they turn up.
	const bool bHoldingUp = Incident && (Incident->GetState() == EFTOIncidentState::Unreported || Incident->GetState() == EFTOIncidentState::Reported);
	return bShooting || bHoldingUp ? EFTOAimPose::Pistol : EFTOAimPose::None;
}

void AFTOPerp::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		TickShooting(DeltaSeconds);
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
}

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

	const bool bCanFight = bArmed && !bHandsUp && IsStillFighting() && Knockdown && !Knockdown->IsDown() && !Knockdown->IsDazed();
	AActor* Officer = bCanFight ? FindTarget() : nullptr;
	if (!Officer)
	{
		if (bShooting)
		{
			bShooting = false;
			AimPitch = 0.f;
			FaceYaw(HomeYaw);
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

void AFTOPerp::Subdued(AController* ByPolice)
{
	check(HasAuthority());
	bShooting = false;
	if (Incident)
	{
		Incident->Subdue(ByPolice);
	}
}

void AFTOPerp::Resume()
{
	bChatting = false;
	bHandsUp = false;
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

void AFTOPerp::OnArrived()
{
	// Deliberately not the pedestrian's stroll to the next corner.
	FaceYaw(HomeYaw);
}
