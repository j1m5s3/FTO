#include "City/FTOCityGenerator.h"
#include "City/FTOCityPalette.h"
#include "Crime/FTOCrimeSpawnPoint.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "FTO.h"

namespace FTOSpawnTags
{
	static const TArray<FName> Street     = { "Jaywalking", "Speeding", "IllegalParking", "CarChase", "PettyTheft", "LostTourist", "Graffiti" };
	static const TArray<FName> Commercial = { "Shoplifting", "ArmedRobbery", "BarFight", "Vandalism", "Riot", "NoiseComplaint", "TerrorPlot", "Graffiti" };
	static const TArray<FName> Home       = { "DomesticDispute", "NoiseComplaint", "Burglary", "CatInTree", "Standoff", "Vandalism" };
	static const TArray<FName> Park       = { "CatInTree", "LostTourist", "Graffiti", "NoiseComplaint", "Riot", "PettyTheft" };
	static const TArray<FName> Industrial = { "Burglary", "Vandalism", "Standoff", "TerrorPlot", "Graffiti", "HostageSituation" };
	static const TArray<FName> Bank       = { "BankHeist", "ArmedRobbery", "HostageSituation" };
}

// Everything sits a hair above Z=0 so it never z-fights with a template floor.
static constexpr float CityZ = 2.f;
static constexpr float CurbHeight = 15.f;

AFTOCityGenerator::AFTOCityGenerator()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	// Static, like the instanced city hung off it.
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> City(TEXT("/Game/FTO/Materials/MI_FTOCity.MI_FTOCity"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Interior(TEXT("/Game/FTO/Materials/MI_FTOCityInterior.MI_FTOCityInterior"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
	SphereMesh = Sphere.Object;
	CityMaterial = City.Object;
	InteriorMaterial = Interior.Object;
}

void AFTOCityGenerator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOCityGenerator, Seed);
}

void AFTOCityGenerator::ServerGenerate(int32 InSeed)
{
	check(HasAuthority());
	Seed = InSeed;
	BuildLayout();
	BuildGeometry();
	SpawnGameplayMarkers();
	UE_LOG(LogFTO, Log, TEXT("City generated: %dx%d blocks, %d buildings, seed %d."), BlocksX, BlocksY, Buildings.Num(), Seed);
}

void AFTOCityGenerator::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority() && Seed != 0)
	{
		OnRep_Seed();
	}
}

void AFTOCityGenerator::OnRep_Seed()
{
	if (!bGeometryBuilt)
	{
		BuildLayout();
		BuildGeometry();
	}
}

FVector AFTOCityGenerator::GetCityExtent() const
{
	const float Pitch = BlockSize + RoadWidth;
	return FVector(BlocksX * Pitch * 0.5f, BlocksY * Pitch * 0.5f, 0.f);
}

FVector AFTOCityGenerator::BlockOrigin(int32 X, int32 Y) const
{
	const float Pitch = BlockSize + RoadWidth;
	return GetActorLocation() + FVector((X - (BlocksX - 1) * 0.5f) * Pitch, (Y - (BlocksY - 1) * 0.5f) * Pitch, CityZ);
}

FVector AFTOCityGenerator::GetIntersection(int32 I, int32 J) const
{
	const float Pitch = BlockSize + RoadWidth;
	return GetActorLocation() + FVector((I - BlocksX * 0.5f) * Pitch, (J - BlocksY * 0.5f) * Pitch, CityZ);
}

FVector AFTOCityGenerator::GetSidewalkCorner(int32 BlockX, int32 BlockY, int32 Corner) const
{
	// Clockwise when seen from above: (-,-) (+,-) (+,+) (-,+)
	static const FVector2D Signs[] = { {-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f} };
	const FVector2D& Sign = Signs[((Corner % 4) + 4) % 4];
	const float Inset = BlockSize * 0.5f - SidewalkWidth * 0.5f;
	return BlockOrigin(BlockX, BlockY) + FVector(Sign.X * Inset, Sign.Y * Inset, CurbHeight);
}

float AFTOCityGenerator::GetCurbHeight() const
{
	return CityZ + CurbHeight;
}

float AFTOCityGenerator::StreetZ() const
{
	return GetActorLocation().Z + CityZ + CurbHeight;
}

const FFTOBuilding* AFTOCityGenerator::FindBuilding(EFTOBuildingType Type) const
{
	return Buildings.FindByPredicate([Type](const FFTOBuilding& B) { return B.Type == Type; });
}

void AFTOCityGenerator::BuildLayout()
{
	Blocks.Reset();
	FRandomStream Rng(Seed);

	const float CX = (BlocksX - 1) * 0.5f;
	const float CY = (BlocksY - 1) * 0.5f;
	const int32 PrecinctX = FMath::FloorToInt(CX);
	const int32 PrecinctY = FMath::FloorToInt(CY);
	const int32 IndustrialEdge = Rng.RandRange(0, 3); // which side of town is industrial

	bool bPlacedBank = false;
	for (int32 X = 0; X < BlocksX; ++X)
	{
		for (int32 Y = 0; Y < BlocksY; ++Y)
		{
			FFTOCityBlock& Block = Blocks.AddDefaulted_GetRef();
			Block.X = X;
			Block.Y = Y;
			Block.Center = BlockOrigin(X, Y);
			Block.HalfSize = BlockSize * 0.5f - SidewalkWidth;

			const float Ring = FMath::Max(FMath::Abs(X - CX) / FMath::Max(1.f, CX), FMath::Abs(Y - CY) / FMath::Max(1.f, CY));
			const bool bOnIndustrialEdge =
				(IndustrialEdge == 0 && X == 0) || (IndustrialEdge == 1 && X == BlocksX - 1) ||
				(IndustrialEdge == 2 && Y == 0) || (IndustrialEdge == 3 && Y == BlocksY - 1);

			if (X == PrecinctX && Y == PrecinctY)
			{
				Block.bPrecinct = true;
				Block.District = EFTODistrict::Downtown;
			}
			else if (bOnIndustrialEdge)
			{
				Block.District = EFTODistrict::Industrial;
			}
			else if (Ring < 0.45f)
			{
				Block.District = Rng.FRand() < 0.1f ? EFTODistrict::Park : EFTODistrict::Downtown;
			}
			else
			{
				Block.District = Rng.FRand() < 0.15f ? EFTODistrict::Park : EFTODistrict::Residential;
			}

			if (!bPlacedBank && Block.District == EFTODistrict::Downtown && !Block.bPrecinct && FMath::Abs(X - PrecinctX) + FMath::Abs(Y - PrecinctY) >= 2)
			{
				Block.bBank = true;
				bPlacedBank = true;
			}
		}
	}

	for (const FFTOCityBlock& Block : Blocks)
	{
		if (Block.bPrecinct)
		{
			// Front steps face the -X road.
			PrecinctLocation = Block.Center + FVector(-Block.HalfSize + 200.f, 0.f, CurbHeight);
		}
	}
}

// ------------------------------------------------------------------------------------------
// Instancing
// ------------------------------------------------------------------------------------------

UStaticMesh* AFTOCityGenerator::Kit(const TCHAR* Piece)
{
	const FName Key(Piece);
	if (const TObjectPtr<UStaticMesh>* Found = KitMeshes.Find(Key))
	{
		return *Found;
	}
	// /Game/FTO is always cooked, so loading the kit by name works in packaged builds too.
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/FTO/Kit/%s.%s"), Piece, Piece));
	if (!Mesh)
	{
		UE_LOG(LogFTO, Warning, TEXT("City: kit piece %s is missing (run Tools/Unreal/import_art.py)."), Piece);
	}
	KitMeshes.Add(Key, Mesh);
	return Mesh;
}

void AFTOCityGenerator::Place(UStaticMesh* Mesh, const FTransform& Transform, const FLinearColor& Tint, bool bInterior)
{
	if (!Mesh)
	{
		return;
	}
	FBatch& Batch = (bInterior ? InteriorBatches : ExteriorBatches).FindOrAdd(Mesh);
	Batch.Transforms.Add(Transform);
	Batch.Colors.Append({ Tint.R, Tint.G, Tint.B });
}

void AFTOCityGenerator::Place(const TCHAR* Piece, const FTransform& Transform, const FLinearColor& Tint, bool bInterior)
{
	Place(Kit(Piece), Transform, Tint, bInterior);
}

void AFTOCityGenerator::FlushInstances()
{
	int32 Components = 0;
	int32 Instances = 0;
	auto Flush = [&](TMap<UStaticMesh*, FBatch>& Batches, bool bInterior)
	{
		for (TPair<UStaticMesh*, FBatch>& Pair : Batches)
		{
			UStaticMesh* Mesh = Pair.Key;
			FBatch& Batch = Pair.Value;
			if (Batch.Transforms.IsEmpty())
			{
				continue;
			}

			const FName Name(*FString::Printf(TEXT("%s%s"), *GetNameSafe(Mesh), bInterior ? TEXT("_In") : TEXT("")));
			UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this, Name);
			ISM->SetupAttachment(Root);
			ISM->SetMobility(EComponentMobility::Static);
			ISM->SetStaticMesh(Mesh);
			ISM->SetCollisionProfileName(TEXT("BlockAll"));
			ISM->SetCanEverAffectNavigation(false);
			ISM->SetNumCustomDataFloats(3);

			// Paint comes from each instance's custom data (MI_FTOCity); rooms get the slightly self-lit
			// variant so they read clearly through shop windows.
			UMaterialInterface* Paint = bInterior ? InteriorMaterial : CityMaterial;
			const int32 BodySlot = Mesh->GetMaterialIndex(TEXT("Body"));
			if (BodySlot != INDEX_NONE)
			{
				ISM->SetMaterial(BodySlot, Paint);
			}
			else if (Mesh == CubeMesh || Mesh == CylinderMesh || Mesh == SphereMesh)
			{
				ISM->SetMaterial(0, Paint);
			}

			ISM->RegisterComponent();
			ISM->AddInstances(Batch.Transforms, false, true, false);
			ISM->SetCustomData(0, Batch.Transforms.Num() - 1, Batch.Colors, true);

			++Components;
			Instances += Batch.Transforms.Num();
		}
		Batches.Reset();
	};
	Flush(ExteriorBatches, false);
	Flush(InteriorBatches, true);
	UE_LOG(LogFTO, Log, TEXT("City: %d instances in %d instanced components."), Instances, Components);
}

// Engine basic shapes are 100 units across and centred on their pivot.
void AFTOCityGenerator::AddBox(const FLinearColor& Color, const FVector& Center, const FVector& Size, float Yaw, bool bInterior)
{
	Place(CubeMesh, FTransform(FRotator(0.f, Yaw, 0.f), Center, Size / 100.f), Color, bInterior);
}

void AFTOCityGenerator::AddCylinder(const FLinearColor& Color, const FVector& Center, const FVector& Size)
{
	Place(CylinderMesh, FTransform(FRotator::ZeroRotator, Center, Size / 100.f), Color);
}

void AFTOCityGenerator::AddSphere(const FLinearColor& Color, const FVector& Center, const FVector& Size)
{
	Place(SphereMesh, FTransform(FRotator::ZeroRotator, Center, Size / 100.f), Color);
}

void AFTOCityGenerator::AddLabel(const FVector& Location, float Yaw, const FText& Text, const FColor& Color, float Size)
{
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
	Label->SetupAttachment(Root);
	Label->RegisterComponent();
	Label->SetWorldLocation(Location);
	Label->SetWorldRotation(FRotator(0.f, Yaw, 0.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(Size);
	Label->SetTextRenderColor(Color);
	Label->SetText(Text);
}

// ------------------------------------------------------------------------------------------
// Geometry
// ------------------------------------------------------------------------------------------

void AFTOCityGenerator::BuildGeometry()
{
	if (bGeometryBuilt)
	{
		return;
	}
	bGeometryBuilt = true;
	const double StartTime = FPlatformTime::Seconds();

	using namespace FTOCityPalette;
	Buildings.Reset();
	FRandomStream Rng(Seed ^ 0x5EED);

	const float Pitch = BlockSize + RoadWidth;
	const FVector Extent = GetCityExtent();
	const FVector Origin = GetActorLocation();

	// Road surface under everything, plus a grass apron around the city.
	AddBox(Grass, Origin + FVector(0.f, 0.f, CityZ - 60.f), FVector(Extent.X * 2.f + 20000.f, Extent.Y * 2.f + 20000.f, 100.f));
	AddBox(Asphalt, Origin + FVector(0.f, 0.f, CityZ - 25.f), FVector(Extent.X * 2.f + RoadWidth, Extent.Y * 2.f + RoadWidth, 50.f));

	// Dashed centre lines on every road (they stop short of each junction's crossings).
	const float DashLength = 300.f;
	const float DashGap = 300.f;
	for (int32 i = 0; i <= BlocksX; ++i)
	{
		const float LineX = Origin.X + (i - BlocksX * 0.5f) * Pitch;
		for (float Y = -Extent.Y; Y < Extent.Y; Y += DashLength + DashGap)
		{
			const float Local = FMath::Fmod(Y + Extent.Y + Pitch * 0.5f, Pitch) - Pitch * 0.5f;
			if (FMath::Abs(Local + DashLength * 0.5f) < RoadWidth * 0.5f + 400.f)
			{
				continue;
			}
			AddBox(RoadLine, FVector(LineX, Origin.Y + Y + DashLength * 0.5f, CityZ + 1.f), FVector(20.f, DashLength, 2.f));
		}
	}
	for (int32 j = 0; j <= BlocksY; ++j)
	{
		const float LineY = Origin.Y + (j - BlocksY * 0.5f) * Pitch;
		for (float X = -Extent.X; X < Extent.X; X += DashLength + DashGap)
		{
			const float Local = FMath::Fmod(X + Extent.X + Pitch * 0.5f, Pitch) - Pitch * 0.5f;
			if (FMath::Abs(Local + DashLength * 0.5f) < RoadWidth * 0.5f + 400.f)
			{
				continue;
			}
			AddBox(RoadLine, FVector(Origin.X + X + DashLength * 0.5f, LineY, CityZ + 1.f), FVector(DashLength, 20.f, 2.f));
		}
	}

	for (const FFTOCityBlock& Block : Blocks)
	{
		// Raised sidewalk slab for the whole block.
		AddBox(Sidewalk, Block.Center + FVector(0.f, 0.f, CurbHeight * 0.5f), FVector(BlockSize, BlockSize, CurbHeight));
	}

	BuildStreets(Rng);

	for (const FFTOCityBlock& Block : Blocks)
	{
		DressSidewalks(Block, Rng);

		if (Block.bPrecinct) { BuildPrecinct(Block); continue; }
		if (Block.bBank)     { BuildBank(Block); continue; }

		switch (Block.District)
		{
		case EFTODistrict::Downtown:    BuildDowntownBlock(Block, Rng); break;
		case EFTODistrict::Residential: BuildResidentialBlock(Block, Rng); break;
		case EFTODistrict::Industrial:  BuildIndustrialBlock(Block, Rng); break;
		case EFTODistrict::Park:        BuildParkBlock(Block, Rng); break;
		}
	}

	FlushInstances();
	UE_LOG(LogFTO, Log, TEXT("City: built %d enterable buildings in %.2f s."), Buildings.Num(), FPlatformTime::Seconds() - StartTime);
}

void AFTOCityGenerator::SpawnGameplayMarkers()
{
	check(HasAuthority());
	UWorld* World = GetWorld();

	auto SpawnPoint = [&](const FVector& Location, const TArray<FName>& Allowed, EFTODistrict District)
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		if (AFTOCrimeSpawnPoint* Point = World->SpawnActor<AFTOCrimeSpawnPoint>(AFTOCrimeSpawnPoint::StaticClass(), Location, FRotator::ZeroRotator, Params))
		{
			Point->AllowedTemplates = Allowed;
			Point->District = FName(*UEnum::GetValueAsString(District));
		}
	};

	for (const FFTOCityBlock& Block : Blocks)
	{
		const float Edge = BlockSize * 0.5f - SidewalkWidth * 0.5f;
		const FVector Z(0.f, 0.f, CurbHeight);

		// Street-side points on the sidewalk midpoints.
		SpawnPoint(Block.Center + FVector(-Edge, 0.f, 0.f) + Z, FTOSpawnTags::Street, Block.District);
		SpawnPoint(Block.Center + FVector(0.f, Edge, 0.f) + Z, FTOSpawnTags::Street, Block.District);

		if (Block.bPrecinct)
		{
			continue;
		}
		if (Block.bBank)
		{
			SpawnPoint(Block.Center + FVector(-Block.HalfSize * 0.7f - 350.f, 0.f, 0.f) + Z, FTOSpawnTags::Bank, Block.District);
			continue;
		}

		const TArray<FName>* AllowedTags = nullptr;
		switch (Block.District)
		{
		case EFTODistrict::Downtown:    AllowedTags = &FTOSpawnTags::Commercial; break;
		case EFTODistrict::Residential: AllowedTags = &FTOSpawnTags::Home; break;
		case EFTODistrict::Industrial:  AllowedTags = &FTOSpawnTags::Industrial; break;
		case EFTODistrict::Park:        AllowedTags = &FTOSpawnTags::Park; break;
		}
		SpawnPoint(Block.Center + FVector(Edge, 0.f, 0.f) + Z, *AllowedTags, Block.District);
		SpawnPoint(Block.Center + FVector(0.f, -Edge, 0.f) + Z, *AllowedTags, Block.District);
	}

	// Officers clock in at the precinct parking lot.
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector Location = PrecinctLocation + FVector(0.f, (i - 1.5f) * 250.f, 100.f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (APlayerStart* Start = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), Location, FRotator(0.f, 180.f, 0.f), Params))
		{
			Start->PlayerStartTag = TEXT("Precinct");
		}
	}
}

TArray<FTransform> AFTOCityGenerator::GetPrecinctParkingSpots() const
{
	TArray<FTransform> Spots;
	for (const FFTOCityBlock& Block : Blocks)
	{
		if (!Block.bPrecinct)
		{
			continue;
		}
		// Bays sit between the painted lines at +-200 and +-600 (see BuildPrecinct).
		for (const float Y : { -600.f, -200.f, 200.f, 600.f })
		{
			const FVector Location = Block.Center + FVector(-Block.HalfSize * 0.5f, Y, CurbHeight + 4.f);
			Spots.Emplace(FRotator(0.f, 180.f, 0.f), Location);
		}
	}
	return Spots;
}
