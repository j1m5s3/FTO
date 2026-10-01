#pragma once

#include "CoreMinimal.h"
#include "Animation/FTOAnimatedActor.h"
#include "FTOWeapons.generated.h"

class UStaticMesh;
class USoundBase;
class USceneComponent;
class USkeletalMeshComponent;

/** What an officer (or a perp) can have in hand. */
UENUM(BlueprintType)
enum class EFTOWeapon : uint8
{
	None,
	Taser,		// standard issue: a stun probe on a wire, short range, never a knockout
	Pistol,
	Shotgun,
	Rifle
};

/**
 * How a weapon shoots. Muzzle velocities and drag are real-ish (cm and seconds); UFTOBallistics flies every round
 * with them, so a shotgun's pellets spread and slow, a rifle round barely drops, and a taser probe arcs.
 */
struct FFTOWeaponSpec
{
	const TCHAR* Name = TEXT("");
	/** Static mesh under /Game/FTO/Weapons (Tools/Blender/build_weapons.py), barrel along +X, grip at the origin. */
	const TCHAR* Mesh = TEXT("");
	EFTOAimPose Pose = EFTOAimPose::Pistol;
	/** Long guns ride slung on the back; sidearms sit on the hip. */
	bool bLongGun = false;
	float MuzzleVelocity = 36000.f;
	/** Quadratic drag k (1/cm): deceleration = k * speed squared. */
	float Drag = 1.2e-5f;
	int32 Pellets = 1;
	/** Half-angle (degrees) each round wanders from the aim, aimed and from the hip. */
	float Spread = 0.6f;
	float HipSpread = 4.f;
	int32 Magazine = 12;
	/** Spare magazines handed out with it (and at each restock). */
	int32 SpareMagazines = 3;
	float Interval = 0.25f;
	float ReloadSeconds = 1.2f;
	/** Rounds are spent after flying this far (cm). */
	float Range = 12000.f;
	/** Seconds a hit keeps someone down. */
	float KnockSeconds = 6.f;
	/** How hard a hit shoves the body along (cm/s). */
	float Push = 600.f;
	/** A zap rather than a knockout (lighter on citizens, still drops a perp). */
	bool bStun = false;
	/** What each round does to a car (out of 100). */
	float CarDamage = 0.f;
};

namespace FTOWeapons
{
	constexpr int32 MaxSlots = 3;

	FTO_API const FFTOWeaponSpec& Spec(EFTOWeapon Weapon);
	/** The weapon's mesh, loaded on first use. */
	FTO_API UStaticMesh* Mesh(EFTOWeapon Weapon);
	FTO_API USoundBase* ShotSound(EFTOWeapon Weapon);
	FTO_API FText DisplayName(EFTOWeapon Weapon);

	/** Where round Index of a shot goes: Aim, wandered by up to Spread degrees (the same on every machine for a Seed). */
	FTO_API FVector RoundDirection(const FVector& Aim, float Spread, int32 Seed, int32 Index);

	/** Put Gun in Body's right hand (the grip just past the wrist) with the barrel along Aim. */
	/**
	 * Put Gun in Body's right hand, held the way Pose (pistol or rifle) holds it: it follows the hand (whose arm the aim
	 * pose raises and the lean points), so the hands are always on the gun.
	 */
	FTO_API void HoldInHand(USceneComponent* Gun, const USkeletalMeshComponent* Body, EFTOAimPose Pose);
}
