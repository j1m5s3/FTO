#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FTODebris.generated.h"

class UAudioComponent;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Wreckage for looking at, made by every machine for itself (none of it matters to gameplay): chunks, glass shards
 * and whole knocked-off props tumbling under Chaos physics before shrinking away, bullet holes that fade, and hydrant
 * fountains. Everything's pooled and capped, the oldest recycled first, so a shootout can't bury the frame rate.
 */
UCLASS()
class FTO_API UFTODebris : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UFTODebris* Get(const UWorld* World);

	/**
	 * Throws Mesh into the world as a rigid body: at Where, moving at Velocity, spinning at Spin (degrees per second),
	 * for Life seconds before it shrinks away. Painted Color (the mesh's paintable parts, or all of an engine shape),
	 * or wearing Material if one's given. Mass in kg (0: whatever the mesh weighs).
	 */
	void Throw(UStaticMesh* Mesh, const FTransform& Where, const FLinearColor& Color, const FVector& Velocity, const FVector& Spin,
		float Life, float Mass = 0.f, UMaterialInterface* Material = nullptr);

	/** A burst of Count little chunks (cubes about Size cm across, in Color) flying out from At along Push. */
	void Chunks(const FVector& At, const FVector& Push, const FLinearColor& Color, int32 Count, float Size, float Life = 4.f);

	/** A pane's worth of glass shards (the pane's transform and its local bounds), flung along Push from Hit. */
	void Shards(const FTransform& Pane, const FBox& LocalBounds, const FVector& Hit, const FVector& Push, int32 Count);

	/** A pock mark where a round hit (Normal: the surface's), stuck to On so it moves with it (a car). */
	void BulletHole(const FVector& At, const FVector& Normal, USceneComponent* On);

	/** A burst hydrant at At spouting water for Seconds. */
	void Fountain(const FVector& At, float Seconds);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

	/** Pieces in the air right now, bullet holes showing, and fountains running. */
	int32 NumPieces() const;
	int32 NumHoles() const;
	int32 NumFountains() const { return Fountains.Num(); }

	/** Chaos rigid bodies in flight at once (the oldest gives way to a new one). */
	static constexpr int32 MaxPieces = 96;
	static constexpr int32 MaxHoles = 80;
	static constexpr int32 DropsPerFountain = 22;

private:
	AActor* GetHost();
	/** A free piece (or the oldest in use, taken back). */
	int32 TakePiece();
	void ReleasePiece(int32 Index);
	void TickPieces(float DeltaTime);
	void TickFountains(float DeltaTime);

	struct FPieceLife
	{
		float Age = 0.f;
		float Life = 0.f;
		FVector Scale = FVector::OneVector;
		bool bInUse = false;
	};

	struct FFountain
	{
		FVector At = FVector::ZeroVector;
		float Until = 0.f;
		int32 FirstDrop = 0;
		TWeakObjectPtr<UAudioComponent> Sound;
	};

	struct FDrop
	{
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float Delay = 0.f;
	};

	UPROPERTY(Transient) TObjectPtr<AActor> Host;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PieceMaterials;
	TArray<FPieceLife> PieceLives;

	/** Live bullet holes, oldest first (each one fades and removes itself). */
	TArray<TWeakObjectPtr<UDecalComponent>> Holes;

	TArray<FFountain> Fountains;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> DropMeshes;
	TArray<FDrop> Drops;

	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> GlassMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> DecalMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WaterMaterial;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Cube;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Sphere;
};
