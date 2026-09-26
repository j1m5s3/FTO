#include "Physics/FTOImpact.h"
#include "City/FTOOccupant.h"
#include "City/FTOPedestrian.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Engine/World.h"
#include "Physics/FTOKnockdownComponent.h"

namespace
{
	/** Citizens the city minds seeing hurt (crooks had it coming, officers signed up for it). */
	bool IsCivilian(const AActor* Victim)
	{
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

		AFTOGameState* GS = Victim->GetWorld()->GetGameState<AFTOGameState>();
		if (!GS || !IsCivilian(Victim))
		{
			return true;
		}
		if (!Police)
		{
			GS->AddChaos(Chaos * 0.5f); // a getaway car ploughing through: the city gets angrier
			return true;
		}
		GS->AddChaos(Chaos);
		++GS->CiviliansBowledOver;
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
