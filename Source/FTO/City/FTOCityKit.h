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

	/** Top of the kit's chairs, benches and booths (build_kit.py SEAT): low, for a cast that's short in the leg. */
	constexpr float SeatHeight = 31.f;
	/** Sitting (A_Officer_Sit): the root is this far below the seat top... */
	constexpr float SitDrop = 33.f;
	/** ...and this far in front of whatever they lean back on. */
	constexpr float SitBack = 27.f;
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
