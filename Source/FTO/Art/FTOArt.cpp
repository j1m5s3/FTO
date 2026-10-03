#include "Art/FTOArt.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

void FTOArt::BuildHotDogSuit(AActor* Owner, USceneComponent* Parent, float HalfHeight, UMaterialInterface* Base, TArray<TObjectPtr<UStaticMeshComponent>>& Out)
{
	// A bun either side, the sausage up the middle and over the head, and a squiggle of mustard.
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	struct FPiece { UStaticMesh* Mesh; FVector At; FVector Scale; FLinearColor Color; };
	const FPiece Pieces[] =
	{
		{ Ball, FVector(0.f, 0.f, 105.f), FVector(0.55f, 0.55f, 1.9f), FLinearColor(0.75f, 0.25f, 0.12f) }, // the sausage
		{ Ball, FVector(0.f, 30.f, 80.f), FVector(0.35f, 0.5f, 1.45f), FLinearColor(0.93f, 0.72f, 0.4f) }, // bun
		{ Ball, FVector(0.f, -30.f, 80.f), FVector(0.35f, 0.5f, 1.45f), FLinearColor(0.93f, 0.72f, 0.4f) }, // bun
		{ Cylinder, FVector(24.f, 0.f, 120.f), FVector(0.06f, 0.06f, 1.3f), FLinearColor(1.f, 0.85f, 0.1f) }, // mustard
	};
	for (const FPiece& Piece : Pieces)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
		Part->SetStaticMesh(Piece.Mesh);
		Part->SetupAttachment(Parent);
		Part->SetRelativeLocation(Piece.At - FVector(0.f, 0.f, HalfHeight));
		Part->SetRelativeScale3D(Piece.Scale);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->RegisterComponent();
		ApplyColor(Part, Base, Piece.Color);
		Out.Add(Part);
	}
}
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

	int32 BodySlot(const UPrimitiveComponent* Component)
	{
		const int32 Slot = Component ? Component->GetMaterialIndex(TEXT("Body")) : INDEX_NONE;
		return Slot == INDEX_NONE ? 0 : Slot;
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
