// Furnishing for AFTOCityGenerator's ground floors. Everything is laid out in room space: X runs from the
// door (0) to the back wall (Depth), Y from YMin to YMax with the door at 0. A strip in front of the door
// stays clear so people can get in. Staff and visitor spots are recorded for the people who'll use them.
#include "City/FTOCityGenerator.h"
#include "City/FTOCityPalette.h"

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

	// Counter just inside on the right, the clerk behind it.
	PlaceInRoom(B, TEXT("SM_ShopCounter"), 230.f, B.YMax - 150.f, ToNegY, Brand);
	B.WorkSpots.Add(RoomToWorld(B, 230.f, B.YMax - 55.f, ToNegY));
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

	// Aisles of gondolas running back from the front, clear of the counter.
	for (float Y = B.YMin + 250.f; Y <= B.YMax - 360.f; Y += 240.f)
	{
		for (float X = 450.f; X + 100.f <= B.Depth - 170.f; X += 210.f)
		{
			PlaceInRoom(B, TEXT("SM_Shelf"), X, Y, ToPosY);
		}
		B.VisitSpots.Add(RoomToWorld(B, 550.f, Y + 120.f, ToNegY));
	}
	PlaceInRoom(B, TEXT("SM_Plant"), 60.f, B.YMin + 60.f, IntoRoom);
	B.VisitSpots.Add(RoomToWorld(B, 330.f, B.YMax - 250.f, ToPosY)); // at the till
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
		B.WorkSpots.Add(RoomToWorld(B, B.Depth - 45.f, Y, ToDoor));
	}
	for (float Y = -Counters * 150.f + 50.f; Y <= Counters * 150.f - 50.f; Y += 75.f)
	{
		PlaceInRoom(B, TEXT("SM_Stool"), CounterX - 85.f, Y, IntoRoom, Brand);
		B.VisitSpots.Add(RoomToWorld(B, CounterX - 85.f, Y, IntoRoom));
	}

	// Booths down both side walls.
	for (float X = 140.f; X + 105.f <= CounterX - 170.f; X += 220.f)
	{
		PlaceInRoom(B, TEXT("SM_Booth"), X + 105.f, B.YMin, ToPosY, Brand);
		PlaceInRoom(B, TEXT("SM_Booth"), X + 105.f, B.YMax, ToNegY, Brand);
		B.VisitSpots.Add(RoomToWorld(B, X + 105.f - 78.f, B.YMin + 75.f, IntoRoom));
		B.VisitSpots.Add(RoomToWorld(B, X + 105.f + 78.f, B.YMax - 75.f, ToDoor));
	}
	PlaceInRoom(B, TEXT("SM_Jukebox"), B.Depth - 20.f, B.YMax - 70.f, ToDoor);
	PlaceInRoom(B, TEXT("SM_Plant"), 60.f, B.YMin + 280.f, IntoRoom);
	PlaceInRoom(B, TEXT("SM_Doormat"), 45.f, 0.f, IntoRoom, Brand);
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
		B.WorkSpots.Add(RoomToWorld(B, X, B.YMax - 95.f, ToNegY));
		for (float S = -110.f; S <= 110.f; S += 75.f)
		{
			PlaceInRoom(B, TEXT("SM_Stool"), X + S, BarY - 90.f, IntoRoom, Brand);
			B.VisitSpots.Add(RoomToWorld(B, X + S, BarY - 90.f, ToPosY));
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
			B.VisitSpots.Add(RoomToWorld(B, X - 65.f, Y, IntoRoom));
		}
	}
	if (bPool)
	{
		PlaceInRoom(B, TEXT("SM_PoolTable"), B.Depth - 190.f, B.YMin + 190.f, ToPosY);
		B.VisitSpots.Add(RoomToWorld(B, B.Depth - 190.f, B.YMin + 330.f, ToNegY));
	}
	PlaceInRoom(B, TEXT("SM_Dartboard"), B.Depth, B.YMin + 420.f, ToDoor);
	PlaceInRoom(B, TEXT("SM_Jukebox"), 0.f, B.YMin + 80.f, IntoRoom);
}

void AFTOCityGenerator::FurnishOffice(FFTOBuilding& B, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const FLinearColor Brand = Accent(Rng.RandRange(0, 7));

	// Reception facing the door, then rows of desks, filing and a water cooler.
	PlaceInRoom(B, TEXT("SM_ReceptionDesk"), 250.f, 0.f, ToDoor, Brand);
	B.WorkSpots.Add(RoomToWorld(B, 330.f, 0.f, ToDoor));
	PlaceInRoom(B, TEXT("SM_Plant"), 250.f, 200.f, IntoRoom);
	for (float X = 520.f; X <= B.Depth - 110.f; X += 240.f)
	{
		for (float Y = B.YMin + 140.f; Y <= B.YMax - 140.f; Y += 200.f)
		{
			// Desks face the back; whoever works there sits on the door side.
			PlaceInRoom(B, TEXT("SM_Desk"), X, Y, ToDoor);
			PlaceInRoom(B, TEXT("SM_OfficeChair"), X - 70.f, Y, IntoRoom, Brand);
			B.WorkSpots.Add(RoomToWorld(B, X - 70.f, Y, IntoRoom));
		}
	}
	for (float X = 120.f; X <= 400.f; X += 60.f)
	{
		PlaceInRoom(B, TEXT("SM_FilingCabinet"), X, B.YMin, ToPosY);
	}
	PlaceInRoom(B, TEXT("SM_WaterCooler"), 60.f, B.YMax - 60.f, IntoRoom);
	B.VisitSpots.Add(RoomToWorld(B, 130.f, B.YMax - 60.f, ToNegY)); // gossip at the cooler
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
	B.VisitSpots.Add(RoomToWorld(B, 300.f, Y(160.f), ToDoor));

	// Kitchen along the back wall, the dining table on the other side.
	PlaceInRoom(B, TEXT("SM_KitchenCounter"), B.Depth, Y(150.f), ToDoor);
	PlaceInRoom(B, TEXT("SM_Fridge"), B.Depth, Y(320.f), ToDoor);
	B.VisitSpots.Add(RoomToWorld(B, B.Depth - 100.f, Y(150.f), IntoRoom));
	PlaceInRoom(B, TEXT("SM_DiningTable"), B.Depth - 230.f, bFlip ? B.YMin + 170.f : B.YMax - 170.f, ToPosY);
	B.VisitSpots.Add(RoomToWorld(B, B.Depth - 230.f, bFlip ? B.YMin + 170.f : B.YMax - 170.f, IntoRoom));

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
		B.VisitSpots.Add(RoomToWorld(B, 700.f, Y + 180.f, IntoRoom));
	}

	// Pallets and crates by the doors, a forklift, some barrels.
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
	B.WorkSpots.Add(RoomToWorld(B, 480.f, B.YMin + 260.f, ToPosY));
	for (int32 k = 0; k < 4; ++k)
	{
		PlaceInRoom(B, TEXT("SM_Barrel"), B.Depth - 60.f - (k % 2) * 65.f, B.YMax - 60.f - (k / 2) * 65.f, IntoRoom, Accent(k));
	}
	PlaceInRoom(B, TEXT("SM_Desk"), 150.f, B.YMax - 120.f, ToNegY);
	PlaceInRoom(B, TEXT("SM_OfficeChair"), 150.f, B.YMax - 190.f, ToPosY);
	B.WorkSpots.Add(RoomToWorld(B, 150.f, B.YMax - 190.f, ToPosY));
}
