#include "Art/FTOArt.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace FTOArt
{
	UMaterialInstanceDynamic* ApplyColor(UPrimitiveComponent* Component, UMaterialInterface* Base, const FLinearColor& Color, float Emissive, int32 Slot)
	{
		if (!Component)
		{
			return nullptr;
		}

		UMaterialInterface* Parent = Base ? Base : Component->GetMaterial(Slot);
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, Component);
		if (MID)
		{
			SetColor(MID, Color, Emissive);
			Component->SetMaterial(Slot, MID);
		}
		return MID;
	}

	void SetColor(UMaterialInstanceDynamic* Material, const FLinearColor& Color, float Emissive)
	{
		if (Material)
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetScalarParameterValue(TEXT("Emissive"), Emissive);
		}
	}

	FLinearColor SkinTone(int32 Index)
	{
		static const FLinearColor Tones[] =
		{
			FLinearColor(0.93f, 0.72f, 0.58f),
			FLinearColor(0.80f, 0.56f, 0.40f),
			FLinearColor(0.58f, 0.38f, 0.25f),
			FLinearColor(0.36f, 0.23f, 0.15f),
			FLinearColor(0.98f, 0.82f, 0.68f),
		};
		return Tones[FMath::Abs(Index) % UE_ARRAY_COUNT(Tones)];
	}
}
