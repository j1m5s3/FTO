// Furnishing for AFTOCityGenerator's ground floors. Everything is laid out in room space: X runs from the
// door (0) to the back wall (Depth), Y from YMin to YMax with the door at 0. A strip in front of the door
// stays clear so people can get in. Where people work, sit and hang about is recorded as spots for the
// interior life (FTOInteriorLife) to fill, along with where a crime in here would play out.
#include "City/FTOCityGenerator.h"
#include "City/FTOCityPalette.h"

using namespace FTOKit;

namespace
{
	// Kept clear in front of the door: |Y| < DoorLane for X < DoorDepth.
	constexpr float DoorLane = 130.f;
	constexpr float DoorDepth = 320.f;

	// Room-space yaws: 0 faces into the room, 180 faces the door, 90 faces +Y, -90 faces -Y.
	constexpr float IntoRoom = 0.f;
	constexpr float ToDoor = 180.f;
	constexpr float ToPosY = 90.f;
	constexpr float ToNegY = -90.f;

	// Where a sitter's root goes in front of each seat piece's origin: SitBack ahead of its backrest.
	constexpr float OnChair = SitBack - 17.5f;
	constexpr float OnDiningChair = SitBack - 16.5f;
	constexpr float OnOfficeChair = SitBack - 19.f;
	constexpr float OnSofa = SitBack - 21.f;
	constexpr float OnStool = 2.f;
	/** A booth's backrests are this far either side of its middle. */
	constexpr float BoothBack = 92.f;
	// Seat tops (cm): chairs and booths are at SeatHeight; the cushier or wheelier things a little higher.
	constexpr float CushionTop = SeatHeight + 2.f;
	// Stools are squashed to suit their counter, so a sitter's hands land on it (the kit's stool is 79 cm).
	constexpr float StoolTop = 79.f;
	constexpr float DinerStool = 62.f;
	constexpr float BarStool = 71.f;
}

FFTOSpot AFTOCityGenerator::StandingSpot(const FFTOBuilding& B, float X, float Y, float Yaw, EFTOAnimAction Action) const
{
	FFTOSpot Spot;
	Spot.Transform = RoomToWorld(B, X, Y, Yaw);
	Spot.Action = Action;
	return Spot;
}

FFTOSpot AFTOCityGenerator::SeatedSpot(const FFTOBuilding& B, float SeatX, float SeatY, float Yaw, float Forward, float SeatTop, EFTOAnimAction Action) const
{
	const float Radians = FMath::DegreesToRadians(Yaw);
	FFTOSpot Spot;
	Spot.Transform = RoomToWorld(B, SeatX + FMath::Cos(Radians) * Forward, SeatY + FMath::Sin(Radians) * Forward, Yaw, SeatTop - SitDrop);
	Spot.Action = Action;
	Spot.bSeated = true;
	return Spot;
}

void AFTOCityGenerator::Furnish(FFTOBuilding& B, FRandomStream& Rng)
{
	switch (B.Type)
	{
	case EFTOBuildingType::Shop:      FurnishShop(B, Rng); break;
	case EFTOBuildingType::Diner:     FurnishDiner(B, Rng); break;
	case EFTOBuildingType::Bar:       FurnishBar(B, Rng); break;
	case EFTOBuildingType::Office:    FurnishOffice(B, Rng); break;
	case EFTOBuildingType::Home:      FurnishHome(B, Rng); break;
	case EFTOBuildingType::Warehouse: FurnishWarehouse(B, Rng); break;
	default: break;
	}
}

void AFTOCityGenerator::FurnishShop(FFTOBuilding& B, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const FLinearColor Brand = Accent(Rng.RandRange(0, 7));
	const bool bBooks = B.Name == TEXT("BOOKS");

	// Counter just inside on the right: the clerk behind it, a customer paying at the till, and across the
	// counter from the clerk is where a hold-up happens.
	PlaceInRoom(B, TEXT("SM_ShopCounter"), 230.f, B.YMax - 150.f, ToNegY, Brand);
	B.WorkSpots.Add(StandingSpot(B, 230.f, B.YMax - 55.f, ToNegY, EFTOAnimAction::Work));
	B.VisitSpots.Add(StandingSpot(B, 320.f, B.YMax - 255.f, ToPosY, EFTOAnimAction::Talk));
	B.CrimeSpots.Add(RoomToWorld(B, 190.f, B.YMax - 290.f, ToPosY));
	PlaceInRoom(B, TEXT("SM_Doormat"), 45.f, 0.f, IntoRoom, Brand);

	// Shelves round the back wall, fridges down the left wall.
	for (float Y = B.YMin + 110.f; Y + 100.f <= B.YMax - 10.f; Y += bBooks ? 105.f : 210.f)
	{
		PlaceInRoom(B, bBooks ? TEXT("SM_Bookshelf") : TEXT("SM_WallShelf"), B.Depth, Y, ToDoor);
	}
	for (int32 k = 0; k < 3; ++k)
	{
		const float X = B.Depth - 150.f - k * 105.f;
		if (X > 420.f)
		{
			PlaceInRoom(B, bBooks ? TEXT("SM_Bookshelf") : TEXT("SM_DrinksFridge"), X, B.YMin, ToPosY);
		}
	}
	B.VisitSpots.Add(StandingSpot(B, B.Depth - 150.f, B.YMin + 110.f, ToNegY, EFTOAnimAction::None)); // at the fridges

	// Aisles of gondolas running back from the front, clear of the counter; a browser in each aisle.
	for (float Y = B.YMin + 250.f; Y <= B.YMax - 360.f; Y += 240.f)
	{
		float LastX = 450.f;
		for (float X = 450.f; X + 100.f <= B.Depth - 170.f; X += 210.f)
		{
			PlaceInRoom(B, TEXT("SM_Shelf"), X, Y, ToPosY);
			LastX = X;
		}
		B.VisitSpots.Add(StandingSpot(B, 550.f, Y + 120.f, ToNegY, EFTOAnimAction::None));
		B.VisitSpots.Add(StandingSpot(B, LastX, Y + 120.f, ToPosY, EFTOAnimAction::None));
	}
	PlaceInRoom(B, TEXT("SM_Plant"), 60.f, B.YMin + 60.f, IntoRoom);
}

void AFTOCityGenerator::FurnishDiner(FFTOBuilding& B, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const FLinearColor Brand = Accent(Rng.RandRange(0, 7));
	const float Width = B.YMax - B.YMin;

	// Counter across the back with stools in front; staff behind.
	const float CounterX = B.Depth - 130.f;
	const int32 Counters = Width > 1000.f ? 2 : 1;
	for (int32 k = 0; k < Counters; ++k)
	{
		const float Y = (k - (Counters - 1) * 0.5f) * 300.f;
		PlaceInRoom(B, TEXT("SM_DinerCounter"), CounterX, Y, ToDoor, Brand);
		B.WorkSpots.Add(StandingSpot(B, B.Depth - 45.f, Y, ToDoor, EFTOAnimAction::Work));
	}
	for (float Y = -Counters * 150.f + 50.f; Y <= Counters * 150.f - 50.f; Y += 75.f)
	{
		FTransform Stool = RoomToWorld(B, CounterX - 75.f, Y, IntoRoom);
		Stool.SetScale3D(FVector(1.f, 1.f, DinerStool / StoolTop));
		Place(TEXT("SM_Stool"), Stool, Brand, true);
		B.VisitSpots.Add(SeatedSpot(B, CounterX - 75.f, Y, IntoRoom, OnStool, DinerStool));
	}

	// Booths down both side walls, somebody on each bench facing across the table.
	for (float X = 140.f; X + 105.f <= CounterX - 170.f; X += 220.f)
	{
		const float Mid = X + 105.f;
		PlaceInRoom(B, TEXT("SM_Booth"), Mid, B.YMin, ToPosY, Brand);
		PlaceInRoom(B, TEXT("SM_Booth"), Mid, B.YMax, ToNegY, Brand);
		for (const float Y : { B.YMin + 75.f, B.YMax - 75.f })
		{
			B.VisitSpots.Add(SeatedSpot(B, Mid - BoothBack, Y, IntoRoom, SitBack, SeatHeight + 1.f));
			B.VisitSpots.Add(SeatedSpot(B, Mid + BoothBack, Y, ToDoor, SitBack, SeatHeight + 1.f));
		}
	}
	PlaceInRoom(B, TEXT("SM_Jukebox"), B.Depth - 20.f, B.YMax - 70.f, ToDoor);
	PlaceInRoom(B, TEXT("SM_Plant"), 60.f, B.YMin + 280.f, IntoRoom);
	PlaceInRoom(B, TEXT("SM_Doormat"), 45.f, 0.f, IntoRoom, Brand);

	// Trouble plays out in the aisle between the booths, facing the counter.
	B.CrimeSpots.Add(RoomToWorld(B, FMath::Max(DoorDepth + 60.f, CounterX - 320.f), 0.f, IntoRoom));
}

void AFTOCityGenerator::FurnishBar(FFTOBuilding& B, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const FLinearColor Brand = Accent(Rng.RandRange(0, 7));

	// The bar runs along the right-hand wall with the bottles behind it.
	const float BarY = B.YMax - 175.f;
	const int32 Sections = FMath::Clamp(FMath::FloorToInt((B.Depth - 420.f) / 300.f), 1, 3);
	for (int32 k = 0; k < Sections; ++k)
	{
		const float X = B.Depth - 170.f - k * 300.f;
		PlaceInRoom(B, TEXT("SM_BarCounter"), X, BarY, ToNegY);
		PlaceInRoom(B, TEXT("SM_BottleShelf"), X, B.YMax, ToNegY);
		B.WorkSpots.Add(StandingSpot(B, X, B.YMax - 95.f, ToNegY, EFTOAnimAction::Work));
		for (float S = -110.f; S <= 110.f; S += 75.f)
		{
			FTransform Stool = RoomToWorld(B, X + S, BarY - 78.f, IntoRoom);
			Stool.SetScale3D(FVector(1.f, 1.f, BarStool / StoolTop));
			Place(TEXT("SM_Stool"), Stool, Brand, true);
			B.VisitSpots.Add(SeatedSpot(B, X + S, BarY - 78.f, ToPosY, OnStool, BarStool));
		}
	}

	// Tables in the rest, a pool table at the back if it fits, darts on the wall.
	const bool bPool = B.Depth > 850.f && BarY - B.YMin > 520.f;
	for (float X = 360.f; X <= B.Depth - (bPool ? 420.f : 150.f); X += 230.f)
	{
		for (float Y = B.YMin + 150.f; Y <= BarY - 260.f; Y += 230.f)
		{
			if (X < DoorDepth + 100.f && FMath::Abs(Y) < DoorLane + 80.f)
			{
				continue;
			}
			PlaceInRoom(B, TEXT("SM_RoundTable"), X, Y, IntoRoom);
			PlaceInRoom(B, TEXT("SM_Chair"), X - 65.f, Y, IntoRoom);
			PlaceInRoom(B, TEXT("SM_Chair"), X + 65.f, Y, ToDoor);
			B.VisitSpots.Add(SeatedSpot(B, X - 65.f, Y, IntoRoom, OnChair, SeatHeight));
			B.VisitSpots.Add(SeatedSpot(B, X + 65.f, Y, ToDoor, OnChair, SeatHeight));
		}
	}
	if (bPool)
	{
		PlaceInRoom(B, TEXT("SM_PoolTable"), B.Depth - 190.f, B.YMin + 190.f, ToPosY);
		B.VisitSpots.Add(StandingSpot(B, B.Depth - 190.f, B.YMin + 330.f, ToNegY, EFTOAnimAction::Work));
	}
	PlaceInRoom(B, TEXT("SM_Dartboard"), B.Depth, B.YMin + 420.f, ToDoor);
	B.VisitSpots.Add(StandingSpot(B, B.Depth - 240.f, B.YMin + 420.f, IntoRoom, EFTOAnimAction::None));
	PlaceInRoom(B, TEXT("SM_Jukebox"), 0.f, B.YMin + 80.f, IntoRoom);

	// Trouble plays out in the lane between the tables and the bar stools.
	B.CrimeSpots.Add(RoomToWorld(B, B.Depth * 0.5f - 55.f, BarY - 158.f, IntoRoom));
}

void AFTOCityGenerator::FurnishOffice(FFTOBuilding& B, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const FLinearColor Brand = Accent(Rng.RandRange(0, 7));

	// Reception facing the door, the receptionist sat behind it; then rows of desks, filing and a water cooler.
	PlaceInRoom(B, TEXT("SM_ReceptionDesk"), 250.f, 0.f, ToDoor, Brand);
	PlaceInRoom(B, TEXT("SM_OfficeChair"), 360.f, 0.f, ToDoor, Brand);
	B.WorkSpots.Add(SeatedSpot(B, 360.f, 0.f, ToDoor, OnOfficeChair, CushionTop));
	B.CrimeSpots.Add(RoomToWorld(B, 170.f, 0.f, IntoRoom));
	PlaceInRoom(B, TEXT("SM_Plant"), 250.f, 200.f, IntoRoom);
	for (float X = 520.f; X <= B.Depth - 110.f; X += 240.f)
	{
		for (float Y = B.YMin + 140.f; Y <= B.YMax - 140.f; Y += 200.f)
		{
			// Desks face the back; whoever works there sits on the door side.
			PlaceInRoom(B, TEXT("SM_Desk"), X, Y, ToDoor);
			PlaceInRoom(B, TEXT("SM_OfficeChair"), X - 70.f, Y, IntoRoom, Brand);
			B.WorkSpots.Add(SeatedSpot(B, X - 70.f, Y, IntoRoom, OnOfficeChair, CushionTop));
		}
	}
	for (float X = 120.f; X <= 400.f; X += 60.f)
	{
		PlaceInRoom(B, TEXT("SM_FilingCabinet"), X, B.YMin, ToPosY);
	}
	// Gossip at the water cooler.
	PlaceInRoom(B, TEXT("SM_WaterCooler"), 60.f, B.YMax - 60.f, IntoRoom);
	B.VisitSpots.Add(StandingSpot(B, 150.f, B.YMax - 70.f, ToNegY, EFTOAnimAction::Talk));
	B.VisitSpots.Add(StandingSpot(B, 150.f, B.YMax - 190.f, ToPosY, EFTOAnimAction::Talk));
	PlaceInRoom(B, TEXT("SM_Plant"), B.Depth - 50.f, B.YMin + 50.f, IntoRoom);
	PlaceInRoom(B, TEXT("SM_Plant"), B.Depth - 50.f, B.YMax - 50.f, IntoRoom);
}

void AFTOCityGenerator::FurnishHome(FFTOBuilding& B, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const FLinearColor Fabric = Accent(Rng.RandRange(0, 7));
	const FLinearColor RugColor = Accent(Rng.RandRange(0, 7));
	const bool bFlip = Rng.FRand() < 0.5f; // living room on the left or the right
	auto Y = [&B, bFlip](float FromNear) { return bFlip ? B.YMax - FromNear : B.YMin + FromNear; };

	// Living room at the front: TV on the front wall, the sofa facing it over a rug and coffee table.
	PlaceInRoom(B, TEXT("SM_TVStand"), 0.f, Y(170.f), IntoRoom);
	PlaceInRoom(B, TEXT("SM_Rug"), 180.f, Y(170.f), IntoRoom, RugColor);
	PlaceInRoom(B, TEXT("SM_CoffeeTable"), 160.f, Y(170.f), IntoRoom);
	PlaceInRoom(B, TEXT("SM_Sofa"), 300.f, Y(160.f), ToDoor, Fabric);
	PlaceInRoom(B, TEXT("SM_FloorLamp"), 330.f, Y(35.f), IntoRoom);
	for (const float Along : { -45.f, 45.f })
	{
		B.VisitSpots.Add(SeatedSpot(B, 300.f, Y(160.f) + Along, ToDoor, OnSofa, CushionTop));
	}
	// A row breaks out past the end of the sofa.
	B.CrimeSpots.Add(RoomToWorld(B, 150.f, Y(360.f), IntoRoom));

	// Kitchen along the back wall, the dining table on the other side with its four chairs.
	PlaceInRoom(B, TEXT("SM_KitchenCounter"), B.Depth, Y(150.f), ToDoor);
	PlaceInRoom(B, TEXT("SM_Fridge"), B.Depth, Y(320.f), ToDoor);
	B.VisitSpots.Add(StandingSpot(B, B.Depth - 100.f, Y(150.f), IntoRoom, EFTOAnimAction::Work));
	const float TableX = B.Depth - 230.f;
	const float TableY = bFlip ? B.YMin + 170.f : B.YMax - 170.f;
	PlaceInRoom(B, TEXT("SM_DiningTable"), TableX, TableY, ToPosY);
	for (const float DX : { -40.f, 40.f })
	{
		B.VisitSpots.Add(SeatedSpot(B, TableX + DX, TableY - 70.f, ToPosY, OnDiningChair, SeatHeight));
		B.VisitSpots.Add(SeatedSpot(B, TableX + DX, TableY + 70.f, ToNegY, OnDiningChair, SeatHeight));
	}

	PlaceInRoom(B, TEXT("SM_Bookshelf"), 200.f, bFlip ? B.YMin : B.YMax, bFlip ? ToPosY : ToNegY);
	PlaceInRoom(B, TEXT("SM_Plant"), 60.f, bFlip ? B.YMin + 60.f : B.YMax - 60.f, IntoRoom);
	PlaceInRoom(B, TEXT("SM_Doormat"), 45.f, 0.f, IntoRoom, Fabric);
}

void AFTOCityGenerator::FurnishWarehouse(FFTOBuilding& B, FRandomStream& Rng)
{
	using namespace FTOCityPalette;

	// Racking down the back two-thirds; the front stays open for the loading door.
	for (float Y = B.YMin + 200.f; Y <= B.YMax - 200.f; Y += 360.f)
	{
		for (float X = 750.f; X + 145.f <= B.Depth - 100.f; X += 300.f)
		{
			PlaceInRoom(B, TEXT("SM_PalletRack"), X, Y, ToPosY);
		}
		B.VisitSpots.Add(StandingSpot(B, 700.f, Y + 180.f, IntoRoom, EFTOAnimAction::Work)); // stocktaking
	}
	// Burglars and worse lurk deep in the first aisle.
	B.CrimeSpots.Add(RoomToWorld(B, B.Depth - 300.f, B.YMin + 380.f, ToDoor));

	// Pallets and crates by the doors, a forklift (and its driver), some barrels.
	for (int32 k = 0; k < 7; ++k)
	{
		const float X = Rng.FRandRange(180.f, 520.f);
		float Y = Rng.FRandRange(B.YMin + 120.f, B.YMax - 120.f);
		if (FMath::Abs(Y) < DoorLane + 60.f)
		{
			Y += (Y < 0.f ? -1.f : 1.f) * 250.f;
		}
		PlaceInRoom(B, Rng.FRand() < 0.5f ? TEXT("SM_Pallet") : TEXT("SM_Crate"), X, Y, Rng.RandRange(0, 3) * 90.f);
	}
	PlaceInRoom(B, TEXT("SM_Forklift"), 560.f, B.YMin + 260.f, ToPosY);
	B.WorkSpots.Add(SeatedSpot(B, 560.f, B.YMin + 230.f, ToPosY, OnStool, 106.f, EFTOAnimAction::Drive));
	for (int32 k = 0; k < 4; ++k)
	{
		PlaceInRoom(B, TEXT("SM_Barrel"), B.Depth - 60.f - (k % 2) * 65.f, B.YMax - 60.f - (k / 2) * 65.f, IntoRoom, Accent(k));
	}
	PlaceInRoom(B, TEXT("SM_Desk"), 150.f, B.YMax - 120.f, ToNegY);
	PlaceInRoom(B, TEXT("SM_OfficeChair"), 150.f, B.YMax - 190.f, ToPosY);
	B.WorkSpots.Add(SeatedSpot(B, 150.f, B.YMax - 190.f, ToPosY, OnOfficeChair, CushionTop));
}
