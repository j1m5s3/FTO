#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "City/FTOCityKit.h"
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
 * Builds the city on a road grid from the in-house building kit (Tools/Blender/build_kit.py): towers
 * with shopfronts downtown, houses with porches and picket fences, warehouses, the bank and the
 * precinct, every ground floor enterable and furnished for what goes on inside, plus street dressing.
 *
 * Deterministic from a replicated seed: the server and every client build the same geometry locally
 * (nothing per-building is replicated). Everything is instanced (one component per kit piece, painted
 * per instance, Nanite where opaque). Only the server spawns gameplay actors: player starts at the
 * precinct and crime spawn points.
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

	/** Every enterable ground floor, with its room, door and the spots people use inside. */
	const TArray<FFTOBuilding>& GetBuildings() const { return Buildings; }
	const FFTOBuilding* GetBuilding(int32 Index) const { return Buildings.IsValidIndex(Index) ? &Buildings[Index] : nullptr; }
	const FFTOBuilding* FindBuilding(EFTOBuildingType Type) const;
	int32 FindBuildingIndex(EFTOBuildingType Type) const;
	/** The precinct's holding cells: where cuffed suspects are walked to be booked. */
	FVector GetHoldingCellsLocation() const;

	/** Built on this machine yet (clients build once the seed arrives)? */
	bool IsGeometryBuilt() const { return bGeometryBuilt; }
	/**
	 * One of the city's instanced components by name (a kit piece's mesh name, "_In" for rooms). Every machine builds
	 * the city identically, so a name and an instance index mean the same thing everywhere.
	 */
	UInstancedStaticMeshComponent* FindInstanced(FName Name) const;

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

	// ---- Instancing: everything is queued, then instanced in one go ----
	struct FBatch
	{
		TArray<FTransform> Transforms;
		TArray<float> Colors; // RGB per instance, read by MI_FTOCity
	};
	/** A kit piece by name (SM_Wall_G_Plain...), loaded on first use. */
	UStaticMesh* Kit(const TCHAR* Piece);
	void Place(UStaticMesh* Mesh, const FTransform& Transform, const FLinearColor& Tint = FLinearColor::White, bool bInterior = false);
	void Place(const TCHAR* Piece, const FTransform& Transform, const FLinearColor& Tint = FLinearColor::White, bool bInterior = false);
	void FlushInstances();

	// Engine basic shapes (100 cm, centred), for roads, slabs and the like.
	void AddBox(const FLinearColor& Color, const FVector& Center, const FVector& Size, float Yaw = 0.f, bool bInterior = false);
	void AddCylinder(const FLinearColor& Color, const FVector& Center, const FVector& Size);
	void AddSphere(const FLinearColor& Color, const FVector& Center, const FVector& Size);
	void AddLabel(const FVector& Location, float Yaw, const FText& Text, const FColor& Color, float Size);

	// ---- Buildings (FTOCityBuildings.cpp) ----
	enum class EFace : uint8 { PosX, NegX, PosY, NegY };
	/** An axis-aligned building: centre of the footprint at street level, size in kit panels. */
	struct FFootprint
	{
		FVector Center = FVector::ZeroVector;
		int32 PanelsX = 1;
		int32 PanelsY = 1;
		float HalfX() const { return PanelsX * FTOKit::PanelWidth * 0.5f; }
		float HalfY() const { return PanelsY * FTOKit::PanelWidth * 0.5f; }
	};
	/** What each ground-floor panel of a face should be. */
	enum class EPanel : uint8 { Plain, Window, Door, Shop, ShopDoor, Roller, Skip };

	static FVector FaceNormal(EFace Face);
	int32 FacePanels(const FFootprint& F, EFace Face) const;
	/** Pivot of panel Index along a face (outer face, at height Z above the footprint), facing out. */
	FTransform PanelTransform(const FFootprint& F, EFace Face, float Index, float Z) const;

	void BuildGroundFace(const FFootprint& F, EFace Face, const TArray<EPanel>& Panels, const FLinearColor& Paint);
	/** The facade above the ground floor. SpecialFace/SpecialIndex (if set) gets SpecialPiece on every floor instead
	 *  (a plain panel behind a lift's stops), or nothing at all if SpecialPiece is null (a doorway to outside stairs). */
	void BuildUpperFloors(const FFootprint& F, int32 Floors, const FLinearColor& Paint, FRandomStream& Rng, bool bWide,
		int32 SpecialFace = -1, int32 SpecialIndex = -1, const TCHAR* SpecialPiece = nullptr);

	// ---- Upper floors (FTOCityUpperFloors.cpp) ----
	/** Standing height of storey 1.. (above the footprint's street level). */
	static float StoreyFloorZ(int32 Storey);
	/** Which quarter of a footprint a point is in (0: +X+Y, 1: -X+Y, 2: -X-Y, 3: +X-Y). */
	static int32 QuarterOf(const FFootprint& F, const FVector& Where);
	/** Floors, lights and furniture for storeys 1..Floors (offices low down, homes higher up or everywhere for
	 *  bHomes), under a ceiling at CeilingTopZ on the top floor; SkipQuarter kept clear (for the lift). */
	void BuildUpperStoreys(const FFootprint& F, int32 Floors, bool bHomes, float CeilingTopZ, int32 SkipQuarter, FRandomStream& Rng);
	/** A lift with its street doors on panel Index of Face and a stop on every floor above (spawned on the server). */
	void PlanLift(const FFootprint& F, EFace Face, int32 Index, int32 Floors);
	/** Stairs up the outside of Face to a doorway at panel Index on the first floor. */
	void BuildOutsideStairs(const FFootprint& F, EFace Face, int32 Index, const FLinearColor& Tint);
	struct FLiftPlan
	{
		/** Where each stop's doors are (on the wall, facing whoever's waiting), street level first. */
		TArray<FTransform> Stops;
	};
	TArray<FLiftPlan> LiftPlans;
	void BuildRoof(const FFootprint& F, float RoofZ, const FLinearColor& Paint, FRandomStream& Rng, bool bRooftopClutter);
	void BuildCorners(const FFootprint& F, int32 Floors, const FLinearColor& Paint);
	/** Floor, ceiling and lights for a ground floor, and the room record interiors are furnished from. */
	FFTOBuilding& AddRoom(const FFootprint& F, EFace DoorFace, float DoorIndex, EFTOBuildingType Type, const FString& Name, float CeilingHeight);
	/** Room-space (X into the room from the door) to world. */
	FTransform RoomToWorld(const FFTOBuilding& B, float X, float Y, float Yaw, float Z = 0.f) const;
	void PlaceInRoom(const FFTOBuilding& B, const TCHAR* Piece, float X, float Y, float Yaw, const FLinearColor& Tint = FLinearColor::White, float Z = 0.f);
	/** Someone standing at (X, Y) in room space, facing Yaw, doing Action. */
	FFTOSpot StandingSpot(const FFTOBuilding& B, float X, float Y, float Yaw, EFTOAnimAction Action) const;
	/**
	 * Someone sat on the seat piece at (SeatX, SeatY) facing Yaw: their root goes Forward cm ahead of the piece's
	 * origin (FTOKit::SitBack in front of its backrest) and FTOKit::SitDrop below SeatTop.
	 */
	FFTOSpot SeatedSpot(const FFTOBuilding& B, float SeatX, float SeatY, float Yaw, float Forward, float SeatTop,
		EFTOAnimAction Action = EFTOAnimAction::Sit) const;
	/**
	 * A partition in room space from From to To, along Y at X = Line (bAlongY) or along X at Y = Line,
	 * tiled with interior wall panels (doorways where Doors asks, the last panel squeezed to fit).
	 */
	void InteriorWall(const FFTOBuilding& B, bool bAlongY, float Line, float From, float To, TArrayView<const float> Doors, const FLinearColor& Paint);

	void BuildTower(const FFTOCityBlock& Block, int32 QuadX, int32 QuadY, FRandomStream& Rng);
	void BuildHouse(const FVector& FrontCenter, EFace Facing, FRandomStream& Rng);
	void BuildWarehouse(const FFootprint& F, EFace DoorFace, FRandomStream& Rng);

	void BuildDowntownBlock(const FFTOCityBlock& Block, FRandomStream& Rng);
	void BuildResidentialBlock(const FFTOCityBlock& Block, FRandomStream& Rng);
	void BuildIndustrialBlock(const FFTOCityBlock& Block, FRandomStream& Rng);
	void BuildParkBlock(const FFTOCityBlock& Block, FRandomStream& Rng);
	void BuildPrecinct(const FFTOCityBlock& Block);
	void BuildBank(const FFTOCityBlock& Block);

	// ---- Interiors (FTOCityInteriors.cpp) ----
	void Furnish(FFTOBuilding& B, FRandomStream& Rng);
	void FurnishShop(FFTOBuilding& B, FRandomStream& Rng);
	void FurnishDiner(FFTOBuilding& B, FRandomStream& Rng);
	void FurnishBar(FFTOBuilding& B, FRandomStream& Rng);
	void FurnishOffice(FFTOBuilding& B, FRandomStream& Rng);
	void FurnishHome(FFTOBuilding& B, FRandomStream& Rng);
	void FurnishWarehouse(FFTOBuilding& B, FRandomStream& Rng);

	// ---- Streets (FTOCityStreets.cpp) ----
	void BuildStreets(FRandomStream& Rng);
	void DressSidewalks(const FFTOCityBlock& Block, FRandomStream& Rng);
	void AddTree(const FVector& Base, FRandomStream& Rng);
	bool IsDowntownCorner(int32 I, int32 J) const;

	FVector BlockOrigin(int32 X, int32 Y) const;
	/** Street level on top of the sidewalk slab, where buildings stand. */
	float StreetZ() const;

	UPROPERTY(ReplicatedUsing=OnRep_Seed) int32 Seed = 0;

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient) TArray<FFTOCityBlock> Blocks;
	UPROPERTY(Transient) TArray<FFTOBuilding> Buildings;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UStaticMesh>> KitMeshes;
	UPROPERTY() TObjectPtr<UMaterialInterface> CityMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> InteriorMaterial;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> SphereMesh;

	/** The instanced components by name (FindInstanced). */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UInstancedStaticMeshComponent>> Instanced;

	TMap<UStaticMesh*, FBatch> ExteriorBatches;
	TMap<UStaticMesh*, FBatch> InteriorBatches;

	FVector PrecinctLocation = FVector::ZeroVector;
	bool bGeometryBuilt = false;
};
