#pragma once

#include "CoreMinimal.h"
#include "Animation/FTOAnimatedActor.h"
#include "FTOVehicleSeats.generated.h"

/**
 * Seats in FTO's vehicles. Each one is a socket on the body mesh (Tools/Blender/build_vehicles.py)
 * placed where a seated character's root (feet origin) goes, facing the car's forward.
 */
UENUM(BlueprintType)
enum class EFTOSeat : uint8
{
	None,
	Driver,
	Passenger,
	RearLeft,
	RearRight
};

namespace FTOSeats
{
	/** Socket a seated character's root attaches to. */
	FTO_API FName SeatSocket(EFTOSeat Seat);

	/** Eye-level socket for the interior camera (front seats only; NAME_None otherwise). */
	FTO_API FName CameraSocket(EFTOSeat Seat);

	/** How someone sits in this seat: hands on the wheel up front, along for the ride elsewhere. */
	FTO_API EFTOAnimAction RidingPose(EFTOSeat Seat);

	/**
	 * Seated eye point relative to a character's capsule centre, for cameras that follow the character
	 * rather than the car (matches the Cam_* sockets: 16 cm ahead of and 144 cm above the root).
	 */
	FTO_API FVector EyeOffset(float CapsuleHalfHeight);
}
