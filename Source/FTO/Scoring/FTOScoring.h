#pragma once

#include "CoreMinimal.h"
#include "Crime/FTOCrimeTypes.h"
#include "FTOScoring.generated.h"

class AActor;
class AFTOIncident;
class AFTOPlayerState;

/** Everything an officer scores (or loses) points for. */
UENUM(BlueprintType)
enum class EFTOScore : uint8
{
	Arrest,			// the cuffs went on (points by the crime's tier)
	Bust,			// a car chase ended with the getaway stopped
	CallHandled,	// a call with no crook, handled by being there (everyone on scene)
	Assist,			// on scene for a partner's arrest
	CaughtInAct,	// the crime was seen happening (bonus on top)
	Booked,			// walked a suspect into the cells
	Ticket,			// a traffic stop written up
	Revive,			// helped a downed partner up
	Collateral,		// a citizen hurt by the police (bowled over, shot, zapped)
	FriendlyFire,	// shot a partner
	WrongfulArrest,	// cuffed someone who'd done nothing
	Teamwork		// a job done by two (tyres shot out from the passenger seat, a burglar caught at the door)
};

/** An officer's tally for the end-of-shift scoreboard. */
USTRUCT(BlueprintType)
struct FFTOOfficerStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 Score = 0;
	UPROPERTY(BlueprintReadOnly) int32 Arrests = 0;
	UPROPERTY(BlueprintReadOnly) int32 Busts = 0;
	UPROPERTY(BlueprintReadOnly) int32 CallsHandled = 0;
	UPROPERTY(BlueprintReadOnly) int32 CaughtInAct = 0;
	UPROPERTY(BlueprintReadOnly) int32 Booked = 0;
	UPROPERTY(BlueprintReadOnly) int32 Tickets = 0;
	UPROPERTY(BlueprintReadOnly) int32 Revives = 0;
	UPROPERTY(BlueprintReadOnly) int32 Collateral = 0;
	UPROPERTY(BlueprintReadOnly) int32 FriendlyFire = 0;
	UPROPERTY(BlueprintReadOnly) int32 BestCombo = 1;
	UPROPERTY(BlueprintReadOnly) int32 Teamwork = 0;
};

/**
 * Per-officer scoring. Gameplay code reports what happened and who did it (an officer's pawn, cruiser or
 * controller); the server looks up their player state, applies the combo multiplier (good work in quick succession
 * builds it up, x1.5 per step to x3; any penalty breaks it), adds it to their tally, and every machine shows a
 * "+250 ARREST! x2" popup where it happened. The end-of-shift scoreboard ranks the squad, hands out awards and
 * grades the shift.
 */
namespace FTOScoring
{
	/** Seconds after one award that the next still builds the combo. */
	constexpr float ComboWindow = 12.f;
	constexpr int32 MaxCombo = 5;

	FTO_API int32 BasePoints(EFTOScore Event, EFTOCrimeTier Tier = EFTOCrimeTier::Petty);
	FTO_API FString Label(EFTOScore Event);
	FTO_API bool IsPenalty(EFTOScore Event);
	/** The squad's streak: every officer's good work in quick succession builds it, any penalty breaks it. */
	constexpr float SquadComboWindow = 20.f;
	/** Up to x1.5: 0.1 per step for one officer, less the bigger the squad (more of them make more steps), and another
	 *  0.1 when officers take turns ("tag team"). */
	FTO_API float SquadMultiplier(int32 Streak, bool bTagTeam, int32 Officers = 1);
	/** Events that move the squad's streak on (the bonuses that ride with an arrest don't). */
	FTO_API bool BuildsSquadCombo(EFTOScore Event);
	/** Steps a penalty knocks off the squad's streak. */
	constexpr int32 SquadPenaltySteps = 3;

	/** 1, 1.5, 2, 2.5, 3. */
	FTO_API float ComboMultiplier(int32 Combo);

	/** Whose tally an actor's work goes on: an officer, the cruiser they drive, or their controller. */
	FTO_API AFTOPlayerState* FindOfficer(const AActor* Who);

	/** Server: Who scores Event at Where (Tier sets arrest and call points). Returns the points given. */
	FTO_API int32 Award(const AActor* Who, EFTOScore Event, const FVector& Where, EFTOCrimeTier Tier = EFTOCrimeTier::Petty);

	/**
	 * Server: an incident was handled. Crimes credit the arresting officer (Arrester, else whoever is nearest) with
	 * the arrest (a bust for car chases) and a caught-in-the-act bonus if it was seen happening, and everyone else on
	 * scene with an assist; calls without a crook credit everyone on scene.
	 */
	FTO_API void IncidentResolved(const AFTOIncident* Incident, const AActor* Arrester);

	// ---- Scoreboard ----
	/**
	 * The end-of-shift line-up outside the precinct door: where each of Count officers stands (feet, facing the street)
	 * and the camera looking back at them. The same on every machine (the city is).
	 */
	FTO_API bool DebriefSpots(const UWorld* World, int32 Count, TArray<FTransform>& OutFeet, FTransform& OutCamera);
	/** A letter grade for the shift: S, A, B, C, D or F. */
	FTO_API FString Grade(int32 TeamScore, int32 Officers, bool bSurvived, float PeakChaos, float MinutesOnDuty = 10.f);
	/** Shift awards for the scoreboard, one line per officer who earned one ("Top Cop", "Traffic Warden"...). */
	FTO_API TArray<FString> AwardsFor(const AFTOPlayerState* Officer, const TArray<const AFTOPlayerState*>& Squad);
}
