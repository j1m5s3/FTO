#include "Physics/FTOImpact.h"
#include "City/FTOOccupant.h"
#include "City/FTOPedestrian.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Crime/FTOPerp.h"
#include "Engine/World.h"
#include "Physics/FTOKnockdownComponent.h"
#include "Scoring/FTOScoring.h"

namespace
{
	/** Citizens the city minds seeing hurt (crooks and perps had it coming, officers signed up for it). */
	bool IsCivilian(const AActor* Victim)
	{
		if (const AFTOPerp* Perp = Cast<AFTOPerp>(Victim))
		{
			return !Perp->IsCriminal();
		}
		if (const AFTOOccupant* Occupant = Cast<AFTOOccupant>(Victim))
		{
			return Occupant->GetRole() != EFTOOccupantRole::Crook && Occupant->GetRole() != EFTOOccupantRole::Officer;
		}
		return Victim && Victim->IsA<AFTOPedestrian>();
	}

	bool Knock(AActor* Victim, const FVector& Launch, float Seconds, AController* Police, float Chaos, const FText& Scolding)
	{
		UFTOKnockdownComponent* Knockdown = Victim ? Victim->FindComponentByClass<UFTOKnockdownComponent>() : nullptr;
		if (!Knockdown || Knockdown->IsDown())
		{
			return false;
		}
		Knockdown->Knockdown(Launch, Seconds);

		// A perp put on the floor by the police is as good as caught.
		if (AFTOPerp* Perp = Cast<AFTOPerp>(Victim); Perp && Police)
		{
			Perp->Subdued(Police);
		}

		AFTOGameState* GS = Victim->GetWorld()->GetGameState<AFTOGameState>();
		if (!GS || !IsCivilian(Victim))
		{
			return true;
		}
		if (!Police)
		{
			GS->AddChaos(Chaos * 0.5f); // a getaway car ploughing through, a perp's stray round: the city gets angrier
			return true;
		}
		GS->AddChaos(Chaos);
		++GS->CiviliansBowledOver;
		FTOScoring::Award(Police, EFTOScore::Collateral, Victim->GetActorLocation() + FVector(0.f, 0.f, 120.f));
		if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(Police))
		{
			PC->ClientToast(FText::Format(INVTEXT("{0} (+{1} chaos)"), Scolding, FText::AsNumber(FMath::RoundToInt(Chaos))), FLinearColor(1.f, 0.45f, 0.3f));
		}
		return true;
	}
}

bool FTOImpact::CanBowlOver(const AActor* Victim)
{
	const UFTOKnockdownComponent* Knockdown = Victim ? Victim->FindComponentByClass<UFTOKnockdownComponent>() : nullptr;
	return Knockdown && !Knockdown->IsDown();
}

bool FTOImpact::RunOver(AActor* Victim, const FVector& CarLocation, const FVector& Velocity, AController* Driver)
{
	const float Speed = Velocity.Size2D();
	if (!Victim || Speed < MinRunOverSpeed)
	{
		return false;
	}
	// Up and away, flung a little off whichever side of the bonnet they were on; the faster, the further.
	const FVector Along = Velocity.GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Along);
	const float Side = FVector::DotProduct(Victim->GetActorLocation() - CarLocation, Right) >= 0.f ? 1.f : -1.f;
	const FVector Launch = Velocity * 0.9f + Right * Side * Speed * 0.25f + FVector(0.f, 0.f, 200.f + Speed * 0.35f);
	const float Seconds = FMath::Clamp(2.5f + Speed / 800.f, 2.5f, 5.f);
	return Knock(Victim, Launch, Seconds, Driver, FMath::RoundToFloat(2.f + Speed / 800.f), INVTEXT("Yikes! Watch where you're driving!"));
}

bool FTOImpact::Tackle(AActor* Victim, const FVector& Direction, AController* Officer)
{
	const FVector Launch = Direction.GetSafeNormal2D() * 520.f + FVector(0.f, 0.f, 300.f);
	return Knock(Victim, Launch, 2.8f, Officer, 1.f, INVTEXT("Easy, officer! That was a citizen."));
}

bool FTOImpact::Strike(AActor* Victim, const FVector& Launch, float Seconds, AController* Police)
{
	return Knock(Victim, Launch, Seconds, Police, 1.5f, INVTEXT("Hands off! That was a citizen."));
}

void FTOImpact::Roughed(AActor* Victim, AController* Police)
{
	AFTOGameState* GS = Victim && Police ? Victim->GetWorld()->GetGameState<AFTOGameState>() : nullptr;
	if (!GS || !IsCivilian(Victim))
	{
		return;
	}
	GS->AddChaos(0.3f);
	// (Told once in a while, not for every punch.)
	static TMap<TWeakObjectPtr<AController>, float> LastTold;
	const float Now = Victim->GetWorld()->GetTimeSeconds();
	for (auto It = LastTold.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	float& Told = LastTold.FindOrAdd(Police);
	if (Now - Told > 4.f || Told > Now)
	{
		Told = Now;
		if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(Police))
		{
			PC->ClientToast(INVTEXT("Easy! Roughing up citizens costs chaos."), FLinearColor(1.f, 0.45f, 0.3f));
		}
	}
}

bool FTOImpact::Shot(AActor* Victim, const FVector& Velocity, EFTOWeapon Weapon, AActor* Shooter)
{
	const FFTOWeaponSpec& Spec = FTOWeapons::Spec(Weapon);
	const AFTOCharacter* ShootingOfficer = Cast<AFTOCharacter>(Shooter);
	AController* Police = ShootingOfficer ? ShootingOfficer->GetController() : nullptr;
	AFTOPlayerController* PC = Cast<AFTOPlayerController>(Police);
	const FVector Launch = Velocity.GetSafeNormal() * Spec.Push + FVector(0.f, 0.f, Spec.Push * 0.4f);

	// Officers hit by gunfire go down until a partner helps them up (or they come round on their own).
	if (AFTOCharacter* Officer = Cast<AFTOCharacter>(Victim))
	{
		// (Body armour: half the time, the vest takes it.)
		const AFTOGameState* Precinct = Officer->GetWorld()->GetGameState<AFTOGameState>();
		if (!Spec.bStun && Precinct && Precinct->HasUpgrade(TEXT("BodyArmour")) && FMath::FRand() < 0.5f)
		{
			if (AFTOPlayerController* Hit = Cast<AFTOPlayerController>(Officer->GetController()))
			{
				Hit->ClientToast(INVTEXT("Your vest took that one!"), FLinearColor(0.6f, 0.85f, 1.f));
			}
			return false;
		}
		if (!Officer->GoDown(Launch, Spec.bStun ? 4.f : 12.f))
		{
			return false;
		}
		if (PC)
		{
			PC->ClientToast(INVTEXT("Friendly fire! Check your target."), FLinearColor(1.f, 0.45f, 0.3f));
			PC->ClientHitMarker(true);
			FTOScoring::Award(PC, EFTOScore::FriendlyFire, Officer->GetActorLocation() + FVector(0.f, 0.f, 120.f));
		}
		else if (AFTOGameState* GS = Victim->GetWorld()->GetGameState<AFTOGameState>())
		{
			GS->AddChaos(3.f); // officer down: the city notices
		}
		return true;
	}

	const bool bCivilian = IsCivilian(Victim);
	const FText Scolding = Spec.bStun ? INVTEXT("Zapping citizens isn't community policing.") : INVTEXT("Cease fire! That was a citizen.");
	if (!Knock(Victim, Launch, Spec.KnockSeconds, Police, Spec.bStun ? 2.f : 5.f, Scolding))
	{
		return false;
	}
	if (PC)
	{
		PC->ClientHitMarker(bCivilian);
	}
	return true;
}
