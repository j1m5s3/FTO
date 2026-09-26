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

/** What happens to a piece of the city when it's broken. */
UENUM()
enum class EFTOBreakKind : uint8
{
	None,
	Shatter,	// glass: gone in a shower of shards
	KnockOff,	// small street furniture sent flying whole (bins, meters, news boxes, mailboxes)
	Burst,		// a hydrant: knocked off, with a fountain where it stood
	Topple,		// tall things tip over from the base (lamp posts, traffic lights, trees)
	Smash		// comes apart in chunks (benches, planters, fences, bushes, bus stops)
};

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

/**
 * The city getting broken: shop windows shot out, bins and hydrants knocked flying, lamp posts and trees felled by
 * cruisers going too fast. The server decides what breaks (and the city holds it against the police when they did
 * it); the list replicates (a fast array: only what's new goes out), and every machine tucks the broken instance
 * away and puts on its own show with UFTODebris, since the pieces flying about are only for looking at.
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

	/** What kind of breakable Component is (None if it isn't: walls, roads, the furniture indoors...). */
	static EFTOBreakKind KindOf(const UPrimitiveComponent* Component);
	/** How fast (cm/s) a car has to hit one to break it. */
	static float BreakSpeed(const UPrimitiveComponent* Component);

	/** Server: break this instance, if it's breakable and still standing. ByWhom: whoever's responsible. */
	bool Break(UPrimitiveComponent* Component, int32 Instance, const FVector& Hit, const FVector& Push, AController* ByWhom);
	/** Server: a round landed on this instance: glass goes at once, small things after a few hits, big ones never. */
	void RoundHit(UPrimitiveComponent* Component, int32 Instance, const FVector& Hit, const FVector& Velocity, EFTOWeapon Weapon, AController* ByWhom);
	/** A driver's own machine, ahead of the server: tuck it away now so their car doesn't bounce off it. */
	void BreakLocally(UPrimitiveComponent* Component, int32 Instance, const FVector& Hit, const FVector& Push);

	/** Every machine: tuck the instance away and put on the show (each piece once). */
	void Apply(const FFTOBrokenPiece& Piece);

	bool IsBroken(FName Component, int32 Instance) const;
	int32 NumBroken() const { return Broken.Items.Num(); }
	/** Instances tucked away on this machine. */
	int32 NumApplied() const { return Applied.Num(); }

	/** How long a burst hydrant gushes. */
	static constexpr float HydrantSeconds = 25.f;

protected:
	virtual void BeginPlay() override;

	AFTOCityGenerator* FindCity();
	/** The fireworks for a fresh break. */
	void Show(EFTOBreakKind Kind, const UInstancedStaticMeshComponent* From, const FTransform& Was, const FLinearColor& Color, const FFTOBrokenPiece& Piece);

	UPROPERTY(Replicated) FFTOBrokenList Broken;

	/** Pieces that arrived before the city was built here (a late joiner), applied once it is. */
	TArray<FFTOBrokenPiece> Pending;
	/** Instances tucked away on this machine. */
	TSet<TPair<FName, int32>> Applied;
	/** Server: every piece broken so far, and the rounds each small prop has soaked up. */
	TSet<TPair<FName, int32>> BrokenKeys;
	TMap<TPair<FName, int32>, int32> Hits;

	UPROPERTY(Transient) TObjectPtr<AFTOCityGenerator> City;
};
