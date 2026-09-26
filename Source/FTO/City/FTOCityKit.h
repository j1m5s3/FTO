#pragma once

#include "CoreMinimal.h"
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
	/** Where staff stand to serve (behind counters, at desks), facing their customers. */
	UPROPERTY() TArray<FTransform> WorkSpots;
	/** Where customers or residents hang about (aisles, stools, sofas). */
	UPROPERTY() TArray<FTransform> VisitSpots;
	/** Precinct only: the armory's racks and the holding cells. */
	UPROPERTY() TArray<FTransform> ArmorySpots;
	UPROPERTY() TArray<FTransform> CellSpots;
};
