#include "Vehicles/FTOVehicleSeats.h"

namespace FTOSeats
{
	FName SeatSocket(EFTOSeat Seat)
	{
		switch (Seat)
		{
		case EFTOSeat::Driver:    return TEXT("Seat_Driver");
		case EFTOSeat::Passenger: return TEXT("Seat_Passenger");
		case EFTOSeat::RearLeft:  return TEXT("Seat_RearL");
		case EFTOSeat::RearRight: return TEXT("Seat_RearR");
		default:                  return NAME_None;
		}
	}

	FName CameraSocket(EFTOSeat Seat)
	{
		switch (Seat)
		{
		case EFTOSeat::Driver:    return TEXT("Cam_Driver");
		case EFTOSeat::Passenger: return TEXT("Cam_Passenger");
		default:                  return NAME_None;
		}
	}

	EFTOAnimAction RidingPose(EFTOSeat Seat)
	{
		return Seat == EFTOSeat::Driver ? EFTOAnimAction::Drive : EFTOAnimAction::Ride;
	}

	FVector EyeOffset(float CapsuleHalfHeight)
	{
		// (The cast's eyes, sat in a car seat: 1.22 m above the root, a little ahead of it.)
		return FVector(16.f, 0.f, 122.f - CapsuleHalfHeight);
	}
}
