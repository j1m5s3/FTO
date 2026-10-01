#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FTOInteriorLife.generated.h"

class AFTOCityGenerator;
class AFTOIncident;
class AFTOOccupant;
struct FFTOBuilding;
struct FFTOSpot;
enum class EFTOOccupantRole : uint8;

/**
 * Server-only: the people inside the city's buildings. Every ground floor has its staff at their posts
 * (clerk, cook, bartender, tellers, office workers, the desk sergeant), customers or residents, and now and
 * then a crook lying low. A room fills when an officer comes within WakeRadius and empties again once every
 * officer is past SleepRadius, so only the few dozen people anyone could see actually exist. The same staff
 * are on shift every time you come back; the customers change.
 *
 * It also plays out crimes indoors: in a hold-up the staff put their hands up and customers cower, a bar
 * fight gets a punter squaring up to the perp while the rest cheer them on, and so on until it's handled.
 */
UCLASS()
class FTO_API AFTOInteriorLife : public AActor
{
	GENERATED_BODY()

public:
	AFTOInteriorLife();

	void Init(AFTOCityGenerator* InCity, int32 InSeed);

	virtual void Tick(float DeltaSeconds) override;

	/** Server: fill building Index now if it's empty (it'll empty again once no officer is near). */
	void Wake(int32 Index);

	/** Server: building Index has come down: whoever was in it is gone, and nobody's in it again. */
	void Abandon(int32 Index);

	/** Server: sneak a crook into building Index, on a free standing spot (tests, scripted trouble). */
	AFTOOccupant* PlantCrook(int32 Index);

	/** How many people are indoors right now, and in how many rooms. */
	int32 GetOccupantCount() const;
	int32 GetAwakeCount() const;

	static AFTOInteriorLife* Get(const UWorld* World);

	/** A room fills when an officer (or their camera) gets this close to it... */
	UPROPERTY(EditAnywhere, Category="Interiors") float WakeRadius = 4500.f;
	/** ...and empties once they're all this far away. */
	UPROPERTY(EditAnywhere, Category="Interiors") float SleepRadius = 6500.f;
	/** Rooms filled per tick at most (spawning is spread out rather than done in one hitch). */
	UPROPERTY(EditAnywhere, Category="Interiors") int32 MaxWakesPerTick = 3;

protected:
	struct FRoom
	{
		TArray<TWeakObjectPtr<AFTOOccupant>> People;
		/** Squaring up to the perp in a brawl or a row. */
		TWeakObjectPtr<AFTOOccupant> Brawler;
		TWeakObjectPtr<AFTOIncident> Incident;
		FVector Center = FVector::ZeroVector;
		/** Bumps every time the room fills, so the customers aren't the same crowd each visit. */
		int32 Visits = 0;
		/** After trouble's handled, everyone cheers the officers until then. */
		float CheerUntil = 0.f;
		bool bAwake = false;
		/** It's come down. */
		bool bGone = false;
	};

	void Sleep(int32 Index);
	AFTOOccupant* SpawnOccupant(int32 Index, const FFTOBuilding& B, const FFTOSpot& Spot, EFTOOccupantRole Job, int32 PersonSeed);
	/** Keeps a room's people reacting to whatever's going on in it. */
	void UpdateTrouble(FRoom& Room, float Now);
	/** Where the officers are, and where their cameras are. */
	void GatherWatchers(TArray<FVector>& Out) const;

	UPROPERTY(Transient) TObjectPtr<AFTOCityGenerator> City;

	TArray<FRoom> Rooms;
	int32 Seed = 0;
};
