#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;

/**
 * Shared art helpers. Everything in FTO renders with one master material
 * (Content/FTO/Materials/M_FTOBase, built by Tools/Unreal/create_materials.py):
 * vertex colour, tinted by a "Color" parameter where vertex alpha is 1,
 * plus an optional "Emissive" glow.
 */
namespace FTOArt
{
	/** Asset path for ConstructorHelpers (so the material is a hard, cookable reference). */
	inline constexpr const TCHAR* BaseMaterialPath = TEXT("/Game/FTO/Materials/M_FTOBase.M_FTOBase");

	/** Makes a tinted instance of Base (or the slot's current material if Base is null) and assigns it. */
	FTO_API UMaterialInstanceDynamic* ApplyColor(UPrimitiveComponent* Component, UMaterialInterface* Base, const FLinearColor& Color, float Emissive = 0.f, int32 Slot = 0);

	/** Updates an existing instance. */
	FTO_API void SetColor(UMaterialInstanceDynamic* Material, const FLinearColor& Color, float Emissive = 0.f);

	/** A handful of friendly cartoon skin tones. */
	FTO_API FLinearColor SkinTone(int32 Index);
}
