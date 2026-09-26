#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FTOCityGenerator.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UTextRenderComponent;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EFTODistrict : uint8
{
	Downtown,
	Residential,
	Industrial,
	Park
};

/** One city block in the generated layout. */
USTRUCT()
struct FFTOCityBlock
{
	GENERATED_BODY()

	UPROPERTY() int32 X = 0;
	UPROPERTY() int32 Y = 0;
	UPROPERTY() EFTODistrict District = EFTODistrict::Residential;
	/** World-space centre and half-size of the buildable area (inside the sidewalk). */
	UPROPERTY() FVector Center = FVector::ZeroVector;
	UPROPERTY() float HalfSize = 0.f;
	UPROPERTY() bool bPrecinct = false;
	UPROPERTY() bool bBank = false;
};

/**
 * Builds a toy-box city from engine primitives on a road grid.
 *
 * Deterministic from a replicated seed: the server and every client build the same
 * geometry locally (instanced meshes, nothing per-building is replicated). Only the
 * server spawns gameplay actors: player starts at the precinct and crime spawn points.
 */
UCLASS()
class FTO_API AFTOCityGenerator : public AActor
{
	GENERATED_BODY()

public:
	AFTOCityGenerator();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: pick the seed, lay out the city and spawn gameplay markers. */
	void ServerGenerate(int32 InSeed);

	UFUNCTION(BlueprintPure, Category="City") FVector GetPrecinctLocation() const { return PrecinctLocation; }
	UFUNCTION(BlueprintPure, Category="City") FVector GetCityExtent() const;
	UFUNCTION(BlueprintPure, Category="City") int32 GetSeed() const { return Seed; }

	// ---- Road / sidewalk graph for ambient traffic and pedestrians ----
	/** Intersections are indexed 0..BlocksX by 0..BlocksY. */
	int32 NumIntersectionsX() const { return BlocksX + 1; }
	int32 NumIntersectionsY() const { return BlocksY + 1; }
	FVector GetIntersection(int32 I, int32 J) const;
	/** Corner 0..3 of a block's sidewalk ring, going round the block. */
	FVector GetSidewalkCorner(int32 BlockX, int32 BlockY, int32 Corner) const;
	float GetRoadWidth() const { return RoadWidth; }
	float GetCurbHeight() const;
	const TArray<FFTOCityBlock>& GetBlocks() const { return Blocks; }

	/** Four parking bays in the precinct lot, facing the street. */
	TArray<FTransform> GetPrecinctParkingSpots() const;

	/** Blocks along each axis. 8 x 8 is about 420 m across. */
	UPROPERTY(EditAnywhere, Category="City|Layout") int32 BlocksX = 8;
	UPROPERTY(EditAnywhere, Category="City|Layout") int32 BlocksY = 8;
	/** Size of a block including its sidewalk. */
	UPROPERTY(EditAnywhere, Category="City|Layout") float BlockSize = 4000.f;
	UPROPERTY(EditAnywhere, Category="City|Layout") float RoadWidth = 1200.f;
	UPROPERTY(EditAnywhere, Category="City|Layout") float SidewalkWidth = 300.f;

protected:
	virtual void BeginPlay() override;

	UFUNCTION() void OnRep_Seed();

	/** Deterministic: same seed, same blocks. Runs on every machine. */
	void BuildLayout();
	void BuildGeometry();
	void SpawnGameplayMarkers();

	// Geometry helpers
	void AddBox(int32 ColorIndex, const FVector& Center, const FVector& Size, float Yaw = 0.f);
	void AddCylinder(int32 ColorIndex, const FVector& Center, const FVector& Size);
	void AddSphere(int32 ColorIndex, const FVector& Center, const FVector& Size);
	UInstancedStaticMeshComponent* GetISM(UStaticMesh* Mesh, int32 ColorIndex);

	void BuildDowntownBlock(const FFTOCityBlock& Block, FRandomStream& Rng);
	void BuildResidentialBlock(const FFTOCityBlock& Block, FRandomStream& Rng);
	void BuildIndustrialBlock(const FFTOCityBlock& Block, FRandomStream& Rng);
	void BuildParkBlock(const FFTOCityBlock& Block, FRandomStream& Rng);
	void BuildPrecinct(const FFTOCityBlock& Block);
	void BuildBank(const FFTOCityBlock& Block);
	void AddTree(const FVector& Base, FRandomStream& Rng);
	void AddLabel(const FVector& Location, const FText& Text, const FColor& Color, float Size);

	FVector BlockOrigin(int32 X, int32 Y) const;

	UPROPERTY(ReplicatedUsing=OnRep_Seed) int32 Seed = 0;

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient) TArray<FFTOCityBlock> Blocks;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UInstancedStaticMeshComponent>> ISMs;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> SphereMesh;

	FVector PrecinctLocation = FVector::ZeroVector;
	bool bGeometryBuilt = false;
};
