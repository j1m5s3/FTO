#include "Crime/FTOCrimeExtra.h"
#include "Crime/FTOIncident.h"
#include "Crime/FTOPerp.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Physics/FTOKnockdownComponent.h"
#include "TimerManager.h"

namespace
{
	const TCHAR* Heading(const FVector& Dir)
	{
		const float Yaw = Dir.Rotation().Yaw;
		if (Yaw >= -45.f && Yaw < 45.f) return TEXT("north");
		if (Yaw >= 45.f && Yaw < 135.f) return TEXT("east");
		if (Yaw >= -135.f && Yaw < -45.f) return TEXT("west");
		return TEXT("south");
	}
}

AFTOCrimeExtra::AFTOCrimeExtra()
{
	PrimaryActorTick.bCanEverTick = true;
	TurnRate = 540.f;
}

void AFTOCrimeExtra::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOCrimeExtra, Incident);
	DOREPLIFETIME(AFTOCrimeExtra, ExtraRole);
}

void AFTOCrimeExtra::SetupExtra(AFTOIncident* InIncident, EFTOExtraRole InRole, const FVector& Where, const FVector& FaceAt, int32 Seed)
{
	check(HasAuthority());
	Incident = InIncident;
	ExtraRole = InRole;
	Rng.Initialize(Seed);
	LookSeed = Seed;
	ApplyLook();
	Spot = Where;
	SpotYaw = (FaceAt - Where).Rotation().Yaw;
	TeleportAndHold(Spot);
	FaceYaw(SpotYaw);
	NextKnockdown = GetWorld()->GetTimeSeconds() + Rng.FRandRange(KnockdownEvery.X, KnockdownEvery.Y);
}

bool AFTOCrimeExtra::IsCrimeGoingOn() const
{
	const AFTOPerp* Perp = Incident ? Incident->GetPerp() : nullptr;
	return Perp && Incident->IsActive() && !Incident->IsSubdued() && !Incident->IsSearching() && !Incident->IsFootChase() &&
		Perp->GetArrestState() == EFTOPerpArrest::None && Incident->GetState() != EFTOIncidentState::Responding;
}

EFTOAnimAction AFTOCrimeExtra::GetAnimAction() const
{
	if (Knockdown && (Knockdown->IsDown() || Knockdown->IsDazed()))
	{
		return Knockdown->IsDazed() ? EFTOAnimAction::Dazed : EFTOAnimAction::None;
	}
	if (bChatting || bHandsUp || GetCurrentSpeed() > 1.f)
	{
		return Super::GetAnimAction();
	}
	const bool bGoingOn = IsCrimeGoingOn();
	switch (ExtraRole)
	{
	case EFTOExtraRole::Brawler:
		return bGoingOn ? EFTOAnimAction::Punch : EFTOAnimAction::Cower;
	case EFTOExtraRole::Arguer:
		return EFTOAnimAction::Talk;
	default:
		// A pickpocket's mark hasn't noticed (nose in the map); a mugging victim has their hands up. Once it's over,
		// they're waving the police down.
		if (bGoingOn)
		{
			return Incident && Incident->GetInfo().TemplateId == TEXT("PettyTheft") ? EFTOAnimAction::Interact : EFTOAnimAction::HandsUp;
		}
		return EFTOAnimAction::Talk;
	}
}

void AFTOCrimeExtra::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !Incident)
	{
		return;
	}
	if (ExtraRole != EFTOExtraRole::Brawler || bScarpering || (Knockdown && Knockdown->IsDown()))
	{
		return;
	}
	// The police are here (or the fight's over): scarper.
	if (!IsCrimeGoingOn())
	{
		Scarper();
		return;
	}
	// The perp lands a good one now and then.
	const float Now = GetWorld()->GetTimeSeconds();
	AFTOPerp* Perp = Incident->GetPerp();
	if (Now >= NextKnockdown && Perp && FVector::Dist2D(Perp->GetActorLocation(), GetActorLocation()) < 250.f)
	{
		NextKnockdown = Now + Rng.FRandRange(KnockdownEvery.X, KnockdownEvery.Y);
		const FVector Away = (GetActorLocation() - Perp->GetActorLocation()).GetSafeNormal2D();
		Knockdown->Knockdown(Away * 380.f + FVector(0.f, 0.f, 220.f), 1.4f);
		if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
		{
			GS->MulticastPlaySound(AFTOGameState::Sounds().Bonk, GetActorLocation(), 0.8f);
		}
	}
}

void AFTOCrimeExtra::Scarper()
{
	bScarpering = true;
	GetWorldTimerManager().ClearTimer(ResumeTimer);
	// Indoors they back off into a corner; outdoors they leg it round the block and are gone.
	if (Incident && Incident->IsIndoors())
	{
		return;
	}
	const FVector Away = (GetActorLocation() - (Incident ? Incident->GetActorLocation() : GetActorLocation())).GetSafeNormal2D();
	MoveTo(GetActorLocation() + (Away.IsNearlyZero() ? GetActorForwardVector() : Away) * 2500.f, 480.f);
	SetLifeSpan(8.f);
}

void AFTOCrimeExtra::OnArrived()
{
	if (!bScarpering)
	{
		FaceYaw(SpotYaw);
	}
}

void AFTOCrimeExtra::Resume()
{
	bChatting = false;
	bHandsUp = false;
	if (bScarpering)
	{
		return;
	}
	// Back to where the trouble is (after a tumble, a whistle or a chat).
	if (FVector::Dist(GetActorLocation(), Spot) > 5.f)
	{
		MoveTo(Spot, 160.f);
	}
	else
	{
		FaceYaw(SpotYaw);
	}
}

FText AFTOCrimeExtra::GetInteractPrompt(const AFTOCharacter* Officer) const
{
	return ExtraRole == EFTOExtraRole::Victim ? INVTEXT("Take the victim's statement") : INVTEXT("Talk to them");
}

FString AFTOCrimeExtra::AnswerWhatTheySaw()
{
	// A statement: what happened, what the suspect looks like, which way they went.
	const AFTOPerp* Perp = Incident ? Incident->GetPerp() : nullptr;
	if (ExtraRole != EFTOExtraRole::Victim)
	{
		return ExtraRole == EFTOExtraRole::Brawler ? TEXT("\"They started it! Well, I finished it. Nearly.\"") : TEXT("\"Officer, tell them it's THEIR turn to do the dishes!\"");
	}
	if (Perp && (Incident->IsSearching() || Perp->IsFleeing()))
	{
		// Which way they went: the way they ran off, or where they were last seen once a sighting's moved the search
		// on (the victim can't know where they are now).
		const FVector LastSeen = Incident->GetActorLocation();
		const bool bSightedElsewhere = Incident->IsSearching() && FVector::Dist2D(LastSeen, GetActorLocation()) > 800.f;
		const FVector Dir = ((bSightedElsewhere ? LastSeen : Perp->GetActorLocation()) - GetActorLocation()).GetSafeNormal2D();
		return FString::Printf(TEXT("\"They ran off %s! %s.\""), Heading(Dir), *Incident->GetInfo().SuspectDescription.ToString());
	}
	if (Perp && IsCrimeGoingOn())
	{
		return TEXT("\"Officer! That's them, right there!\"");
	}
	return TEXT("\"Thank goodness you're here. I'm fine, just a bit shaken.\"");
}

FString AFTOCrimeExtra::Contraband()
{
	return FString(); // the victim's clean, whatever the officer thinks
}

FText AFTOCrimeExtra::GetTalkTitle() const
{
	switch (ExtraRole)
	{
	case EFTOExtraRole::Victim:  return FText::FromString(FString::Printf(TEXT("Victim: %s"), *DescribeLook()));
	case EFTOExtraRole::Brawler: return FText::FromString(FString::Printf(TEXT("Brawler: %s"), *DescribeLook()));
	default:                     return FText::FromString(FString::Printf(TEXT("Caller: %s"), *DescribeLook()));
	}
}
