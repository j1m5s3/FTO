#include "Combat/FTOFighting.h"
#include "Audio/FTOAudio.h"
#include "City/FTOPathMover.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Crime/FTOArrestee.h"
#include "Crime/FTOPerp.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Physics/FTOImpact.h"
#include "Physics/FTOKnockdownComponent.h"
#include "TimerManager.h"

namespace
{
	/** From fight_timing.json (length, contact time, reach), and our own sense of how hard each one is. */
	const FFTOMoveSpec MoveSpecs[] =
	{
		{},
		{ EFTOAnimAction::Jab,            0.667f, 0.267f, 78.f, 14.f,  220.f, false, TEXT("head"),     TEXT("Punch") },
		{ EFTOAnimAction::Cross,          0.733f, 0.3f,   82.f, 20.f,  280.f, false, TEXT("head"),     TEXT("Punch") },
		{ EFTOAnimAction::Hook,           0.8f,   0.333f, 70.f, 26.f,  320.f, false, TEXT("head"),     TEXT("Punch") },
		{ EFTOAnimAction::Uppercut,       0.867f, 0.367f, 62.f, 34.f,  420.f, true,  TEXT("head"),     TEXT("Punch") },
		{ EFTOAnimAction::KickFront,      1.f,    0.4f,   81.f, 24.f,  450.f, false, TEXT("spine_03"), TEXT("Kick") },
		{ EFTOAnimAction::KickSide,       1.067f, 0.433f, 80.f, 28.f,  520.f, true,  TEXT("spine_03"), TEXT("Kick") },
		{ EFTOAnimAction::KickRoundhouse, 1.133f, 0.433f, 88.f, 40.f,  600.f, true,  TEXT("head"),     TEXT("Kick") },
		{ EFTOAnimAction::Shove,          0.8f,   0.3f,   83.f, 4.f,   520.f, false, TEXT("spine_04"), TEXT("Scuff") },
		{ EFTOAnimAction::FightGrab,      0.8f,   0.333f, 75.f, 0.f,   0.f,   false, TEXT("spine_04"), TEXT("Scuff") },
		{ EFTOAnimAction::Throw,          1.6f,   0.8f,   90.f, 100.f, 650.f, true,  TEXT("pelvis"),   TEXT("BodyFall") },
	};

	/** Which way the blow came from, as they see it: their reel. */
	EFTOAnimAction ReactionFrom(const AActor* Victim, const FVector& TowardAttacker)
	{
		const FVector Local = Victim->GetActorTransform().InverseTransformVectorNoScale(TowardAttacker);
		if (FMath::Abs(Local.X) >= FMath::Abs(Local.Y))
		{
			return Local.X >= 0.f ? EFTOAnimAction::HitLightFront : EFTOAnimAction::HitLightBack;
		}
		return Local.Y >= 0.f ? EFTOAnimAction::HitLightRight : EFTOAnimAction::HitLightLeft;
	}
}

const FFTOMoveSpec& FTOFighting::Spec(EFTOMove Move)
{
	const int32 Index = FMath::Clamp(int32(Move), 0, int32(UE_ARRAY_COUNT(MoveSpecs)) - 1);
	return MoveSpecs[Index];
}

bool FTOFighting::CanSwing(const AActor* Fighter)
{
	const UFTOKnockdownComponent* Knockdown = Fighter ? Fighter->FindComponentByClass<UFTOKnockdownComponent>() : nullptr;
	return Knockdown && !Knockdown->IsDown() && !Knockdown->IsDazed() && !Knockdown->IsBusy();
}

EFTOMove FTOFighting::PickBrawlerMove(FRandomStream& Rng)
{
	const float Roll = Rng.FRand();
	return Roll < 0.3f ? EFTOMove::Jab : Roll < 0.55f ? EFTOMove::Cross : Roll < 0.75f ? EFTOMove::Hook :
		Roll < 0.85f ? EFTOMove::KickFront : Roll < 0.93f ? EFTOMove::Shove : EFTOMove::Uppercut;
}

AActor* FTOFighting::FindTarget(const AActor* Attacker, float Reach, AActor* Only)
{
	if (!Attacker)
	{
		return nullptr;
	}
	const FVector From = Attacker->GetActorLocation();
	const FVector Facing = Attacker->GetActorForwardVector().GetSafeNormal2D();
	// (Centre to centre: their reach, plus the body it lands on.)
	const float InReach = Reach + 45.f;
	auto Fits = [&](const AActor* Who)
	{
		if (!Who || Who == Attacker || Who->IsA<AFTOArrestee>())
		{
			return false;
		}
		const UFTOKnockdownComponent* Knockdown = Who->FindComponentByClass<UFTOKnockdownComponent>();
		const AFTOCharacter* Officer = Cast<AFTOCharacter>(Who);
		if (!Knockdown || Knockdown->IsDown() || (Officer && Officer->GetCurrentVehicle()))
		{
			return false;
		}
		const FVector To = Who->GetActorLocation() - From;
		return FMath::Abs(To.Z) < 120.f && To.Size2D() < InReach && FVector::DotProduct(To.GetSafeNormal2D(), Facing) > 0.45f;
	};
	if (Only)
	{
		return Fits(Only) ? Only : nullptr;
	}
	TArray<FOverlapResult> Near;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOFight), false, Attacker);
	Attacker->GetWorld()->OverlapMultiByObjectType(Near, From + Facing * InReach * 0.5f, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(InReach * 0.6f + 40.f), Params);
	AActor* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FOverlapResult& Overlap : Near)
	{
		AActor* Who = Overlap.GetActor();
		const float Distance = Who ? FVector::DistSquared2D(Who->GetActorLocation(), From) : 0.f;
		if (Fits(Who) && Distance < BestDistance)
		{
			Best = Who;
			BestDistance = Distance;
		}
	}
	return Best;
}

bool FTOFighting::Swing(AActor* Attacker, EFTOMove Move, AController* ByPolice, AActor* Target, bool bStaged)
{
	UFTOKnockdownComponent* Self = Attacker ? Attacker->FindComponentByClass<UFTOKnockdownComponent>() : nullptr;
	if (!Self || Move == EFTOMove::None || !CanSwing(Attacker))
	{
		return false;
	}
	check(Attacker->HasAuthority());
	const FFTOMoveSpec& Thrown = Spec(Move);
	Self->PlayMove(Thrown.Action, Thrown.Length);

	// At the moment of contact: whoever's in reach takes it (unless the swing was knocked out of them first).
	TWeakObjectPtr<AActor> WeakAttacker(Attacker);
	TWeakObjectPtr<AActor> WeakTarget(Target);
	TWeakObjectPtr<AController> WeakPolice(ByPolice);
	const bool bHadTarget = Target != nullptr;
	FTimerHandle Contact;
	Attacker->GetWorldTimerManager().SetTimer(Contact, FTimerDelegate::CreateWeakLambda(Attacker, [WeakAttacker, WeakTarget, WeakPolice, bHadTarget, Move, bStaged]()
	{
		AActor* Who = WeakAttacker.Get();
		const UFTOKnockdownComponent* Knockdown = Who ? Who->FindComponentByClass<UFTOKnockdownComponent>() : nullptr;
		const FFTOMoveSpec& Landing = Spec(Move);
		if (!Knockdown || Knockdown->IsDown() || Knockdown->GetMove() != Landing.Action || (bHadTarget && !WeakTarget.IsValid()))
		{
			return;
		}
		if (AActor* Victim = FindTarget(Who, Landing.Reach, WeakTarget.Get()))
		{
			TakeHit(Victim, Who, Move, WeakPolice.Get(), bStaged);
		}
		else if (AFTOGameState* GS = Who->GetWorld()->GetGameState<AFTOGameState>())
		{
			GS->MulticastPlaySound(FTOAudio::Pick(TEXT("Whoosh")), Who->GetActorLocation() + FVector(0.f, 0.f, 40.f), 0.6f);
		}
	}), Thrown.Contact, false);
	return true;
}

void FTOFighting::TakeHit(AActor* Victim, AActor* Attacker, EFTOMove Move, AController* ByPolice, bool bStaged)
{
	UFTOKnockdownComponent* Knockdown = Victim ? Victim->FindComponentByClass<UFTOKnockdownComponent>() : nullptr;
	if (!Knockdown || Knockdown->IsDown() || !Attacker)
	{
		return;
	}
	const FFTOMoveSpec& Landed = Spec(Move);
	FVector Away = (Victim->GetActorLocation() - Attacker->GetActorLocation()).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = Attacker->GetActorForwardVector();
	}
	if (AFTOGameState* GS = Victim->GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastPlaySound(FTOAudio::Pick(Landed.Sound), Victim->GetActorLocation() + FVector(0.f, 0.f, 50.f), 1.f);
	}

	// A suspect the police lay a hand on decides there and then: fight back, run, or give in.
	if (AFTOPerp* Perp = Cast<AFTOPerp>(Victim); Perp && ByPolice && !bStaged)
	{
		Perp->Provoked(Cast<AFTOCharacter>(Attacker));
	}

	if (AFTOCharacter* Officer = Cast<AFTOCharacter>(Victim))
	{
		Officer->EnterFightStance();
	}

	// A grab: held fast, squirming, right in front of them (the throw comes next).
	if (Move == EFTOMove::Grab)
	{
		const FVector Spot = Attacker->GetActorLocation() + Attacker->GetActorForwardVector().GetSafeNormal2D() * 62.f;
		const FRotator Facing = (-Attacker->GetActorForwardVector().GetSafeNormal2D()).Rotation();
		if (AFTOPathMover* Mover = Cast<AFTOPathMover>(Victim))
		{
			Mover->TeleportAndHold(FVector(Spot.X, Spot.Y, Victim->GetActorLocation().Z));
			Mover->FaceYaw(Facing.Yaw);
		}
		else
		{
			Victim->SetActorLocationAndRotation(FVector(Spot.X, Spot.Y, Victim->GetActorLocation().Z), Facing, false, nullptr, ETeleportType::TeleportPhysics);
		}
		Knockdown->TakeBlow(EFTOAnimAction::Struggle, Spec(EFTOMove::Throw).Contact + Landed.Length, NAME_None, FVector::ZeroVector);
		return;
	}

	// Officers hit harder than the people they're up against.
	const float Grog = bStaged ? 0.f : Knockdown->AddDaze(Landed.Daze * (ByPolice ? 1.f : 0.7f));
	const bool bDown = !bStaged && (Move == EFTOMove::Throw || Grog >= 100.f || (Landed.bHeavy && Grog >= 55.f));
	if (bDown)
	{
		Knockdown->AddDaze(-100.f);
		const FVector Launch = Away * Landed.Push + FVector(0.f, 0.f, Landed.Push * 0.45f + 150.f);
		FTOImpact::Strike(Victim, Launch, Move == EFTOMove::Throw ? 3.2f : 2.6f, ByPolice);
		return;
	}

	// Rocked: they reel the way it came from, the upper body knocked loose for a moment.
	const bool bHeavy = Landed.bHeavy && !bStaged;
	Knockdown->TakeBlow(bHeavy ? EFTOAnimAction::HitHeavy : ReactionFrom(Victim, -Away), bHeavy ? 1.1f : 0.6f, Landed.Bone,
		Away * Landed.Push * 0.7f + FVector(0.f, 0.f, 40.f));
	if (ACharacter* Body = Cast<ACharacter>(Victim))
	{
		Body->LaunchCharacter(Away * Landed.Push * 0.4f, true, false);
	}
	if (ByPolice && !bStaged)
	{
		FTOImpact::Roughed(Victim, ByPolice);
	}
}
