#include "Scoring/FTOScoring.h"
#include "City/FTOCityGenerator.h"
#include "Core/FTOCharacter.h"
#include "EngineUtils.h"
#include "Core/FTOPlayerState.h"
#include "Crime/FTOIncident.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"

int32 FTOScoring::BasePoints(EFTOScore Event, EFTOCrimeTier Tier)
{
	static const int32 ArrestByTier[] = { 100, 200, 400, 800 };
	const int32 TierPoints = ArrestByTier[FMath::Clamp(int32(Tier), 0, 3)];
	switch (Event)
	{
	case EFTOScore::Arrest:       return TierPoints;
	case EFTOScore::Bust:         return TierPoints + 150;
	case EFTOScore::CallHandled:  return TierPoints * 3 / 4;
	case EFTOScore::Assist:       return TierPoints / 4;
	case EFTOScore::CaughtInAct:  return 150;
	case EFTOScore::Booked:       return 150;
	case EFTOScore::Ticket:       return 75;
	case EFTOScore::Revive:       return 150;
	case EFTOScore::Collateral:   return -100;
	case EFTOScore::FriendlyFire: return -200;
	case EFTOScore::WrongfulArrest: return -150;
	case EFTOScore::Teamwork:     return 250;
	default:                      return 0;
	}
}

FString FTOScoring::Label(EFTOScore Event)
{
	switch (Event)
	{
	case EFTOScore::Arrest:       return TEXT("ARREST!");
	case EFTOScore::Bust:         return TEXT("BUSTED!");
	case EFTOScore::CallHandled:  return TEXT("CALL HANDLED");
	case EFTOScore::Assist:       return TEXT("ASSIST");
	case EFTOScore::CaughtInAct:  return TEXT("CAUGHT IN THE ACT!");
	case EFTOScore::Booked:       return TEXT("BOOKED!");
	case EFTOScore::Ticket:       return TEXT("TICKET");
	case EFTOScore::Revive:       return TEXT("REVIVE!");
	case EFTOScore::Collateral:   return TEXT("COLLATERAL");
	case EFTOScore::FriendlyFire: return TEXT("FRIENDLY FIRE");
	case EFTOScore::WrongfulArrest: return TEXT("WRONGFUL ARREST");
	case EFTOScore::Teamwork:     return TEXT("TEAMWORK!");
	default:                      return FString();
	}
}

bool FTOScoring::IsPenalty(EFTOScore Event)
{
	return Event == EFTOScore::Collateral || Event == EFTOScore::FriendlyFire || Event == EFTOScore::WrongfulArrest;
}

float FTOScoring::SquadMultiplier(int32 Streak, bool bTagTeam)
{
	if (Streak < 2)
	{
		return 1.f;
	}
	return FMath::Min(1.5f, 1.f + 0.05f * (Streak - 1) + (bTagTeam ? 0.15f : 0.f));
}

float FTOScoring::ComboMultiplier(int32 Combo)
{
	return 1.f + 0.5f * (FMath::Clamp(Combo, 1, MaxCombo) - 1);
}

AFTOPlayerState* FTOScoring::FindOfficer(const AActor* Who)
{
	if (const AController* Controller = Cast<AController>(Who))
	{
		return Controller->GetPlayerState<AFTOPlayerState>();
	}
	// An officer on foot, or the cruiser a player is driving (the driver's controller possesses it).
	if (const APawn* Pawn = Cast<APawn>(Who))
	{
		if (AFTOPlayerState* PS = Pawn->GetPlayerState<AFTOPlayerState>())
		{
			return PS;
		}
	}
	return nullptr;
}

int32 FTOScoring::Award(const AActor* Who, EFTOScore Event, const FVector& Where, EFTOCrimeTier Tier)
{
	AFTOPlayerState* Officer = FindOfficer(Who);
	if (!Officer || !Officer->HasAuthority())
	{
		return 0;
	}
	return Officer->AddScore(Event, BasePoints(Event, Tier), Where);
}

void FTOScoring::IncidentResolved(const AFTOIncident* Incident, const AActor* Arrester)
{
	if (!Incident)
	{
		return;
	}
	UWorld* World = Incident->GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!GS)
	{
		return;
	}
	const FFTOIncidentInfo Info = Incident->GetInfo();
	const FVector Where = Incident->GetActorLocation() + FVector(0.f, 0.f, 120.f);
	const float Radius = Incident->GetSceneRadius();

	// Who was there: every officer within the scene (on foot or at the wheel), nearest first.
	TArray<TPair<float, AFTOPlayerState*>> OnScene;
	for (APlayerState* PS : GS->PlayerArray)
	{
		AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS);
		const APawn* Pawn = Officer ? Officer->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		const float Distance = FVector::Dist2D(Pawn->GetActorLocation(), Incident->GetActorLocation());
		if (Distance <= Radius * 3.f)
		{
			OnScene.Add({ Distance, Officer });
		}
	}
	OnScene.Sort([](const TPair<float, AFTOPlayerState*>& A, const TPair<float, AFTOPlayerState*>& B) { return A.Key < B.Key; });

	if (!Info.bArrest)
	{
		for (const TPair<float, AFTOPlayerState*>& Entry : OnScene)
		{
			if (Entry.Key <= Radius * 1.5f)
			{
				Entry.Value->AddScore(EFTOScore::CallHandled, BasePoints(EFTOScore::CallHandled, Info.Tier), Where);
			}
		}
		return;
	}

	AFTOPlayerState* Arresting = FindOfficer(Arrester);
	if (!Arresting && OnScene.Num() > 0)
	{
		Arresting = OnScene[0].Value;
	}
	if (!Arresting)
	{
		return;
	}
	const bool bChase = Info.TemplateId == TEXT("CarChase") || Info.TemplateId == TEXT("HeistGetaway") || Info.TemplateId == TEXT("Pursuit");
	const EFTOScore Credit = bChase ? EFTOScore::Bust : EFTOScore::Arrest;
	Arresting->AddScore(Credit, BasePoints(Credit, Info.Tier), Where);
	if (Incident->WasWitnessed())
	{
		Arresting->AddScore(EFTOScore::CaughtInAct, BasePoints(EFTOScore::CaughtInAct), Where + FVector(0.f, 0.f, 60.f));
	}
	for (const TPair<float, AFTOPlayerState*>& Entry : OnScene)
	{
		if (Entry.Value != Arresting && Entry.Key <= Radius * 1.5f)
		{
			Entry.Value->AddScore(EFTOScore::Assist, BasePoints(EFTOScore::Assist, Info.Tier), Where);
		}
	}
}

bool FTOScoring::DebriefSpots(const UWorld* World, int32 Count, TArray<FTransform>& OutFeet, FTransform& OutCamera)
{
	const AFTOCityGenerator* City = nullptr;
	for (TActorIterator<AFTOCityGenerator> It(World); It; ++It)
	{
		City = *It;
		break;
	}
	const FFTOBuilding* Precinct = City ? City->FindBuilding(EFTOBuildingType::Precinct) : nullptr;
	if (!Precinct)
	{
		return false;
	}
	// On the pavement out front, shoulder to shoulder, facing the street (the room's X points in through the door).
	const FVector Out = -Precinct->Room.GetRotation().GetForwardVector().GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Out);
	const FVector Middle = Precinct->DoorOutside + Out * 280.f;
	const FRotator Facing = Out.Rotation();
	OutFeet.Reset();
	for (int32 i = 0; i < Count; ++i)
	{
		OutFeet.Add(FTransform(Facing, Middle + Right * ((i - (Count - 1) * 0.5f) * 150.f)));
	}
	const FVector Eye = Middle + Out * 820.f + FVector(0.f, 0.f, 300.f);
	OutCamera = FTransform((Middle + FVector(0.f, 0.f, 95.f) - Eye).Rotation(), Eye);
	return true;
}

FString FTOScoring::Grade(int32 TeamScore, int32 Officers, bool bSurvived, float PeakChaos, float MinutesOnDuty)
{
	if (!bSurvived)
	{
		return TEXT("F");
	}
	// Points per officer per 10 minutes on the clock (overtime earns more, but isn't an easier S), nudged by how close
	// the city came to boiling over.
	const float PerOfficer = TeamScore / float(FMath::Max(1, Officers)) * 10.f / FMath::Max(10.f, MinutesOnDuty);
	const float Value = PerOfficer * (1.25f - 0.5f * FMath::Clamp(PeakChaos / 100.f, 0.f, 1.f));
	if (Value >= 2200.f) return TEXT("S");
	if (Value >= 1500.f) return TEXT("A");
	if (Value >= 950.f)  return TEXT("B");
	if (Value >= 450.f)  return TEXT("C");
	return TEXT("D");
}

TArray<FString> FTOScoring::AwardsFor(const AFTOPlayerState* Officer, const TArray<const AFTOPlayerState*>& Squad)
{
	TArray<FString> Awards;
	if (!Officer)
	{
		return Awards;
	}
	// Each award goes to whoever leads the squad in it (ties share), as long as they did it at all.
	auto Leads = [&](TFunctionRef<int32(const FFTOOfficerStats&)> Stat) -> bool
	{
		const int32 Mine = Stat(Officer->GetStats());
		if (Mine <= 0)
		{
			return false;
		}
		for (const AFTOPlayerState* Other : Squad)
		{
			if (Other && Stat(Other->GetStats()) > Mine)
			{
				return false;
			}
		}
		return true;
	};
	if (Squad.Num() > 1 && Leads([](const FFTOOfficerStats& S) { return S.Score; }))            { Awards.Add(TEXT("Top Cop")); }
	if (Leads([](const FFTOOfficerStats& S) { return S.Arrests + S.Busts; }))                     { Awards.Add(TEXT("Long Arm of the Law")); }
	if (Leads([](const FFTOOfficerStats& S) { return S.Tickets; }))                               { Awards.Add(TEXT("Traffic Warden")); }
	if (Leads([](const FFTOOfficerStats& S) { return S.Revives; }))                               { Awards.Add(TEXT("Guardian Angel")); }
	if (Leads([](const FFTOOfficerStats& S) { return S.Teamwork; }))                              { Awards.Add(TEXT("Team Player")); }
	if (Leads([](const FFTOOfficerStats& S) { return S.CaughtInAct; }))                           { Awards.Add(TEXT("Eagle Eye")); }
	if (Leads([](const FFTOOfficerStats& S) { return S.BestCombo >= 3 ? S.BestCombo : 0; }))     { Awards.Add(TEXT("On a Roll")); }
	if (Leads([](const FFTOOfficerStats& S) { return S.Collateral + S.FriendlyFire; }))           { Awards.Add(TEXT("Bull in a China Shop")); }
	return Awards;
}
