#include "Weapons/FTOWeapons.h"
#include "Core/FTOGameState.h"
#include "Engine/StaticMesh.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	FFTOWeaponSpec MakeSpecs(EFTOWeapon Weapon)
	{
		FFTOWeaponSpec S;
		switch (Weapon)
		{
		case EFTOWeapon::Taser:
			// Probes leave at ~55 m/s and trail wires: short, loopy range, one shot per cartridge.
			S.Name = TEXT("Taser"); S.Mesh = TEXT("SM_Taser"); S.Pose = EFTOAimPose::Pistol;
			S.MuzzleVelocity = 5500.f; S.Drag = 2e-5f; S.Spread = 0.8f; S.HipSpread = 3.f;
			S.Magazine = 1; S.SpareMagazines = 6; S.Interval = 0.6f; S.ReloadSeconds = 1.f;
			S.Range = 900.f; S.KnockSeconds = 4.f; S.Push = 150.f; S.bStun = true;
			break;
		case EFTOWeapon::Pistol:
			// 9 mm: ~360 m/s, loses a little over a city block.
			S.Name = TEXT("Pistol"); S.Mesh = TEXT("SM_Pistol"); S.Pose = EFTOAimPose::Pistol;
			S.MuzzleVelocity = 36000.f; S.Drag = 1.2e-5f; S.Spread = 0.6f; S.HipSpread = 4.f;
			S.Magazine = 12; S.SpareMagazines = 3; S.Interval = 0.22f; S.ReloadSeconds = 1.2f;
			S.Range = 12000.f; S.KnockSeconds = 6.f; S.Push = 500.f;
			break;
		case EFTOWeapon::Shotgun:
			// Buckshot: eight pellets at ~400 m/s that shed speed fast.
			S.Name = TEXT("Shotgun"); S.Mesh = TEXT("SM_Shotgun"); S.Pose = EFTOAimPose::Rifle; S.bLongGun = true;
			S.MuzzleVelocity = 40000.f; S.Drag = 4e-5f; S.Pellets = 8; S.Spread = 3.f; S.HipSpread = 5.f;
			S.Magazine = 6; S.SpareMagazines = 3; S.Interval = 0.8f; S.ReloadSeconds = 2.f;
			S.Range = 5000.f; S.KnockSeconds = 7.f; S.Push = 900.f;
			break;
		case EFTOWeapon::Rifle:
			// 5.56: ~940 m/s and flat.
			S.Name = TEXT("Rifle"); S.Mesh = TEXT("SM_Rifle"); S.Pose = EFTOAimPose::Rifle; S.bLongGun = true;
			S.MuzzleVelocity = 94000.f; S.Drag = 3e-6f; S.Spread = 0.25f; S.HipSpread = 5.f;
			S.Magazine = 20; S.SpareMagazines = 3; S.Interval = 0.14f; S.ReloadSeconds = 1.6f;
			S.Range = 30000.f; S.KnockSeconds = 8.f; S.Push = 700.f;
			break;
		default:
			break;
		}
		return S;
	}
}

const FFTOWeaponSpec& FTOWeapons::Spec(EFTOWeapon Weapon)
{
	static const FFTOWeaponSpec Specs[] =
	{
		MakeSpecs(EFTOWeapon::None), MakeSpecs(EFTOWeapon::Taser), MakeSpecs(EFTOWeapon::Pistol),
		MakeSpecs(EFTOWeapon::Shotgun), MakeSpecs(EFTOWeapon::Rifle),
	};
	const int32 Index = static_cast<int32>(Weapon);
	return Specs[FMath::Clamp(Index, 0, int32(UE_ARRAY_COUNT(Specs)) - 1)];
}

UStaticMesh* FTOWeapons::Mesh(EFTOWeapon Weapon)
{
	static TMap<EFTOWeapon, TWeakObjectPtr<UStaticMesh>> Cache;
	if (Weapon == EFTOWeapon::None)
	{
		return nullptr;
	}
	TWeakObjectPtr<UStaticMesh>& Cached = Cache.FindOrAdd(Weapon);
	if (!Cached.IsValid())
	{
		// /Game/FTO is always cooked, so loading by name works in packaged builds too.
		const TCHAR* Name = Spec(Weapon).Mesh;
		Cached = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/FTO/Weapons/%s.%s"), Name, Name));
	}
	return Cached.Get();
}

USoundBase* FTOWeapons::ShotSound(EFTOWeapon Weapon)
{
	const FFTOSoundSet& Sounds = AFTOGameState::Sounds();
	switch (Weapon)
	{
	case EFTOWeapon::Taser:   return Sounds.Taser;
	case EFTOWeapon::Pistol:  return Sounds.ShotPistol;
	case EFTOWeapon::Shotgun: return Sounds.ShotShotgun;
	case EFTOWeapon::Rifle:   return Sounds.ShotRifle;
	default:                  return nullptr;
	}
}

FText FTOWeapons::DisplayName(EFTOWeapon Weapon)
{
	return Weapon == EFTOWeapon::None ? INVTEXT("-") : FText::FromString(Spec(Weapon).Name);
}

FVector FTOWeapons::RoundDirection(const FVector& Aim, float Spread, int32 Seed, int32 Index)
{
	// Mixed in unsigned arithmetic (wraps the same on every machine; signed overflow wouldn't be defined).
	FRandomStream Rng(static_cast<int32>(static_cast<uint32>(Seed) * 7919u + static_cast<uint32>(Index) * 104729u));
	return Spread > 0.f ? Rng.VRandCone(Aim.GetSafeNormal(), FMath::DegreesToRadians(Spread)) : Aim.GetSafeNormal();
}

void FTOWeapons::HoldInHand(USceneComponent* Gun, const USkeletalMeshComponent* Body, const FRotator& Aim)
{
	if (Gun && Body)
	{
		const FVector Wrist = Body->GetSocketLocation(TEXT("hand_r"));
		Gun->SetWorldLocationAndRotation(Wrist + Aim.RotateVector(FVector(4.f, 0.f, -3.f)), Aim);
	}
}
