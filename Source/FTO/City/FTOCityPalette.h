#pragma once

#include "CoreMinimal.h"

/** Colours the city generator paints with (linear; the kit's own details carry their vertex colours). */
namespace FTOCityPalette
{
	inline const FLinearColor Asphalt(0.035f, 0.035f, 0.045f);
	inline const FLinearColor Sidewalk(0.45f, 0.45f, 0.48f);
	inline const FLinearColor RoadLine(0.95f, 0.95f, 0.85f);
	inline const FLinearColor Grass(0.18f, 0.55f, 0.15f);
	inline const FLinearColor Water(0.10f, 0.35f, 0.75f);
	inline const FLinearColor Concrete(0.30f, 0.30f, 0.30f);
	inline const FLinearColor Path(0.62f, 0.58f, 0.52f);
	inline const FLinearColor Roof(0.24f, 0.22f, 0.23f);
	inline const FLinearColor Ceiling(0.92f, 0.91f, 0.88f);
	inline const FLinearColor White(1.f, 1.f, 1.f);
	inline const FLinearColor PoliceBlue(0.10f, 0.22f, 0.62f);
	inline const FLinearColor BankGold(0.85f, 0.65f, 0.15f);
	inline const FLinearColor BankStone(0.86f, 0.82f, 0.72f);
	inline const FLinearColor Warehouse(0.62f, 0.64f, 0.66f);

	// Floors by room type
	inline const FLinearColor FloorWood(0.52f, 0.33f, 0.17f);
	inline const FLinearColor FloorTile(0.80f, 0.80f, 0.78f);
	inline const FLinearColor FloorDiner(0.88f, 0.86f, 0.80f);
	inline const FLinearColor FloorCarpet(0.30f, 0.34f, 0.46f);
	inline const FLinearColor FloorConcrete(0.50f, 0.50f, 0.50f);
	inline const FLinearColor FloorMarble(0.90f, 0.88f, 0.84f);

	/** Cheerful pastel walls. */
	inline FLinearColor Building(int32 Index)
	{
		static const FLinearColor Colors[] =
		{
			FLinearColor(0.95f, 0.55f, 0.55f), FLinearColor(0.55f, 0.80f, 0.95f), FLinearColor(0.95f, 0.85f, 0.45f),
			FLinearColor(0.65f, 0.90f, 0.60f), FLinearColor(0.85f, 0.65f, 0.95f), FLinearColor(0.98f, 0.70f, 0.40f),
			FLinearColor(0.90f, 0.90f, 0.90f), FLinearColor(0.50f, 0.85f, 0.80f), FLinearColor(0.96f, 0.78f, 0.66f),
		};
		return Colors[FMath::Abs(Index) % UE_ARRAY_COUNT(Colors)];
	}

	/** Bolder colours for awnings, signs, upholstery and roofs. */
	inline FLinearColor Accent(int32 Index)
	{
		static const FLinearColor Colors[] =
		{
			FLinearColor(0.80f, 0.10f, 0.10f), FLinearColor(0.10f, 0.35f, 0.80f), FLinearColor(0.10f, 0.55f, 0.25f),
			FLinearColor(0.85f, 0.45f, 0.05f), FLinearColor(0.50f, 0.15f, 0.60f), FLinearColor(0.05f, 0.55f, 0.60f),
			FLinearColor(0.90f, 0.25f, 0.45f), FLinearColor(0.35f, 0.22f, 0.12f),
		};
		return Colors[FMath::Abs(Index) % UE_ARRAY_COUNT(Colors)];
	}
}
