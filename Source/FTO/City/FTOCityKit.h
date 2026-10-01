#pragma once

#include "CoreMinimal.h"
#include "Animation/FTOAnimatedActor.h"
#include "FTOCityKit.generated.h"

/**
 * The building kit's grid (Tools/Blender/build_kit.py builds the pieces to these numbers, in cm):
 * wall panels are PanelWidth wide, GroundHeight tall on the ground floor and UpperHeight above,
 * with their outer face on the panel's pivot and the wall running WallThickness back into the room.
 */
namespace FTOKit
{
	constexpr float PanelWidth = 200.f;
	constexpr float GroundHeight = 400.f;
	constexpr float UpperHeight = 320.f;
	constexpr float WallThickness = 30.f;

	/** Top of the kit's chairs, benches and booths (build_kit.py SEAT): an ordinary chair. */
	constexpr float SeatHeight = 45.f;
	/** Sitting (A_FTO_Sit, made for a 45 cm chair, hip joints 53 cm up): the root is this far below the seat top... */
	constexpr float SitDrop = 45.f;
	/** ...and this far in front of whatever they lean back on. */
	constexpr float SitBack = 18.f;
}

/** What goes on inside a building's ground floor. */
UENUM(BlueprintType)
enum class EFTOBuildingType : uint8
{
	Shop,
	Diner,
	Bar,
	Office,
	Home,
	Warehouse,
	Bank,
	Precinct
};

/** Somewhere a person spends their time indoors, and what they do there. */
USTRUCT()
struct FFTOSpot
{
	GENERATED_BODY()

	/** Their feet (root), facing the way they face. Seats put the root below the seat top by FTOKit::SitDrop. */
	UPROPERTY() FTransform Transform;
	UPROPERTY() EFTOAnimAction Action = EFTOAnimAction::None;
	UPROPERTY() bool bSeated = false;
};

/**
 * One enterable ground floor. The room frame has its origin on the inside face of the front wall,
 * at the door, with X pointing into the room; the room spans X 0..Depth and Y YMin..YMax.
 */
USTRUCT()
struct FFTOBuilding
{
	GENERATED_BODY()

	UPROPERTY() EFTOBuildingType Type = EFTOBuildingType::Shop;
	/** Sign over the door ("DINER", "BAR"...), or the family name on a house. */
	UPROPERTY() FString Name;
	UPROPERTY() FTransform Room;
	UPROPERTY() float Depth = 0.f;
	UPROPERTY() float YMin = 0.f;
	UPROPERTY() float YMax = 0.f;
	/** On the pavement just outside the door. */
	UPROPERTY() FVector DoorOutside = FVector::ZeroVector;
	/** Staff at their posts (behind counters, at desks), facing their customers. The first is the one who's always in. */
	UPROPERTY() TArray<FFTOSpot> WorkSpots;
	/** Customers or residents (aisles, stools, booths, sofas). */
	UPROPERTY() TArray<FFTOSpot> VisitSpots;
	/** Where a crime inside plays out: the perp stands here facing their victim (the bank's second is its vault). */
	UPROPERTY() TArray<FTransform> CrimeSpots;
	/** Precinct only: the armory's racks... */
	UPROPERTY() TArray<FTransform> ArmorySpots;
	/** ...the holding cells' bench places (a sitter's root, facing out through the bars)... */
	UPROPERTY() TArray<FTransform> CellSpots;
	/** ...and, for each of those, the cell door to go in by (on the floor in the doorway, facing into the cell). */
	UPROPERTY() TArray<FTransform> CellDoors;

	/** Middle of the room, on the floor. */
	FVector GetCenter() const { return Room.TransformPosition(FVector(Depth * 0.5f, (YMin + YMax) * 0.5f, 0.f)); }
	/** Is this world point inside the room (with a little slack for the walls)? */
	bool Contains(const FVector& World, float Slack = 0.f) const
	{
		const FVector Local = Room.InverseTransformPosition(World);
		return Local.X > -Slack && Local.X < Depth + Slack && Local.Y > YMin - Slack && Local.Y < YMax + Slack && Local.Z > -100.f && Local.Z < FTOKit::GroundHeight;
	}
};

/** What a piece of a building is to the building (AFTODestruction knocks them down). */
enum class EFTOPieceRole : uint8
{
	Wall,		// a facade panel: it holds up whatever's above it
	Glass,		// a pane in one
	Trim,		// stuck on the outside: corners, cornices, awnings, signs, parapets, the roof, porches, stairs
	Floor,		// a slab, a ceiling, a floor covering
	Inside,		// furniture, lights, partitions
	Foundation	// the ground floor's floor: what's left when it's all come down
};

/** One instance of the city that belongs to a building. */
struct FFTOStructurePiece
{
	/** The city's instanced component (AFTOCityGenerator::FindInstanced) and the instance in it. */
	FName Component;
	int32 Instance = INDEX_NONE;
	/** Its pivot, in the world. */
	FVector Location = FVector::ZeroVector;
	EFTOPieceRole Role = EFTOPieceRole::Trim;
	/** Which face (AFTOCityGenerator's EFace: +X, -X, +Y, -Y) and which panel along it, for pieces in the facade. */
	int8 Face = -1;
	int8 Column = -1;
	/** 0: the ground floor; 1.. the storeys above (the parapet is one past the top). */
	int8 Level = 0;
};

/**
 * A building as a structure, for knocking down: every piece of it, sorted into the cells of its facade (a panel on
 * a face on a storey), which hold each other up. Built identically on every machine with the city.
 */
struct FFTOStructure
{
	/** The footprint (centre at street level) and how tall. */
	FVector Center = FVector::ZeroVector;
	float HalfX = 0.f;
	float HalfY = 0.f;
	/** Storeys above the ground floor. */
	int32 Floors = 0;
	/** Panels along each face (by EFace). */
	int32 Columns[4] = { 0, 0, 0, 0 };
	/** Its ground-floor room (AFTOCityGenerator::GetBuildings), if it has one. */
	int32 Building = INDEX_NONE;
	FLinearColor Paint = FLinearColor::White;
	TArray<FFTOStructurePiece> Pieces;
	/** Lettering stuck on it (the bank's name), which goes with it. */
	TArray<TWeakObjectPtr<class UTextRenderComponent>> Labels;

	/** The faces in order round the building (each starts where the last one ends). */
	static constexpr int32 RingFaces[4] = { 0, 2, 1, 3 };
	/** Panels all the way round. */
	int32 RingLength() const { return Columns[0] + Columns[1] + Columns[2] + Columns[3]; }
	/** Where panel Column of Face is, going round. */
	int32 RingOf(int32 Face, int32 Column) const
	{
		int32 At = 0;
		for (const int32 F : RingFaces)
		{
			if (F == Face)
			{
				return At + Column;
			}
			At += Columns[F];
		}
		return INDEX_NONE;
	}
	/** Which face a place round the ring is on. */
	int32 FaceAt(int32 Ring) const
	{
		for (const int32 F : RingFaces)
		{
			if (Ring < Columns[F])
			{
				return F;
			}
			Ring -= Columns[F];
		}
		return INDEX_NONE;
	}
	/** Is this world point inside the footprint (with Slack round it)? */
	bool Contains2D(const FVector& Where, float Slack = 0.f) const
	{
		return FMath::Abs(Where.X - Center.X) < HalfX + Slack && FMath::Abs(Where.Y - Center.Y) < HalfY + Slack;
	}
};
