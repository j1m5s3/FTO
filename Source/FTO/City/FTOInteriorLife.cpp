#include "City/FTOInteriorLife.h"
#include "City/FTOCityGenerator.h"
#include "City/FTOOccupant.h"
#include "Core/FTOGameState.h"
#include "Crime/FTOIncident.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "FTO.h"

namespace
{
	/** Who a kind of building has in it. */
	struct FCrowd
	{
		int32 MaxStaff;
		int32 MinVisitors;
		int32 MaxVisitors;
		/** Chance each post after the first is manned. */
		float StaffChance;
		/** Chance a crook is lying low in here. */
		float CrookChance;
	};

	FCrowd CrowdFor(EFTOBuildingType Type)
	{
		switch (Type)
		{
		case EFTOBuildingType::Shop:      return { 1, 1, 3, 1.f,   0.06f };
		case EFTOBuildingType::Diner:     return { 2, 2, 6, 0.7f,  0.08f };
		case EFTOBuildingType::Bar:       return { 2, 3, 7, 0.6f,  0.25f };
		case EFTOBuildingType::Office:    return { 5, 0, 2, 0.45f, 0.05f };
		case EFTOBuildingType::Home:      return { 0, 1, 3, 0.f,   0.05f };
		case EFTOBuildingType::Warehouse: return { 2, 0, 2, 0.8f,  0.4f };
		case EFTOBuildingType::Bank:      return { 4, 2, 5, 0.8f,  0.03f };
		case EFTOBuildingType::Precinct:  return { 2, 1, 2, 1.f,   0.f };
		}
		return { 1, 0, 2, 1.f, 0.f };
	}

	bool IsArmedCrime(FName Crime)
	{
		static const FName Armed[] = { TEXT("ArmedRobbery"), TEXT("BankHeist"), TEXT("HostageSituation"), TEXT("Standoff"), TEXT("TerrorPlot") };
		for (const FName& Name : Armed)
		{
			if (Crime == Name)
			{
				return true;
			}
		}
		return false;
	}

	/** How people not in the thick of it take trouble in their building. */
	EFTOAnimAction ReactionTo(FName Crime, EFTOOccupantRole Role)
	{
		const bool bStaff = Role == EFTOOccupantRole::Staff || Role == EFTOOccupantRole::Officer;
		if (Role == EFTOOccupantRole::Crook)
		{
			return EFTOAnimAction::None; // seen it all before
		}
		if (IsArmedCrime(Crime))
		{
			return bStaff ? EFTOAnimAction::HandsUp : EFTOAnimAction::Cower;
		}
		if (Crime == TEXT("BarFight") || Crime == TEXT("Riot"))
		{
			return bStaff ? EFTOAnimAction::Talk : EFTOAnimAction::Cheer; // "break it up!" / egging them on
		}
		if (Crime == TEXT("NoiseComplaint"))
		{
			return Role == EFTOOccupantRole::Customer ? EFTOAnimAction::Cheer : EFTOAnimAction::Talk;
		}
		if (Crime == TEXT("Shoplifting"))
		{
			return bStaff ? EFTOAnimAction::Talk : EFTOAnimAction::None;
		}
		if (Crime == TEXT("Burglary") || Crime == TEXT("Vandalism") || Crime == TEXT("DomesticDispute"))
		{
			return EFTOAnimAction::Cower;
		}
		return EFTOAnimAction::None;
	}

	/** Crimes where somebody in the room squares up to the perp, and what they do about it. */
	EFTOAnimAction ConfrontationIn(FName Crime)
	{
		if (Crime == TEXT("BarFight"))
		{
			return EFTOAnimAction::Punch;
		}
		if (Crime == TEXT("DomesticDispute"))
		{
			return EFTOAnimAction::Talk;
		}
		return EFTOAnimAction::None;
	}
}

AFTOInteriorLife::AFTOInteriorLife()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = false;
}

void AFTOInteriorLife::Init(AFTOCityGenerator* InCity, int32 InSeed)
{
	check(HasAuthority());
	City = InCity;
	Seed = InSeed;
	const int32 Count = City ? City->GetBuildings().Num() : 0;
	Rooms.SetNum(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		Rooms[i].Center = City->GetBuildings()[i].GetCenter();
	}
	UE_LOG(LogFTO, Log, TEXT("Interior life: %d rooms, filling within %.0f m of an officer."), Count, WakeRadius / 100.f);
}

AFTOInteriorLife* AFTOInteriorLife::Get(const UWorld* World)
{
	for (TActorIterator<AFTOInteriorLife> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AFTOInteriorLife::GatherWatchers(TArray<FVector>& Out) const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		if (const APawn* Pawn = PC->GetPawn())
		{
			Out.Add(Pawn->GetActorLocation());
		}
		// Remote players' cameras reach the server too (client-side camera updates).
		if (PC->PlayerCameraManager)
		{
			Out.Add(PC->PlayerCameraManager->GetCameraLocation());
		}
	}
}

void AFTOInteriorLife::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!City || Rooms.IsEmpty())
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();

	// Which room each live (or just-finished) incident is in.
	for (FRoom& Room : Rooms)
	{
		if (!Room.Incident.IsValid())
		{
			Room.Incident.Reset();
		}
	}
	if (const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		for (AFTOIncident* Incident : GS->GetIncidents())
		{
			if (Incident && Rooms.IsValidIndex(Incident->GetBuildingIndex()))
			{
				FRoom& Room = Rooms[Incident->GetBuildingIndex()];
				if (!Room.Incident.IsValid() || Incident->IsActive())
				{
					Room.Incident = Incident;
				}
			}
		}
	}

	TArray<FVector> Watchers;
	GatherWatchers(Watchers);
	const float WakeSq = FMath::Square(WakeRadius);
	const float SleepSq = FMath::Square(SleepRadius);

	TArray<TPair<float, int32>> ToWake;
	for (int32 i = 0; i < Rooms.Num(); ++i)
	{
		FRoom& Room = Rooms[i];
		float Nearest = TNumericLimits<float>::Max();
		for (const FVector& Watcher : Watchers)
		{
			Nearest = FMath::Min(Nearest, float(FVector::DistSquared2D(Watcher, Room.Center)));
		}

		if (!Room.bAwake && Nearest < WakeSq)
		{
			ToWake.Emplace(Nearest, i);
		}
		else if (Room.bAwake && Nearest > SleepSq)
		{
			Sleep(i);
		}
		if (Room.bAwake)
		{
			UpdateTrouble(Room, Now);
		}
	}

	// Nearest rooms first, a few a tick.
	ToWake.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
	for (int32 k = 0; k < ToWake.Num() && k < MaxWakesPerTick; ++k)
	{
		Wake(ToWake[k].Value);
		UpdateTrouble(Rooms[ToWake[k].Value], Now);
	}
}

void AFTOInteriorLife::Wake(int32 Index)
{
	const FFTOBuilding* B = City ? City->GetBuilding(Index) : nullptr;
	if (!B || !Rooms.IsValidIndex(Index) || Rooms[Index].bAwake)
	{
		return;
	}
	FRoom& Room = Rooms[Index];
	if (Room.bGone)
	{
		return;
	}
	Room.bAwake = true;
	const FCrowd Crowd = CrowdFor(B->Type);
	const uint32 RoomHash = HashCombine(GetTypeHash(Seed), GetTypeHash(Index));
	FRandomStream Rng(int32(HashCombine(RoomHash, GetTypeHash(Room.Visits++))));

	// Staff at their posts, the first always manned. The same faces every visit: seeded by the post.
	int32 Staff = 0;
	for (int32 s = 0; s < B->WorkSpots.Num() && Staff < Crowd.MaxStaff; ++s)
	{
		if (s == 0 || Rng.FRand() < Crowd.StaffChance)
		{
			const EFTOOccupantRole Job = B->Type == EFTOBuildingType::Precinct ? EFTOOccupantRole::Officer : EFTOOccupantRole::Staff;
			SpawnOccupant(Index, *B, B->WorkSpots[s], Job, int32(HashCombine(RoomHash, GetTypeHash(s + 1000))));
			++Staff;
		}
	}

	// Customers or residents in a few of the other spots, and now and then a crook lying low (on their feet).
	TArray<int32> Free;
	for (int32 s = 0; s < B->VisitSpots.Num(); ++s)
	{
		Free.Add(s);
	}
	for (int32 s = Free.Num() - 1; s > 0; --s)
	{
		Free.Swap(s, Rng.RandRange(0, s));
	}
	const int32 Want = Rng.RandRange(Crowd.MinVisitors, Crowd.MaxVisitors);
	bool bCrook = Rng.FRand() < Crowd.CrookChance;
	int32 Visitors = 0;
	for (const int32 s : Free)
	{
		const FFTOSpot& Spot = B->VisitSpots[s];
		if (bCrook && !Spot.bSeated)
		{
			SpawnOccupant(Index, *B, Spot, EFTOOccupantRole::Crook, Rng.RandRange(1, MAX_int32 - 1));
			bCrook = false;
		}
		else if (Visitors < Want)
		{
			SpawnOccupant(Index, *B, Spot, B->Type == EFTOBuildingType::Home ? EFTOOccupantRole::Resident : EFTOOccupantRole::Customer, Rng.RandRange(1, MAX_int32 - 1));
			++Visitors;
		}
		if (Visitors >= Want && !bCrook)
		{
			break;
		}
	}
}

AFTOOccupant* AFTOInteriorLife::PlantCrook(int32 Index)
{
	const FFTOBuilding* B = City ? City->GetBuilding(Index) : nullptr;
	if (!B)
	{
		return nullptr;
	}
	Wake(Index);
	for (const FFTOSpot& Spot : B->VisitSpots)
	{
		const bool bTaken = Rooms[Index].People.ContainsByPredicate([&Spot](const TWeakObjectPtr<AFTOOccupant>& Person)
		{
			return Person.IsValid() && Person->GetSpot().Transform.GetLocation().Equals(Spot.Transform.GetLocation(), 1.f);
		});
		if (!Spot.bSeated && !bTaken)
		{
			return SpawnOccupant(Index, *B, Spot, EFTOOccupantRole::Crook, int32(HashCombine(GetTypeHash(Seed), GetTypeHash(Index * 31 + 7))) & MAX_int32);
		}
	}
	return nullptr;
}

AFTOOccupant* AFTOInteriorLife::SpawnOccupant(int32 Index, const FFTOBuilding& B, const FFTOSpot& Spot, EFTOOccupantRole Job, int32 PersonSeed)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector At = Spot.Transform.GetLocation() + FVector(0.f, 0.f, AFTOPedestrian::HalfHeight);
	AFTOOccupant* Person = GetWorld()->SpawnActor<AFTOOccupant>(AFTOOccupant::StaticClass(), At, Spot.Transform.Rotator(), Params);
	if (Person)
	{
		Person->Settle(Index, B.Type, Spot, Job, FMath::Max(1, PersonSeed & MAX_int32));
		Rooms[Index].People.Add(Person);
	}
	return Person;
}

void AFTOInteriorLife::Abandon(int32 Index)
{
	if (Rooms.IsValidIndex(Index))
	{
		Sleep(Index);
		Rooms[Index].bGone = true;
	}
}

void AFTOInteriorLife::Sleep(int32 Index)
{
	FRoom& Room = Rooms[Index];
	for (const TWeakObjectPtr<AFTOOccupant>& Person : Room.People)
	{
		if (Person.IsValid())
		{
			Person->Destroy();
		}
	}
	Room.People.Reset();
	Room.Brawler.Reset();
	Room.bAwake = false;
}

void AFTOInteriorLife::UpdateTrouble(FRoom& Room, float Now)
{
	Room.People.RemoveAll([](const TWeakObjectPtr<AFTOOccupant>& Person) { return !Person.IsValid(); });
	AFTOIncident* Incident = Room.Incident.Get();
	const bool bLive = Incident && Incident->IsActive() && !Incident->IsMobile();

	if (Incident && Incident->GetState() == EFTOIncidentState::Resolved && Room.CheerUntil <= 0.f)
	{
		Room.CheerUntil = Now + 3.5f; // hooray for the police
	}
	if (!Incident)
	{
		Room.CheerUntil = 0.f;
	}

	const FName Crime = bLive ? Incident->GetInfo().TemplateId : NAME_None;
	const FVector Perp = Incident ? Incident->GetActorLocation() : Room.Center;

	// Somebody squares up to the perp in a brawl or a row: the nearest punter (never staff or a crook).
	const EFTOAnimAction Squaring = bLive ? ConfrontationIn(Crime) : EFTOAnimAction::None;
	if (Squaring != EFTOAnimAction::None && !Room.Brawler.IsValid())
	{
		float Best = TNumericLimits<float>::Max();
		for (const TWeakObjectPtr<AFTOOccupant>& Person : Room.People)
		{
			const EFTOOccupantRole Job = Person->GetRole();
			if ((Job == EFTOOccupantRole::Customer || Job == EFTOOccupantRole::Resident) && FVector::DistSquared(Person->GetActorLocation(), Perp) < Best)
			{
				Best = FVector::DistSquared(Person->GetActorLocation(), Perp);
				Room.Brawler = Person;
			}
		}
	}
	if (Squaring == EFTOAnimAction::None)
	{
		Room.Brawler.Reset();
	}

	for (const TWeakObjectPtr<AFTOOccupant>& Person : Room.People)
	{
		if (bLive && Person == Room.Brawler)
		{
			const FVector Facing = Incident->GetActorForwardVector();
			Person->Confront(Perp + Facing * 110.f, Perp, Squaring);
		}
		else if (bLive)
		{
			Person->React(ReactionTo(Crime, Person->GetRole()), Perp);
		}
		else if (Now < Room.CheerUntil && Person->GetRole() != EFTOOccupantRole::Crook)
		{
			Person->React(EFTOAnimAction::Cheer, Perp);
		}
		else
		{
			Person->React(EFTOAnimAction::None, Perp);
		}
	}
}

int32 AFTOInteriorLife::GetOccupantCount() const
{
	int32 Count = 0;
	for (const FRoom& Room : Rooms)
	{
		for (const TWeakObjectPtr<AFTOOccupant>& Person : Room.People)
		{
			Count += Person.IsValid() ? 1 : 0;
		}
	}
	return Count;
}

int32 AFTOInteriorLife::GetAwakeCount() const
{
	int32 Count = 0;
	for (const FRoom& Room : Rooms)
	{
		Count += Room.bAwake ? 1 : 0;
	}
	return Count;
}
