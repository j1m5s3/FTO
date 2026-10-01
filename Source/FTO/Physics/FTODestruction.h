#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Weapons/FTOWeapons.h"
#include "FTODestruction.generated.h"

class AFTOCityGenerator;
class AFTODestruction;
class UInstancedStaticMeshComponent;
class UPrimitiveComponent;
struct FFTOStructure;
enum class EFTOPieceRole : uint8;

/** What happens to a piece of the city when it's broken. */
UENUM()
enum class EFTOBreakKind : uint8
{
	None,
	Shatter,	// glass: gone in a shower of shards
	KnockOff,	// small street furniture sent flying whole (bins, meters, news boxes, mailboxes)
	Burst,		// a hydrant: knocked off, with a fountain where it stood
	Topple,		// tall things tip over from the base (lamp posts, traffic lights, trees)
	Smash,		// comes apart in chunks (benches, planters, fences, bushes, bus stops)
	Crumble		// a building's wall panel: comes apart in lumps of masonry and dust, leaving a hole
};

/** How a broken piece goes, beyond what it is. */
namespace FTOBreakHow
{
	/** Broken where it was hit. */
	constexpr uint8 Hit = 0;
	/** Nothing held it up any more: it drops off the building whole. */
	constexpr uint8 Fall = 1;
	/** Went along with the wall it was in or on (its glass, its awning): just falls away. */
	constexpr uint8 Along = 2;
}

/** One broken instance of the instanced city, as every machine should see it. */
USTRUCT()
struct FFTOBrokenPiece : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The city's instanced component (built identically everywhere, so its name and instance index agree). */
	UPROPERTY() FName Component;
	UPROPERTY() int32 Instance = INDEX_NONE;
	/** Where it was hit, and the push (direction times speed) it was hit with. */
	UPROPERTY() FVector_NetQuantize10 Hit = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize10 Push = FVector::ZeroVector;
	/** Server time it broke (a late joiner skips the fireworks for old breaks). */
	UPROPERTY() float Time = 0.f;
	/** FTOBreakHow. */
	UPROPERTY() uint8 How = FTOBreakHow::Hit;

	void PostReplicatedAdd(const struct FFTOBrokenList& List);
};

USTRUCT()
struct FFTOBrokenList : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY() TArray<FFTOBrokenPiece> Items;
	/** Who applies the pieces as they arrive. */
	UPROPERTY(NotReplicated) TObjectPtr<AFTODestruction> Owner;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FFTOBrokenPiece, FFTOBrokenList>(Items, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FFTOBrokenList> : public TStructOpsTypeTraitsBase2<FFTOBrokenList>
{
	enum { WithNetDeltaSerializer = true };
};

/** A building's wall panel that's taken a beating (but is still standing), as every machine should see it. */
USTRUCT()
struct FFTOWallHit : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY() FName Component;
	UPROPERTY() int32 Instance = INDEX_NONE;
	/** How much of it's gone (0-100: at 100 it crumbles). */
	UPROPERTY() uint8 Damage = 0;
	/** The latest knock: where, and the face it landed on. */
	UPROPERTY() FVector_NetQuantize10 Hit = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantizeNormal Normal = FVector::ZeroVector;

	void PostReplicatedAdd(const struct FFTOWallHitList& List);
	void PostReplicatedChange(const struct FFTOWallHitList& List);
};

USTRUCT()
struct FFTOWallHitList : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY() TArray<FFTOWallHit> Items;
	UPROPERTY(NotReplicated) TObjectPtr<AFTODestruction> Owner;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FFTOWallHit, FFTOWallHitList>(Items, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FFTOWallHitList> : public TStructOpsTypeTraitsBase2<FFTOWallHitList>
{
	enum { WithNetDeltaSerializer = true };
};

/** A building (or the top of one) coming down: everything from FromLevel up. */
USTRUCT()
struct FFTOCollapse
{
	GENERATED_BODY()

	/** Which of the city's structures (AFTOCityGenerator::GetStructures). */
	UPROPERTY() int32 Structure = INDEX_NONE;
	/** The storey it gave way at (0: the ground floor, so the whole thing). */
	UPROPERTY() int32 FromLevel = 0;
	UPROPERTY() float Time = 0.f;
};

/**
 * The city getting broken: shop windows shot out, bins and hydrants knocked flying, lamp posts and trees felled by
 * cruisers going too fast, and the buildings themselves. The server decides what breaks (and the city holds it against
 * the police when they did it); the lists replicate (fast arrays: only what's new goes out), and every machine tucks
 * the broken instances away and puts on its own show with UFTODebris, since the pieces flying about are only for
 * looking at.
 *
 * Buildings are structures (FFTOStructure): every facade panel holds up the one above it, and props up its
 * neighbours a panel or two either side. Knocks (cars, rounds, blasts) wear a panel down, cracking it, until it
 * crumbles; whatever's left with nothing under it drops off; and once too much of a storey has gone, everything from
 * there up comes down in one go (a single replicated event that every machine plays out for itself), leaving dust
 * and a heap of rubble on whatever's left.
 */
UCLASS(NotPlaceable)
class FTO_API AFTODestruction : public AActor
{
	GENERATED_BODY()

public:
	AFTODestruction();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	static AFTODestruction* Get(const UWorld* World);

	/** What kind of street breakable Component is (None if it isn't: walls, roads, the furniture indoors...). */
	static EFTOBreakKind KindOf(const UPrimitiveComponent* Component);
	/** How fast (cm/s) a car has to hit one to break it. */
	static float BreakSpeed(const UPrimitiveComponent* Component);

	/** Straight at a building's wall this fast (cm/s), a car goes through it. */
	static constexpr float BreakThroughSpeed = 1800.f;
	/** What a wall panel can take before it crumbles. */
	static constexpr float WallStrength = 100.f;

	/** Server: break this instance, if it's breakable (or a building's wall) and still standing. ByWhom: whoever's responsible. */
	bool Break(UPrimitiveComponent* Component, int32 Instance, const FVector& Hit, const FVector& Push, AController* ByWhom);
	/** Server: a round landed on this instance: glass goes at once, small things after a few hits, walls wear down. */
	void RoundHit(UPrimitiveComponent* Component, int32 Instance, const FVector& Hit, const FVector& Velocity, EFTOWeapon Weapon, AController* ByWhom);
	/** A driver's own machine, ahead of the server: tuck it away now so their car doesn't bounce off it. */
	void BreakLocally(UPrimitiveComponent* Component, int32 Instance, const FVector& Hit, const FVector& Push);

	/** Is this a piece of furniture small enough for a car to send flying (no partitions, counters or vault walls)? */
	static bool IsLoose(const UPrimitiveComponent* Component);
	/** Is this instance part of a building that can come down (and still up)? Its role in the building if so. */
	bool GetStructurePiece(const UPrimitiveComponent* Component, int32 Instance, EFTOPieceRole& OutRole);
	/** The wall panel a building's piece (a pane, or the panel itself) is in, if it's still standing. */
	bool FindWallOf(const UPrimitiveComponent* Component, int32 Instance, UInstancedStaticMeshComponent*& OutWall, int32& OutInstance);
	/**
	 * A car HalfWidth wide either side of Center going through this wall panel: it and whichever panels beside it
	 * the car's as wide as (their walls, and their glass), still standing.
	 */
	void WallsInTheWay(const UPrimitiveComponent* Component, int32 Instance, const FVector& Center, float HalfWidth,
		TArray<TPair<UInstancedStaticMeshComponent*, int32>>& OutPieces);
	/** Server: knock Amount (of WallStrength) off a wall panel, landing at Hit on the face with Normal. */
	void DamageWall(UPrimitiveComponent* Component, int32 Instance, float Amount, const FVector& Hit, const FVector& Normal, const FVector& Push, AController* ByWhom);
	/** Server: a knock of Amount at Point, felt by every wall panel within Radius (less the further off). */
	void DamageAt(const FVector& Point, float Radius, float Amount, AController* ByWhom);
	/** Server: something blew up at At: walls, windows, street furniture, cars and people within Radius feel it. */
	void Blast(const FVector& At, float Radius, float Amount, AController* ByWhom);

	/** Every machine: tuck the instance away and put on the show (each piece once). */
	void Apply(const FFTOBrokenPiece& Piece);
	/** Every machine: a wall panel's cracks and darkening, and the bits coming off it. */
	void ShowWallHit(const FFTOWallHit& Hit);

	bool IsBroken(FName Component, int32 Instance) const;
	int32 NumBroken() const { return Broken.Items.Num(); }
	/** Instances tucked away on this machine. */
	int32 NumApplied() const { return Applied.Num(); }
	/** Has building Index's ground floor (AFTOCityGenerator::GetBuildings) come down? */
	bool IsBuildingDown(int32 BuildingIndex) const;
	/** Has structure Index come down (from FromLevel or below)? */
	bool IsStructureDown(int32 Structure, int32 FromLevel = 0) const;
	int32 NumCollapses() const { return Collapses.Num(); }
	/** Server: how worn a wall panel is (0-WallStrength), for tests. */
	float GetWallDamage(FName Component, int32 Instance) const;
	/** Pieces falling off buildings right now (on this machine), and rubble heaped up. */
	int32 NumFalling() const { return Falling.Num(); }
	int32 NumRubble() const;

	/** How long a burst hydrant gushes. */
	static constexpr float HydrantSeconds = 25.f;

protected:
	virtual void BeginPlay() override;

	AFTOCityGenerator* FindCity();
	/** The fireworks for a fresh break. */
	void Show(EFTOBreakKind Kind, const UInstancedStaticMeshComponent* From, const FTransform& Was, const FLinearColor& Color, const FFTOBrokenPiece& Piece);

	/** Server: add a piece to the broken list (and apply it here). */
	void AddBroken(FName Component, int32 Instance, const FVector& Hit, const FVector& Push, uint8 How);

	// ---- Buildings ----
	/** A building's facade as cells (a panel on a face on a storey), on every machine. */
	struct FCells
	{
		/** Panels round the building, and storeys with walls (the ground floor and those above). */
		int32 Ring = 0;
		int32 Levels = 0;
		/** Per cell (Level * Ring + place round the ring): its pieces, the wall panel first. */
		TArray<TArray<int32>> Pieces;
		/** Per cell: its wall panel's piece (INDEX_NONE: a gap, a doorway). */
		TArray<int32> Wall;
		/** Server, per cell: its wall's gone. */
		TArray<bool> Down;
		/** Server: the lowest storey that's come down. */
		int32 CollapsedFrom = MAX_int32;
	};
	/** Sort every building's pieces into cells, once the city's built here. */
	bool EnsureIndexed();
	const FFTOStructure* GetStructure(int32 Index) const;
	/** Which building and piece an instance is (false if it's not one). */
	bool FindPiece(FName Component, int32 Instance, int32& OutStructure, int32& OutPiece) const;
	/** Server: a wall panel's crumbled: it and everything in its cell goes, then whatever it held up. */
	void CrumbleCell(int32 Structure, int32 Cell, const FVector& Hit, const FVector& Push, AController* ByWhom);
	/** Server: drop whatever's lost its support, and bring down any storey that's too far gone. */
	void Settle(int32 Structure, AController* ByWhom);
	/** Server: everything from FromLevel up comes down. */
	void Collapse(int32 Structure, int32 FromLevel, AController* ByWhom);
	/** Every machine: play a collapse out (pieces tucked away, falling copies, dust, rubble). */
	void ApplyCollapse(const FFTOCollapse& Event);
	/** Every machine: a heap of rubble on Floor (Z) over the structure's footprint. */
	void HeapRubble(const FFTOStructure& S, float FloorZ, int32 Heaps, FRandomStream& Rng);
	/** The falling copy of a city component's pieces (no collision, moved every frame). */
	UInstancedStaticMeshComponent* ProxyFor(const UInstancedStaticMeshComponent* Source);
	void TickFalling(float DeltaSeconds);

	UFUNCTION() void OnRep_Collapses();
	UFUNCTION(NetMulticast, Unreliable) void MulticastBlast(FVector_NetQuantize At, float Radius);

	UPROPERTY(Replicated) FFTOBrokenList Broken;
	UPROPERTY(Replicated) FFTOWallHitList WallHits;
	UPROPERTY(ReplicatedUsing=OnRep_Collapses) TArray<FFTOCollapse> Collapses;

	/** Pieces that arrived before the city was built here (a late joiner), applied once it is. */
	TArray<FFTOBrokenPiece> Pending;
	TArray<FFTOWallHit> PendingHits;
	/** Instances tucked away on this machine. */
	TSet<TPair<FName, int32>> Applied;
	/** Collapses played out on this machine (structure, storey). */
	TSet<FIntPoint> AppliedCollapses;
	/** Server: every piece broken so far, and the rounds each small prop has soaked up. */
	TSet<TPair<FName, int32>> BrokenKeys;
	TMap<TPair<FName, int32>, int32> Hits;
	/** Server: how worn each damaged wall panel is, and where it is in WallHits. */
	TMap<TPair<FName, int32>, float> WallDamage;
	TMap<TPair<FName, int32>, int32> WallHitIndex;
	/** Every machine: how worn a panel last looked, and its paint before it was. */
	TMap<TPair<FName, int32>, uint8> WallShown;
	TMap<TPair<FName, int32>, FLinearColor> CleanPaint;

	TArray<FCells> Cells;
	TMap<TPair<FName, int32>, FIntPoint> PieceIndex;
	bool bIndexed = false;

	/** A piece of a building on its way down (a copy in a proxy; the real one's already tucked away). */
	struct FFalling
	{
		TWeakObjectPtr<UInstancedStaticMeshComponent> Proxy;
		int32 Index = INDEX_NONE;
		FTransform Start;
		float StartTime = 0.f;
		/** Where it's gone once it's dropped this far. */
		float DropTo = 0.f;
		FVector Drift = FVector::ZeroVector;
		FRotator Tumble = FRotator::ZeroRotator;
	};
	TArray<FFalling> Falling;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UInstancedStaticMeshComponent>> Proxies;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Rubble;

	UPROPERTY(Transient) TObjectPtr<AFTOCityGenerator> City;
};
