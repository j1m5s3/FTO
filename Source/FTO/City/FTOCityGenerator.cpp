#include "City/FTOCityGenerator.h"
#include "Crime/FTOCrimeSpawnPoint.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "FTO.h"

namespace FTOCityPalette
{
	enum : int32
	{
		Asphalt, Sidewalk, RoadLine, Grass, Trunk, Leaves, Water, Glass, Roof, Precinct, Bank, Concrete,
		BuildingFirst // pastel building colours follow
	};

	static const FLinearColor Colors[] =
	{
		FLinearColor(0.035f, 0.035f, 0.045f), // Asphalt
		FLinearColor(0.45f, 0.45f, 0.48f),    // Sidewalk
		FLinearColor(0.95f, 0.95f, 0.85f),    // RoadLine
		FLinearColor(0.18f, 0.55f, 0.15f),    // Grass
		FLinearColor(0.30f, 0.17f, 0.08f),    // Trunk
		FLinearColor(0.10f, 0.45f, 0.12f),    // Leaves
		FLinearColor(0.10f, 0.35f, 0.75f),    // Water
		FLinearColor(0.05f, 0.10f, 0.18f),    // Glass
		FLinearColor(0.20f, 0.12f, 0.12f),    // Roof
		FLinearColor(0.08f, 0.20f, 0.70f),    // Precinct
		FLinearColor(0.85f, 0.65f, 0.15f),    // Bank
		FLinearColor(0.30f, 0.30f, 0.30f),    // Concrete
		// Pastel buildings
		FLinearColor(0.95f, 0.55f, 0.55f),
		FLinearColor(0.55f, 0.80f, 0.95f),
		FLinearColor(0.95f, 0.85f, 0.45f),
		FLinearColor(0.65f, 0.90f, 0.60f),
		FLinearColor(0.85f, 0.65f, 0.95f),
		FLinearColor(0.98f, 0.70f, 0.40f),
		FLinearColor(0.90f, 0.90f, 0.90f),
		FLinearColor(0.50f, 0.85f, 0.80f),
	};
	static constexpr int32 NumBuildingColors = UE_ARRAY_COUNT(Colors) - BuildingFirst;
}

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

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
	SphereMesh = Sphere.Object;
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
	UE_LOG(LogFTO, Log, TEXT("City generated: %dx%d blocks, seed %d."), BlocksX, BlocksY, Seed);
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

UInstancedStaticMeshComponent* AFTOCityGenerator::GetISM(UStaticMesh* Mesh, int32 ColorIndex)
{
	const FName Key(*FString::Printf(TEXT("%s_%d"), *GetNameSafe(Mesh), ColorIndex));
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = ISMs.Find(Key))
	{
		return *Found;
	}

	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this, Key);
	ISM->SetupAttachment(Root);
	ISM->SetStaticMesh(Mesh);
	ISM->SetMobility(EComponentMobility::Static);
	ISM->SetCollisionProfileName(TEXT("BlockAll"));
	ISM->RegisterComponent();

	if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Mesh->GetMaterial(0), this))
	{
		const int32 SafeIndex = FMath::Clamp(ColorIndex, 0, int32(UE_ARRAY_COUNT(FTOCityPalette::Colors)) - 1);
		MID->SetVectorParameterValue(TEXT("Color"), FTOCityPalette::Colors[SafeIndex]);
		ISM->SetMaterial(0, MID);
	}

	ISMs.Add(Key, ISM);
	return ISM;
}

// Engine basic shapes are 100 units across and centred on their pivot.
void AFTOCityGenerator::AddBox(int32 ColorIndex, const FVector& Center, const FVector& Size, float Yaw)
{
	GetISM(CubeMesh, ColorIndex)->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), Center, Size / 100.f), true);
}

void AFTOCityGenerator::AddCylinder(int32 ColorIndex, const FVector& Center, const FVector& Size)
{
	GetISM(CylinderMesh, ColorIndex)->AddInstance(FTransform(FRotator::ZeroRotator, Center, Size / 100.f), true);
}

void AFTOCityGenerator::AddSphere(int32 ColorIndex, const FVector& Center, const FVector& Size)
{
	GetISM(SphereMesh, ColorIndex)->AddInstance(FTransform(FRotator::ZeroRotator, Center, Size / 100.f), true);
}

void AFTOCityGenerator::AddLabel(const FVector& Location, const FText& Text, const FColor& Color, float Size)
{
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
	Label->SetupAttachment(Root);
	Label->RegisterComponent();
	Label->SetWorldLocation(Location);
	Label->SetWorldRotation(FRotator(0.f, 180.f, 0.f)); // face -X, towards the front road
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(Size);
	Label->SetTextRenderColor(Color);
	Label->SetText(Text);
}

void AFTOCityGenerator::BuildGeometry()
{
	if (bGeometryBuilt)
	{
		return;
	}
	bGeometryBuilt = true;

	using namespace FTOCityPalette;
	FRandomStream Rng(Seed ^ 0x5EED);

	const float Pitch = BlockSize + RoadWidth;
	const FVector Extent = GetCityExtent();
	const FVector Origin = GetActorLocation();

	// Road surface under everything, plus a grass apron around the city.
	AddBox(Grass, Origin + FVector(0.f, 0.f, CityZ - 60.f), FVector(Extent.X * 2.f + 20000.f, Extent.Y * 2.f + 20000.f, 100.f));
	AddBox(Asphalt, Origin + FVector(0.f, 0.f, CityZ - 25.f), FVector(Extent.X * 2.f + RoadWidth, Extent.Y * 2.f + RoadWidth, 50.f));

	// Dashed centre lines on every road.
	const float DashLength = 300.f;
	const float DashGap = 300.f;
	for (int32 i = 0; i <= BlocksX; ++i)
	{
		const float LineX = Origin.X + (i - BlocksX * 0.5f) * Pitch;
		for (float Y = -Extent.Y; Y < Extent.Y; Y += DashLength + DashGap)
		{
			AddBox(RoadLine, FVector(LineX, Origin.Y + Y + DashLength * 0.5f, CityZ + 1.f), FVector(20.f, DashLength, 2.f));
		}
	}
	for (int32 j = 0; j <= BlocksY; ++j)
	{
		const float LineY = Origin.Y + (j - BlocksY * 0.5f) * Pitch;
		for (float X = -Extent.X; X < Extent.X; X += DashLength + DashGap)
		{
			AddBox(RoadLine, FVector(Origin.X + X + DashLength * 0.5f, LineY, CityZ + 1.f), FVector(DashLength, 20.f, 2.f));
		}
	}

	for (const FFTOCityBlock& Block : Blocks)
	{
		// Raised sidewalk slab for the whole block.
		AddBox(Sidewalk, Block.Center + FVector(0.f, 0.f, CurbHeight * 0.5f), FVector(BlockSize, BlockSize, CurbHeight));

		// Street lamps on the corners.
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const FVector Offset((Corner & 1 ? 1.f : -1.f) * (BlockSize * 0.5f - 80.f), (Corner & 2 ? 1.f : -1.f) * (BlockSize * 0.5f - 80.f), 0.f);
			AddCylinder(Concrete, Block.Center + Offset + FVector(0.f, 0.f, CurbHeight + 250.f), FVector(15.f, 15.f, 500.f));
			AddSphere(RoadLine, Block.Center + Offset + FVector(0.f, 0.f, CurbHeight + 520.f), FVector(50.f));
		}

		if (Block.bPrecinct)       { BuildPrecinct(Block); continue; }
		if (Block.bBank)           { BuildBank(Block); continue; }

		switch (Block.District)
		{
		case EFTODistrict::Downtown:    BuildDowntownBlock(Block, Rng); break;
		case EFTODistrict::Residential: BuildResidentialBlock(Block, Rng); break;
		case EFTODistrict::Industrial:  BuildIndustrialBlock(Block, Rng); break;
		case EFTODistrict::Park:        BuildParkBlock(Block, Rng); break;
		}
	}
}

void AFTOCityGenerator::BuildDowntownBlock(const FFTOCityBlock& Block, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const float Base = CurbHeight;

	// 2 x 2 plots, each a tower with window bands and a roof cap.
	const float Plot = Block.HalfSize;
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector PlotCenter = Block.Center + FVector((i & 1 ? 0.5f : -0.5f) * Plot, (i & 2 ? 0.5f : -0.5f) * Plot, 0.f);
		const float W = Plot * Rng.FRandRange(0.65f, 0.85f);
		const float D = Plot * Rng.FRandRange(0.65f, 0.85f);
		const float H = Rng.FRandRange(1200.f, 5500.f);
		const int32 Color = BuildingFirst + Rng.RandRange(0, NumBuildingColors - 1);

		AddBox(Color, PlotCenter + FVector(0.f, 0.f, Base + H * 0.5f), FVector(W, D, H));
		AddBox(Roof, PlotCenter + FVector(0.f, 0.f, Base + H + 30.f), FVector(W * 1.04f, D * 1.04f, 60.f));

		for (float BandZ = 350.f; BandZ < H - 150.f; BandZ += 380.f)
		{
			AddBox(Glass, PlotCenter + FVector(0.f, 0.f, Base + BandZ), FVector(W + 8.f, D + 8.f, 120.f));
		}
	}
}

void AFTOCityGenerator::BuildResidentialBlock(const FFTOCityBlock& Block, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const float Base = CurbHeight;

	AddBox(Grass, Block.Center + FVector(0.f, 0.f, Base + 2.f), FVector(Block.HalfSize * 2.f, Block.HalfSize * 2.f, 4.f));

	// Houses around the edge of the block, facing outwards.
	const int32 HousesPerSide = 3;
	const float Spacing = (Block.HalfSize * 2.f) / HousesPerSide;
	for (int32 Side = 0; Side < 4; ++Side)
	{
		for (int32 h = 0; h < HousesPerSide; ++h)
		{
			if (Rng.FRand() < 0.15f)
			{
				AddTree(Block.Center + FVector(0.f, 0.f, Base), Rng); // empty lot, just a tree
				continue;
			}

			const float Along = -Block.HalfSize + Spacing * (h + 0.5f);
			const float Inset = Block.HalfSize - 550.f;
			FVector Offset;
			switch (Side)
			{
			case 0:  Offset = FVector(-Inset, Along, 0.f); break;
			case 1:  Offset = FVector(Inset, Along, 0.f); break;
			case 2:  Offset = FVector(Along, -Inset, 0.f); break;
			default: Offset = FVector(Along, Inset, 0.f); break;
			}

			const FVector Center = Block.Center + Offset;
			const float W = Rng.FRandRange(650.f, 850.f);
			const float H = Rng.FRandRange(450.f, 750.f);
			const int32 Color = BuildingFirst + Rng.RandRange(0, NumBuildingColors - 1);
			AddBox(Color, Center + FVector(0.f, 0.f, Base + H * 0.5f), FVector(W, W, H));
			// Chunky stepped roof.
			AddBox(Roof, Center + FVector(0.f, 0.f, Base + H + 60.f), FVector(W * 1.1f, W * 1.1f, 120.f));
			AddBox(Roof, Center + FVector(0.f, 0.f, Base + H + 170.f), FVector(W * 0.7f, W * 0.7f, 100.f));
		}
	}

	for (int32 t = 0; t < 4; ++t)
	{
		AddTree(Block.Center + FVector(Rng.FRandRange(-600.f, 600.f), Rng.FRandRange(-600.f, 600.f), Base), Rng);
	}
}

void AFTOCityGenerator::BuildIndustrialBlock(const FFTOCityBlock& Block, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const float Base = CurbHeight;

	AddBox(Concrete, Block.Center + FVector(0.f, 0.f, Base + 2.f), FVector(Block.HalfSize * 2.f, Block.HalfSize * 2.f, 4.f));

	const int32 Warehouses = Rng.RandRange(1, 2);
	for (int32 w = 0; w < Warehouses; ++w)
	{
		const float OffsetY = Warehouses == 1 ? 0.f : (w == 0 ? -0.45f : 0.45f) * Block.HalfSize;
		const FVector Center = Block.Center + FVector(0.f, OffsetY, 0.f);
		const float W = Block.HalfSize * 1.5f;
		const float D = Block.HalfSize * (Warehouses == 1 ? 1.3f : 0.75f);
		const float H = Rng.FRandRange(700.f, 1000.f);
		AddBox(BuildingFirst + 6, Center + FVector(0.f, 0.f, Base + H * 0.5f), FVector(W, D, H));
		AddBox(Roof, Center + FVector(0.f, 0.f, Base + H + 25.f), FVector(W * 1.02f, D * 1.02f, 50.f));

		// Smokestack.
		const FVector Stack = Center + FVector(W * 0.3f, 0.f, 0.f);
		AddCylinder(Concrete, Stack + FVector(0.f, 0.f, Base + H + 500.f), FVector(160.f, 160.f, 1000.f));
		AddCylinder(BuildingFirst, Stack + FVector(0.f, 0.f, Base + H + 900.f), FVector(170.f, 170.f, 80.f));
	}

	// Shipping crates.
	for (int32 c = 0; c < 5; ++c)
	{
		const FVector Pos = Block.Center + FVector(Rng.FRandRange(-0.9f, 0.9f) * Block.HalfSize, Block.HalfSize * 0.9f * (c % 2 ? 1.f : -1.f), Base + 130.f);
		AddBox(BuildingFirst + Rng.RandRange(0, NumBuildingColors - 1), Pos, FVector(600.f, 250.f, 260.f), Rng.FRandRange(-10.f, 10.f));
	}
}

void AFTOCityGenerator::BuildParkBlock(const FFTOCityBlock& Block, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const float Base = CurbHeight;

	AddBox(Grass, Block.Center + FVector(0.f, 0.f, Base + 2.f), FVector(Block.HalfSize * 2.f, Block.HalfSize * 2.f, 4.f));
	AddCylinder(Water, Block.Center + FVector(0.f, 0.f, Base + 6.f), FVector(1400.f, 1400.f, 6.f));

	const int32 Trees = Rng.RandRange(8, 14);
	for (int32 t = 0; t < Trees; ++t)
	{
		const float Angle = Rng.FRandRange(0.f, 2.f * PI);
		const float Radius = Rng.FRandRange(900.f, Block.HalfSize - 200.f);
		AddTree(Block.Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, Base), Rng);
	}

	// Benches around the pond.
	for (int32 b = 0; b < 4; ++b)
	{
		const float Angle = b * PI * 0.5f + PI * 0.25f;
		const FVector Pos = Block.Center + FVector(FMath::Cos(Angle) * 900.f, FMath::Sin(Angle) * 900.f, Base + 30.f);
		AddBox(Trunk, Pos, FVector(200.f, 60.f, 20.f), FMath::RadiansToDegrees(Angle) + 90.f);
	}
}

void AFTOCityGenerator::BuildPrecinct(const FFTOCityBlock& Block)
{
	using namespace FTOCityPalette;
	const float Base = CurbHeight;

	// Parking lot out front (-X side), station house at the back.
	AddBox(Asphalt, Block.Center + FVector(-Block.HalfSize * 0.5f, 0.f, Base + 2.f), FVector(Block.HalfSize, Block.HalfSize * 2.f, 4.f));
	for (int32 Bay = -3; Bay <= 3; ++Bay)
	{
		AddBox(RoadLine, Block.Center + FVector(-Block.HalfSize * 0.5f, Bay * 400.f, Base + 5.f), FVector(600.f, 15.f, 2.f));
	}

	const FVector House = Block.Center + FVector(Block.HalfSize * 0.45f, 0.f, 0.f);
	const float H = 900.f;
	AddBox(Precinct, House + FVector(0.f, 0.f, Base + H * 0.5f), FVector(1300.f, 2600.f, H));
	AddBox(RoadLine, House + FVector(0.f, 0.f, Base + H + 40.f), FVector(1360.f, 2660.f, 80.f));
	// Door and the obligatory flashing-light roof ornaments.
	AddBox(Glass, House + FVector(-660.f, 0.f, Base + 170.f), FVector(20.f, 400.f, 340.f));
	AddSphere(Water, House + FVector(0.f, -600.f, Base + H + 150.f), FVector(160.f));
	AddSphere(BuildingFirst, House + FVector(0.f, 600.f, Base + H + 150.f), FVector(160.f));

	AddLabel(House + FVector(-680.f, 0.f, Base + H - 250.f), INVTEXT("PRECINCT"), FColor::White, 220.f);
}

void AFTOCityGenerator::BuildBank(const FFTOCityBlock& Block)
{
	using namespace FTOCityPalette;
	const float Base = CurbHeight;
	const float H = 1200.f;

	AddBox(Bank, Block.Center + FVector(0.f, 0.f, Base + H * 0.5f), FVector(Block.HalfSize * 1.4f, Block.HalfSize * 1.4f, H));
	AddBox(RoadLine, Block.Center + FVector(0.f, 0.f, Base + H + 60.f), FVector(Block.HalfSize * 1.5f, Block.HalfSize * 1.5f, 120.f));

	// Columns along the front.
	const float Front = -Block.HalfSize * 0.7f - 150.f;
	for (int32 c = -3; c <= 3; ++c)
	{
		AddCylinder(RoadLine, Block.Center + FVector(Front, c * 350.f, Base + H * 0.5f), FVector(120.f, 120.f, H));
	}

	AddLabel(Block.Center + FVector(Front - 100.f, 0.f, Base + H + 250.f), INVTEXT("BANK"), FColor(255, 220, 80), 300.f);
}

void AFTOCityGenerator::AddTree(const FVector& Base, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const float Height = Rng.FRandRange(250.f, 450.f);
	const float Canopy = Rng.FRandRange(250.f, 400.f);
	AddCylinder(Trunk, Base + FVector(0.f, 0.f, Height * 0.5f), FVector(40.f, 40.f, Height));
	AddSphere(Leaves, Base + FVector(0.f, 0.f, Height + Canopy * 0.35f), FVector(Canopy));
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

		const TArray<FName>* Tags = nullptr;
		switch (Block.District)
		{
		case EFTODistrict::Downtown:    Tags = &FTOSpawnTags::Commercial; break;
		case EFTODistrict::Residential: Tags = &FTOSpawnTags::Home; break;
		case EFTODistrict::Industrial:  Tags = &FTOSpawnTags::Industrial; break;
		case EFTODistrict::Park:        Tags = &FTOSpawnTags::Park; break;
		}
		SpawnPoint(Block.Center + FVector(Edge, 0.f, 0.f) + Z, *Tags, Block.District);
		SpawnPoint(Block.Center + FVector(0.f, -Edge, 0.f) + Z, *Tags, Block.District);
	}

	// Officers clock in at the precinct parking lot.
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector Location = PrecinctLocation + FVector(0.f, (i - 1.5f) * 250.f, 100.f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (APlayerStart* Start = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), Location, FRotator::ZeroRotator, Params))
		{
			Start->PlayerStartTag = TEXT("Precinct");
		}
	}
}
