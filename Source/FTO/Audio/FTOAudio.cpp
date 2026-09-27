#include "Audio/FTOAudio.h"
#include "City/FTOCityGenerator.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Core/FTOGameState.h"
#include "Engine/HitResult.h"
#include "Physics/FTOVehicleDamage.h"
#include "Sound/SoundBase.h"

namespace FTOAudio
{
	const TArray<FFamilySpec>& Families()
	{
		static const TArray<FFamilySpec> Specs = []
		{
			TArray<FFamilySpec> Out =
			{
				{ TEXT("ShotPistol"), 4 }, { TEXT("ShotRifle"), 4 }, { TEXT("ShotShotgun"), 4 }, { TEXT("Ricochet"), 3 },
				{ TEXT("Crash"), 4 }, { TEXT("Glass"), 3 }, { TEXT("Clang"), 3 },
				{ TEXT("Punch"), 4 }, { TEXT("Kick"), 3 }, { TEXT("BodyFall"), 3 }, { TEXT("Whoosh"), 3 },
				{ TEXT("Land_Concrete"), 2 }, { TEXT("Scuff"), 3 },
				{ TEXT("CarImpactLight"), 3 }, { TEXT("CarImpactHeavy"), 3 }, { TEXT("MetalCreak"), 2 },
				{ TEXT("Rubble"), 4 }, { TEXT("WallBreak"), 3 },
			};
			for (const TCHAR* Surface : { TEXT("Concrete"), TEXT("Wood"), TEXT("Tile"), TEXT("Carpet"), TEXT("Metal"), TEXT("Grass") })
			{
				Out.Add({ FName(*FString::Printf(TEXT("Step_%s"), Surface)), 6 });
				Out.Add({ FName(*FString::Printf(TEXT("StepRun_%s"), Surface)), 4 });
			}
			return Out;
		}();
		return Specs;
	}

	USoundBase* Pick(FName Family)
	{
		const FFTOSoundFamily* Takes = AFTOGameState::Sounds().Families.Find(Family);
		return Takes && Takes->Sounds.Num() > 0 ? Takes->Sounds[FMath::RandRange(0, Takes->Sounds.Num() - 1)].Get() : nullptr;
	}

	USoundBase* Vary(USoundBase* Sound)
	{
		const FName* Family = Sound ? AFTOGameState::Sounds().TakesOf.Find(Sound) : nullptr;
		USoundBase* Take = Family ? Pick(*Family) : nullptr;
		return Take ? Take : Sound;
	}

	USoundBase* Step(EFTOSurface Surface, bool bRunning)
	{
		static const TCHAR* Names[] = { TEXT("Concrete"), TEXT("Wood"), TEXT("Tile"), TEXT("Carpet"), TEXT("Metal"), TEXT("Grass") };
		const TCHAR* Name = Names[FMath::Clamp(int32(Surface), 0, int32(UE_ARRAY_COUNT(Names)) - 1)];
		return Pick(FName(*FString::Printf(bRunning ? TEXT("StepRun_%s") : TEXT("Step_%s"), Name)));
	}

	EFTOSurface SurfaceOf(const FHitResult& Ground)
	{
		const AActor* Actor = Ground.GetActor();
		if (!Actor)
		{
			return EFTOSurface::Concrete;
		}
		// Up on a car.
		if (Actor->FindComponentByClass<UFTOVehicleDamage>())
		{
			return EFTOSurface::Metal;
		}
		const AFTOCityGenerator* City = Cast<AFTOCityGenerator>(Actor);
		if (!City)
		{
			return EFTOSurface::Concrete;
		}
		// Indoors: what the room's floor is (FTOCityBuildings.cpp lays them by the kind of building).
		const FVector Feet = Ground.ImpactPoint + FVector(0.f, 0.f, 10.f);
		for (const FFTOBuilding& Building : City->GetBuildings())
		{
			if (Building.Contains(Feet))
			{
				switch (Building.Type)
				{
				case EFTOBuildingType::Home:
				case EFTOBuildingType::Bar:       return EFTOSurface::Wood;
				case EFTOBuildingType::Office:    return EFTOSurface::Carpet;
				case EFTOBuildingType::Warehouse: return EFTOSurface::Concrete;
				default:                          return EFTOSurface::Tile;
				}
			}
		}
		// Outdoors: a green instance is a lawn or a park.
		const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Ground.GetComponent());
		if (ISM && Ground.Item != INDEX_NONE && ISM->NumCustomDataFloats >= 3)
		{
			const int32 At = Ground.Item * ISM->NumCustomDataFloats;
			if (ISM->PerInstanceSMCustomData.IsValidIndex(At + 2))
			{
				const float R = ISM->PerInstanceSMCustomData[At];
				const float G = ISM->PerInstanceSMCustomData[At + 1];
				const float B = ISM->PerInstanceSMCustomData[At + 2];
				if (G > R * 1.15f && G > B * 1.15f)
				{
					return EFTOSurface::Grass;
				}
			}
		}
		return EFTOSurface::Concrete;
	}
}
