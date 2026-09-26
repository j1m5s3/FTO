// Street dressing for AFTOCityGenerator: curbs, zebra crossings, traffic lights on downtown junctions,
// street lamps, and the clutter that makes a sidewalk feel lived in (hydrants, bins, benches, bus stops,
// planters, parking meters, trees).
#include "City/FTOCityGenerator.h"
#include "City/FTOCityPalette.h"

namespace
{
	struct FSide
	{
		FVector Out;   // away from the block, toward the road
		FVector Along;
	};
	const FSide Sides[] =
	{
		{ FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f) }, { FVector(-1.f, 0.f, 0.f), FVector(0.f, -1.f, 0.f) },
		{ FVector(0.f, 1.f, 0.f), FVector(-1.f, 0.f, 0.f) }, { FVector(0.f, -1.f, 0.f), FVector(1.f, 0.f, 0.f) },
	};
}

bool AFTOCityGenerator::IsDowntownCorner(int32 I, int32 J) const
{
	for (const int32 DX : { -1, 0 })
	{
		for (const int32 DY : { -1, 0 })
		{
			const int32 BX = I + DX;
			const int32 BY = J + DY;
			if (BX >= 0 && BY >= 0 && BX < BlocksX && BY < BlocksY && Blocks.IsValidIndex(BX * BlocksY + BY) &&
				Blocks[BX * BlocksY + BY].District == EFTODistrict::Downtown)
			{
				return true;
			}
		}
	}
	return false;
}

void AFTOCityGenerator::BuildStreets(FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const float Half = RoadWidth * 0.5f;
	const FIntPoint Steps[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

	for (int32 I = 0; I < NumIntersectionsX(); ++I)
	{
		for (int32 J = 0; J < NumIntersectionsY(); ++J)
		{
			const FVector C = GetIntersection(I, J);
			const bool bLights = IsDowntownCorner(I, J);
			for (const FIntPoint& Step : Steps)
			{
				const bool bRoadThisWay = I + Step.X >= 0 && I + Step.X < NumIntersectionsX() && J + Step.Y >= 0 && J + Step.Y < NumIntersectionsY();
				if (!bRoadThisWay)
				{
					continue;
				}
				const FVector D(Step.X, Step.Y, 0.f);
				const FVector Across(-D.Y, D.X, 0.f);

				// Zebra crossing just outside the junction.
				const FVector Crossing = C + D * (Half + 170.f) + FVector(0.f, 0.f, 1.f);
				for (float S = -Half + 70.f; S <= Half - 70.f; S += 110.f)
				{
					AddBox(RoadLine, Crossing + Across * S, Step.X != 0 ? FVector(300.f, 55.f, 2.f) : FVector(55.f, 300.f, 2.f));
				}

				// Traffic arriving along -D stops here: its signal stands on the near-right corner, the arm
				// out over its lanes and the lamps looking back at it.
				if (bLights)
				{
					const FVector Travel = -D;
					const FVector Right(-Travel.Y, Travel.X, 0.f);
					const FVector Corner = C - Travel * (Half + 60.f) + Right * (Half + 60.f);
					Place(TEXT("SM_TrafficLight"), FTransform((-Travel).Rotation(), FVector(Corner.X, Corner.Y, StreetZ())));
				}
			}
		}
	}

	// Curb stones round every block (the Y-running sides stop short of the corners so they don't overlap).
	for (const FFTOCityBlock& Block : Blocks)
	{
		for (const FSide& Side : Sides)
		{
			const bool bShort = Side.Out.Y != 0.f;
			const float Length = BlockSize - (bShort ? 48.f : 0.f);
			const int32 Count = FMath::RoundToInt(BlockSize / FTOKit::PanelWidth);
			const float Piece = Length / Count;
			for (int32 k = 0; k < Count; ++k)
			{
				const float S = -Length * 0.5f + Piece * (k + 0.5f);
				const FVector At = Block.Center + Side.Out * (BlockSize * 0.5f) + Side.Along * S - FVector(0.f, 0.f, 3.f);
				Place(TEXT("SM_Curb"), FTransform(Side.Out.Rotation(), At, FVector(1.f, Piece / FTOKit::PanelWidth, 1.f)));
			}
		}
	}
}

void AFTOCityGenerator::DressSidewalks(const FFTOCityBlock& Block, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const float Edge = BlockSize * 0.5f;
	const float Z = StreetZ();
	const bool bDowntown = Block.District == EFTODistrict::Downtown || Block.bBank || Block.bPrecinct;

	for (const FSide& Side : Sides)
	{
		const FVector Base = FVector(Block.Center.X, Block.Center.Y, Z) + Side.Out * Edge;
		auto At = [&](float Along, float In) { return Base + Side.Along * Along - Side.Out * In; };
		const FRotator FacingRoad = Side.Out.Rotation();

		// Street lamps a quarter of the way in from each corner, arms out over the road.
		for (const float F : { -0.25f, 0.25f })
		{
			Place(TEXT("SM_LampPost"), FTransform(FacingRoad, At(F * BlockSize, 45.f)));
		}

		// Free spots along the curb: not at the corners (signals), the lamps, or mid-block (incident scenes).
		TArray<float> Slots;
		for (float S = -1500.f; S <= 1500.f; S += 200.f)
		{
			if (FMath::Abs(FMath::Abs(S) - Edge * 0.5f) > 150.f && FMath::Abs(S) > 150.f)
			{
				Slots.Add(S);
			}
		}
		auto TakeSlot = [&]() -> float
		{
			if (Slots.IsEmpty())
			{
				return 0.f;
			}
			const int32 Index = Rng.RandRange(0, Slots.Num() - 1);
			const float S = Slots[Index];
			Slots.RemoveAt(Index);
			return S;
		};

		Place(TEXT("SM_Hydrant"), FTransform(FacingRoad, At(TakeSlot(), 50.f)));
		Place(TEXT("SM_Bin"), FTransform(FacingRoad, At(TakeSlot(), 50.f)));

		if (bDowntown)
		{
			if (Rng.FRand() < 0.35f)
			{
				const float S = Rng.FRand() < 0.5f ? -600.f : 600.f;
				Slots.RemoveAll([S](float Slot) { return FMath::Abs(Slot - S) < 250.f; });
				Place(TEXT("SM_BusStop"), FTransform(FacingRoad, At(S, 40.f)));
			}
			else
			{
				Place(TEXT("SM_StreetBench"), FTransform(FacingRoad, At(TakeSlot(), 60.f)));
			}
			for (int32 m = 0; m < 3; ++m)
			{
				Place(TEXT("SM_ParkingMeter"), FTransform(FacingRoad, At(TakeSlot(), 35.f)));
			}
			Place(Rng.FRand() < 0.5f ? TEXT("SM_NewsBox") : TEXT("SM_Mailbox"), FTransform(FacingRoad, At(TakeSlot(), 50.f)));
			for (int32 p = 0; p < 2; ++p)
			{
				Place(TEXT("SM_Planter"), FTransform(FacingRoad, At(TakeSlot(), 250.f)));
			}
		}
		else if (Block.District == EFTODistrict::Residential)
		{
			for (int32 t = 0; t < 2; ++t)
			{
				AddTree(At(TakeSlot(), 70.f), Rng);
			}
		}
		else if (Block.District == EFTODistrict::Park)
		{
			Place(TEXT("SM_StreetBench"), FTransform(FacingRoad, At(TakeSlot(), 60.f)));
		}
	}
}

void AFTOCityGenerator::AddTree(const FVector& Base, FRandomStream& Rng)
{
	const float Roll = Rng.FRand();
	const TCHAR* Piece = Roll < 0.5f ? TEXT("SM_Tree_Round") : (Roll < 0.8f ? TEXT("SM_Tree_Tall") : TEXT("SM_Tree_Pine"));
	const float Scale = Rng.FRandRange(0.85f, 1.25f);
	Place(Piece, FTransform(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), FVector(Base.X, Base.Y, StreetZ()), FVector(Scale)));
}
