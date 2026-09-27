// Building assembly for AFTOCityGenerator: facades from the kit's wall panels, roofs, the ground-floor
// rooms, and the special buildings (bank, precinct). Interiors are furnished in FTOCityInteriors.cpp.
#include "City/FTOCityGenerator.h"
#include "City/FTOCityPalette.h"
#include "Engine/StaticMesh.h"

using namespace FTOKit;

namespace
{
	struct FBusiness
	{
		const TCHAR* Sign;      // SM_Sign_<Sign> and the name on the room
		EFTOBuildingType Type;
	};

	const FBusiness Businesses[] =
	{
		{ TEXT("Diner"), EFTOBuildingType::Diner }, { TEXT("Coffee"), EFTOBuildingType::Diner },
		{ TEXT("Pizza"), EFTOBuildingType::Diner }, { TEXT("Donuts"), EFTOBuildingType::Diner },
		{ TEXT("Bar"), EFTOBuildingType::Bar }, { TEXT("Market"), EFTOBuildingType::Shop },
		{ TEXT("Books"), EFTOBuildingType::Shop }, { TEXT("Toys"), EFTOBuildingType::Shop },
		{ TEXT("Laundry"), EFTOBuildingType::Shop }, { TEXT("Offices"), EFTOBuildingType::Office },
	};

	const TCHAR* FamilyNames[] =
	{
		TEXT("The Smiths"), TEXT("The Garcias"), TEXT("The Nguyens"), TEXT("The O'Briens"), TEXT("The Patels"),
		TEXT("The Kowalskis"), TEXT("The Johnsons"), TEXT("The Okafors"), TEXT("The Rossis"), TEXT("The Tanakas"),
	};
}

// ------------------------------------------------------------------------------------------
// Facades
// ------------------------------------------------------------------------------------------

FVector AFTOCityGenerator::FaceNormal(EFace Face)
{
	switch (Face)
	{
	case EFace::PosX: return FVector(1.f, 0.f, 0.f);
	case EFace::NegX: return FVector(-1.f, 0.f, 0.f);
	case EFace::PosY: return FVector(0.f, 1.f, 0.f);
	default:          return FVector(0.f, -1.f, 0.f);
	}
}

int32 AFTOCityGenerator::FacePanels(const FFootprint& F, EFace Face) const
{
	return (Face == EFace::PosX || Face == EFace::NegX) ? F.PanelsY : F.PanelsX;
}

FTransform AFTOCityGenerator::PanelTransform(const FFootprint& F, EFace Face, float Index, float Z) const
{
	// Faces run round the building: +X along +Y, +Y along -X, -X along -Y, -Y along +X.
	const float HX = F.HalfX();
	const float HY = F.HalfY();
	FVector Start;
	FVector Along;
	switch (Face)
	{
	case EFace::PosX: Start = FVector(HX, -HY, 0.f); Along = FVector(0.f, 1.f, 0.f); break;
	case EFace::PosY: Start = FVector(HX, HY, 0.f);  Along = FVector(-1.f, 0.f, 0.f); break;
	case EFace::NegX: Start = FVector(-HX, HY, 0.f); Along = FVector(0.f, -1.f, 0.f); break;
	default:          Start = FVector(-HX, -HY, 0.f); Along = FVector(1.f, 0.f, 0.f); break;
	}
	return FTransform(FaceNormal(Face).Rotation(), F.Center + Start + Along * (PanelWidth * (Index + 0.5f)) + FVector(0.f, 0.f, Z));
}

void AFTOCityGenerator::BuildGroundFace(const FFootprint& F, EFace Face, const TArray<EPanel>& Panels, const FLinearColor& Paint)
{
	// Ground-floor walls share the rooms' slightly self-lit paint: their plaster side is the room's walls.
	const bool bRoomSide = true;
	for (int32 i = 0; i < Panels.Num(); ++i)
	{
		const FTransform T = PanelTransform(F, Face, i, 0.f);
		switch (Panels[i])
		{
		case EPanel::Plain:    Place(TEXT("SM_Wall_G_Plain"), T, Paint, bRoomSide); break;
		case EPanel::Window:   Place(TEXT("SM_Wall_G_Window"), T, Paint, bRoomSide); Place(TEXT("SM_Wall_G_Window_Glass"), T); break;
		case EPanel::Door:     Place(TEXT("SM_Wall_G_Door"), T, Paint, bRoomSide); break;
		case EPanel::Shop:     Place(TEXT("SM_Wall_G_Shop"), T, Paint, bRoomSide); Place(TEXT("SM_Wall_G_Shop_Glass"), T); break;
		case EPanel::ShopDoor: Place(TEXT("SM_Wall_G_ShopDoor"), T, Paint, bRoomSide); break;
		case EPanel::Roller:   Place(TEXT("SM_Wall_G_Roller"), PanelTransform(F, Face, i + 0.5f, 0.f), Paint, bRoomSide); break; // also covers i + 1
		default: break;
		}
	}
}

void AFTOCityGenerator::BuildUpperFloors(const FFootprint& F, int32 Floors, const FLinearColor& Paint, FRandomStream& Rng, bool bWide,
	int32 SpecialFace, int32 SpecialIndex, const TCHAR* SpecialPiece)
{
	for (int32 Floor = 0; Floor < Floors; ++Floor)
	{
		const float Z = GroundHeight + Floor * UpperHeight;
		for (const EFace Face : { EFace::PosX, EFace::NegX, EFace::PosY, EFace::NegY })
		{
			const int32 N = FacePanels(F, Face);
			for (int32 i = 0; i < N; ++i)
			{
				// Plain panels at the ends of longer faces frame the windows.
				const bool bEnd = N > 3 && (i == 0 || i == N - 1);
				const TCHAR* Piece = bEnd ? TEXT("SM_Wall_U_Plain") : (bWide ? TEXT("SM_Wall_U_Wide") : TEXT("SM_Wall_U_Window"));
				if (int32(Face) == SpecialFace && i == SpecialIndex)
				{
					Piece = SpecialPiece;
				}
				if (Piece)
				{
					Place(Piece, PanelTransform(F, Face, i, Z), Paint);
				}
			}
		}
	}
}

void AFTOCityGenerator::BuildCorners(const FFootprint& F, int32 UpperFloors, const FLinearColor& Paint)
{
	const float HX = F.HalfX();
	const float HY = F.HalfY();
	const FVector Offsets[] = { FVector(HX, HY, 0.f), FVector(-HX, HY, 0.f), FVector(-HX, -HY, 0.f), FVector(HX, -HY, 0.f) };
	for (int32 c = 0; c < 4; ++c)
	{
		// The pilaster's +X and +Y are the two faces meeting at the corner.
		const FRotator Rot(0.f, c * 90.f, 0.f);
		Place(TEXT("SM_Corner_G"), FTransform(Rot, F.Center + Offsets[c]), Paint);
		for (int32 Floor = 0; Floor < UpperFloors; ++Floor)
		{
			Place(TEXT("SM_Corner_U"), FTransform(Rot, F.Center + Offsets[c] + FVector(0.f, 0.f, GroundHeight + Floor * UpperHeight)), Paint);
		}
	}
}

void AFTOCityGenerator::BuildRoof(const FFootprint& F, float RoofZ, const FLinearColor& Paint, FRandomStream& Rng, bool bRooftopClutter)
{
	using namespace FTOCityPalette;
	const float HX = F.HalfX();
	const float HY = F.HalfY();

	AddBox(Roof, F.Center + FVector(0.f, 0.f, RoofZ + 5.f), FVector(HX * 2.f - 10.f, HY * 2.f - 10.f, 30.f));
	for (const EFace Face : { EFace::PosX, EFace::NegX, EFace::PosY, EFace::NegY })
	{
		for (int32 i = 0; i < FacePanels(F, Face); ++i)
		{
			Place(TEXT("SM_Parapet"), PanelTransform(F, Face, i, RoofZ), Paint);
		}
	}
	const FVector Offsets[] = { FVector(HX, HY, 0.f), FVector(-HX, HY, 0.f), FVector(-HX, -HY, 0.f), FVector(HX, -HY, 0.f) };
	for (int32 c = 0; c < 4; ++c)
	{
		Place(TEXT("SM_ParapetCorner"), FTransform(FRotator(0.f, c * 90.f, 0.f), F.Center + Offsets[c] + FVector(0.f, 0.f, RoofZ)));
	}
	if (!bRooftopClutter)
	{
		return;
	}

	// One thing per quarter of the roof: a stair hut, a water tank, AC units, vents, an aerial.
	const float Top = RoofZ + 20.f;
	const FVector Quarters[] = { FVector(0.5f, 0.5f, 0.f), FVector(-0.5f, 0.5f, 0.f), FVector(-0.5f, -0.5f, 0.f), FVector(0.5f, -0.5f, 0.f) };
	const int32 First = Rng.RandRange(0, 3);
	for (int32 q = 0; q < 4; ++q)
	{
		const FVector Spot = F.Center + FVector(Quarters[(First + q) % 4].X * (HX - 150.f), Quarters[(First + q) % 4].Y * (HY - 150.f), Top);
		const float Yaw = Rng.RandRange(0, 3) * 90.f;
		const float Roll = Rng.FRand();
		const TCHAR* Piece =
			q == 0 ? TEXT("SM_Roof_Hut") :
			(q == 1 && HX > 550.f && HY > 550.f && Roll < 0.6f) ? TEXT("SM_Roof_WaterTower") :
			Roll < 0.55f ? TEXT("SM_Roof_AC") :
			Roll < 0.85f ? TEXT("SM_Roof_Vent") : TEXT("SM_Roof_Antenna");
		Place(Piece, FTransform(FRotator(0.f, Yaw, 0.f), Spot), Paint);
		if (FCString::Strcmp(Piece, TEXT("SM_Roof_AC")) == 0 && Rng.FRand() < 0.5f)
		{
			Place(TEXT("SM_Roof_AC"), FTransform(FRotator(0.f, Yaw, 0.f), Spot + FRotator(0.f, Yaw, 0.f).RotateVector(FVector(0.f, 140.f, 0.f))));
		}
	}
}

FFTOBuilding& AFTOCityGenerator::AddRoom(const FFootprint& F, EFace DoorFace, float DoorIndex, EFTOBuildingType Type, const FString& Name, float CeilingHeight)
{
	using namespace FTOCityPalette;
	FFTOBuilding& B = Buildings.AddDefaulted_GetRef();
	B.Type = Type;
	B.Name = Name;

	// The room frame: on the inside of the door, X pointing into the room, standing on the floor.
	const FVector Out = FaceNormal(DoorFace);
	const FTransform Door = PanelTransform(F, DoorFace, DoorIndex, 0.f);
	B.Room = FTransform((-Out).Rotation(), Door.GetLocation() - Out * WallThickness + FVector(0.f, 0.f, 5.f));
	B.DoorOutside = Door.GetLocation() + Out * 160.f;

	const float IX = F.HalfX() - WallThickness;
	const float IY = F.HalfY() - WallThickness;
	FVector2D Min(TNumericLimits<float>::Max());
	FVector2D Max(-TNumericLimits<float>::Max());
	for (const FVector& Corner : { FVector(IX, IY, 0.f), FVector(-IX, IY, 0.f), FVector(-IX, -IY, 0.f), FVector(IX, -IY, 0.f) })
	{
		const FVector Local = B.Room.InverseTransformPosition(F.Center + Corner + FVector(0.f, 0.f, 5.f));
		Min = FVector2D(FMath::Min(Min.X, Local.X), FMath::Min(Min.Y, Local.Y));
		Max = FVector2D(FMath::Max(Max.X, Local.X), FMath::Max(Max.Y, Local.Y));
	}
	B.Depth = Max.X;
	B.YMin = Min.Y;
	B.YMax = Max.Y;

	// Floor, ceiling, and a grid of glowing ceiling panels.
	FLinearColor Floor = FloorTile;
	switch (Type)
	{
	case EFTOBuildingType::Diner:     Floor = FloorDiner; break;
	case EFTOBuildingType::Bar:       Floor = FloorWood; break;
	case EFTOBuildingType::Home:      Floor = FloorWood; break;
	case EFTOBuildingType::Office:    Floor = FloorCarpet; break;
	case EFTOBuildingType::Warehouse: Floor = FloorConcrete; break;
	case EFTOBuildingType::Bank:      Floor = FloorMarble; break;
	case EFTOBuildingType::Precinct:  Floor = FloorTile; break;
	default: break;
	}
	AddBox(Floor, F.Center + FVector(0.f, 0.f, 2.5f), FVector(IX * 2.f + 4.f, IY * 2.f + 4.f, 5.f), 0.f, true);
	AddBox(Ceiling, F.Center + FVector(0.f, 0.f, CeilingHeight - 10.f), FVector(IX * 2.f + 4.f, IY * 2.f + 4.f, 20.f), 0.f, true);

	const int32 LightsX = FMath::Max(1, FMath::RoundToInt(IX * 2.f / 400.f));
	const int32 LightsY = FMath::Max(1, FMath::RoundToInt(IY * 2.f / 400.f));
	for (int32 i = 0; i < LightsX; ++i)
	{
		for (int32 j = 0; j < LightsY; ++j)
		{
			const FVector Spot(-IX + IX * 2.f * (i + 0.5f) / LightsX, -IY + IY * 2.f * (j + 0.5f) / LightsY, CeilingHeight - 20.f);
			Place(TEXT("SM_CeilingLight"), FTransform(F.Center + Spot), White, true);
		}
	}
	return B;
}

FTransform AFTOCityGenerator::RoomToWorld(const FFTOBuilding& B, float X, float Y, float Yaw, float Z) const
{
	return FTransform(FRotator(0.f, B.Room.Rotator().Yaw + Yaw, 0.f), B.Room.TransformPosition(FVector(X, Y, Z)));
}

void AFTOCityGenerator::PlaceInRoom(const FFTOBuilding& B, const TCHAR* Piece, float X, float Y, float Yaw, const FLinearColor& Tint, float Z)
{
	Place(Piece, RoomToWorld(B, X, Y, Yaw, Z), Tint, true);
}

void AFTOCityGenerator::InteriorWall(const FFTOBuilding& B, bool bAlongY, float Line, float From, float To, TArrayView<const float> Doors, const FLinearColor& Paint)
{
	const float Length = To - From;
	const int32 Whole = FMath::FloorToInt(Length / PanelWidth + KINDA_SMALL_NUMBER);
	auto Put = [&](const TCHAR* Piece, float Centre, float Width)
	{
		const float X = bAlongY ? Line : Centre;
		const float Y = bAlongY ? Centre : Line;
		FTransform T = RoomToWorld(B, X, Y, bAlongY ? 0.f : 90.f);
		T.SetScale3D(FVector(1.f, Width / PanelWidth, 1.f));
		Place(Piece, T, Paint, true);
	};
	for (int32 i = 0; i < Whole; ++i)
	{
		const float Centre = From + PanelWidth * (i + 0.5f);
		const bool bDoor = Doors.ContainsByPredicate([Centre](float D) { return FMath::Abs(D - Centre) < PanelWidth * 0.5f; });
		Put(bDoor ? TEXT("SM_IWall_Door") : TEXT("SM_IWall_Plain"), Centre, PanelWidth);
	}
	const float Rest = Length - Whole * PanelWidth;
	if (Rest > 1.f)
	{
		Put(TEXT("SM_IWall_Plain"), To - Rest * 0.5f, Rest);
	}
}

// ------------------------------------------------------------------------------------------
// Downtown: towers with shops, diners, bars and offices on the ground floor
// ------------------------------------------------------------------------------------------

void AFTOCityGenerator::BuildDowntownBlock(const FFTOCityBlock& Block, FRandomStream& Rng)
{
	for (const int32 QX : { -1, 1 })
	{
		for (const int32 QY : { -1, 1 })
		{
			BuildTower(Block, QX, QY, Rng);
		}
	}
}

void AFTOCityGenerator::BuildTower(const FFTOCityBlock& Block, int32 QuadX, int32 QuadY, FRandomStream& Rng)
{
	using namespace FTOCityPalette;

	// One tower per quarter of the block, pushed out to its street corner behind a little apron.
	FFootprint F;
	F.PanelsX = Rng.RandRange(5, 7);
	F.PanelsY = Rng.RandRange(5, 7);
	const float Apron = 60.f;
	F.Center = FVector(Block.Center.X + QuadX * (Block.HalfSize - Apron - F.HalfX()),
		Block.Center.Y + QuadY * (Block.HalfSize - Apron - F.HalfY()), StreetZ());

	const EFace StreetX = QuadX > 0 ? EFace::PosX : EFace::NegX;
	const EFace StreetY = QuadY > 0 ? EFace::PosY : EFace::NegY;
	const EFace DoorFace = Rng.FRand() < 0.5f ? StreetX : StreetY;
	const int32 DoorIndex = FacePanels(F, DoorFace) / 2;

	const FBusiness& Biz = Businesses[Rng.RandRange(0, UE_ARRAY_COUNT(Businesses) - 1)];
	const FLinearColor Paint = Building(Rng.RandRange(0, 8));
	const FLinearColor Brand = Accent(Rng.RandRange(0, 7));
	const int32 Floors = Rng.RandRange(2, 11); // storeys above the shop
	// The residents' lift: street doors on the other street face, near the corner.
	const EFace LiftFace = DoorFace == StreetX ? StreetY : StreetX;
	const int32 LiftIndex = 1;

	// Ground floor: shop windows on the street, windows round the back, the door mid-front.
	for (const EFace Face : { EFace::PosX, EFace::NegX, EFace::PosY, EFace::NegY })
	{
		const int32 N = FacePanels(F, Face);
		const bool bStreet = Face == StreetX || Face == StreetY;
		TArray<EPanel> Panels;
		for (int32 i = 0; i < N; ++i)
		{
			Panels.Add(bStreet ? EPanel::Shop : (i % 2 ? EPanel::Window : EPanel::Plain));
		}
		if (Face == DoorFace)
		{
			Panels[DoorIndex] = EPanel::ShopDoor;
		}
		if (Face == LiftFace)
		{
			Panels[LiftIndex] = EPanel::Plain;
		}
		BuildGroundFace(F, Face, Panels, Paint);

		if (bStreet)
		{
			for (int32 i = 0; i < N; ++i)
			{
				Place(TEXT("SM_Cornice"), PanelTransform(F, Face, i, GroundHeight));
				// Awnings over the shop windows, clear of the sign over the door (and the lift doors).
				if ((Face != DoorFace || FMath::Abs(i - DoorIndex) >= 2) && !(Face == LiftFace && i == LiftIndex))
				{
					Place(TEXT("SM_Awning"), PanelTransform(F, Face, i, 0.f), Brand);
				}
			}
		}
	}
	Place(*FString::Printf(TEXT("SM_Sign_%s"), Biz.Sign), PanelTransform(F, DoorFace, DoorIndex, 0.f), Brand);

	BuildUpperFloors(F, Floors, Paint, Rng, Rng.FRand() < 0.4f, int32(LiftFace), LiftIndex, TEXT("SM_Wall_U_Plain"));
	BuildCorners(F, Floors, Paint);
	BuildRoof(F, GroundHeight + Floors * UpperHeight, Paint, Rng, true);
	PlanLift(F, LiftFace, LiftIndex, Floors);
	BuildUpperStoreys(F, Floors, false, GroundHeight + Floors * UpperHeight - 12.f, QuarterOf(F, PanelTransform(F, LiftFace, LiftIndex, 0.f).GetLocation()), Rng);

	FFTOBuilding& Room = AddRoom(F, DoorFace, DoorIndex, Biz.Type, FString(Biz.Sign).ToUpper(), GroundHeight);
	Furnish(Room, Rng);
}

// ------------------------------------------------------------------------------------------
// Residential: houses with porches, picket fences and gardens
// ------------------------------------------------------------------------------------------

void AFTOCityGenerator::BuildResidentialBlock(const FFTOCityBlock& Block, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const float H = Block.HalfSize;
	AddBox(Grass, FVector(Block.Center.X, Block.Center.Y, StreetZ() + 2.f), FVector(H * 2.f, H * 2.f, 4.f));

	// Three lots down each X side, one in the middle of each Y side, a shared garden in the middle.
	const float Lot = H * 2.f / 3.f;
	for (int32 k = 0; k < 3; ++k)
	{
		const float Y = -H + Lot * (k + 0.5f);
		BuildHouse(FVector(Block.Center.X - H, Block.Center.Y + Y, 0.f), EFace::NegX, Rng);
		BuildHouse(FVector(Block.Center.X + H, Block.Center.Y + Y, 0.f), EFace::PosX, Rng);
	}
	BuildHouse(FVector(Block.Center.X, Block.Center.Y - H, 0.f), EFace::NegY, Rng);
	BuildHouse(FVector(Block.Center.X, Block.Center.Y + H, 0.f), EFace::PosY, Rng);

	for (int32 t = 0; t < 3; ++t)
	{
		AddTree(FVector(Block.Center.X + Rng.FRandRange(-350.f, 350.f), Block.Center.Y + Rng.FRandRange(-350.f, 350.f), StreetZ()), Rng);
	}
}

void AFTOCityGenerator::BuildHouse(const FVector& FrontCenter, EFace Facing, FRandomStream& Rng)
{
	using namespace FTOCityPalette;

	const FVector Out = FaceNormal(Facing);
	const FVector Along(-Out.Y, Out.X, 0.f);
	const bool bFacesX = Facing == EFace::PosX || Facing == EFace::NegX;
	const int32 Wide = Rng.RandRange(4, 5);
	const int32 Deep = 4;
	const float Yard = 260.f;

	FFootprint F;
	F.PanelsX = bFacesX ? Deep : Wide;
	F.PanelsY = bFacesX ? Wide : Deep;
	F.Center = FrontCenter - Out * (Yard + Deep * PanelWidth * 0.5f);
	F.Center.Z = StreetZ();

	const FLinearColor Paint = Building(Rng.RandRange(0, 8));
	const FLinearColor RoofTint = Accent(Rng.RandRange(0, 7)) * 0.8f;
	const bool bTwoStorey = Rng.FRand() < 0.45f;
	const int32 DoorIndex = FacePanels(F, Facing) / 2;

	for (const EFace Face : { EFace::PosX, EFace::NegX, EFace::PosY, EFace::NegY })
	{
		const int32 N = FacePanels(F, Face);
		TArray<EPanel> Panels;
		for (int32 i = 0; i < N; ++i)
		{
			Panels.Add((i + (Face == Facing ? 1 : 0)) % 2 ? EPanel::Window : EPanel::Plain);
		}
		if (Face == Facing)
		{
			Panels[DoorIndex] = EPanel::Door;
		}
		BuildGroundFace(F, Face, Panels, Paint);
	}
	Place(TEXT("SM_Porch"), PanelTransform(F, Facing, DoorIndex, 0.f), RoofTint);

	const int32 Upper = bTwoStorey ? 1 : 0;
	// Upstairs is reached by stairs up the back wall, to a doorway in the corner panel.
	const EFace Back = Facing == EFace::PosX ? EFace::NegX : Facing == EFace::NegX ? EFace::PosX : Facing == EFace::PosY ? EFace::NegY : EFace::PosY;
	BuildUpperFloors(F, Upper, Paint, Rng, false, bTwoStorey ? int32(Back) : -1, 0, nullptr);
	BuildCorners(F, Upper, Paint);
	if (bTwoStorey)
	{
		BuildOutsideStairs(F, Back, 0, Path);
		BuildUpperStoreys(F, 1, true, GroundHeight + UpperHeight - 2.f, QuarterOf(F, PanelTransform(F, Back, 0, 0.f).GetLocation()), Rng);
	}

	// Gable roof with its ridge along the street, and a chimney.
	const float Eaves = GroundHeight + Upper * UpperHeight;
	const float Across = (bFacesX ? F.HalfX() : F.HalfY()) * 2.f + 80.f;
	const float AlongRidge = (bFacesX ? F.HalfY() : F.HalfX()) * 2.f + 50.f;
	const FRotator RoofRot(0.f, bFacesX ? 0.f : 90.f, 0.f);
	Place(TEXT("SM_GableRoof"), FTransform(RoofRot, F.Center + FVector(0.f, 0.f, Eaves), FVector(Across / 100.f, AlongRidge / 100.f, 2.3f)), RoofTint);
	AddBox(Ceiling, F.Center + FVector(0.f, 0.f, Eaves + 5.f), FVector(F.HalfX() * 2.f, F.HalfY() * 2.f, 10.f));
	Place(TEXT("SM_Chimney"), FTransform(F.Center + Along * (AlongRidge * 0.3f) - Out * 120.f + FVector(0.f, 0.f, Eaves + 60.f)));

	// Front garden: a path to the door, a picket fence with a gap, and the mailbox.
	const FVector Gate = FrontCenter;
	const FVector DoorStep = PanelTransform(F, Facing, DoorIndex, 0.f).GetLocation();
	FVector PathMid = (Gate + DoorStep) * 0.5f;
	PathMid.Z = StreetZ() + 3.f;
	AddBox(Path, PathMid, FVector(bFacesX ? Yard : 130.f, bFacesX ? 130.f : Yard, 6.f));
	const float LotHalf = bFacesX ? 520.f : 470.f;
	for (float Offset = 190.f; Offset + 100.f <= LotHalf + 60.f; Offset += 200.f)
	{
		for (const float Side : { -1.f, 1.f })
		{
			Place(TEXT("SM_Fence"), FTransform(Out.Rotation(), FVector(Gate.X, Gate.Y, StreetZ()) + Along * Side * Offset - Out * 20.f));
		}
	}
	Place(TEXT("SM_HouseMailbox"), FTransform(Out.Rotation(), FVector(Gate.X, Gate.Y, StreetZ()) + Along * 110.f - Out * 40.f), Accent(Rng.RandRange(0, 7)));
	AddTree(F.Center - Out * (Deep * PanelWidth * 0.5f + 180.f) + Along * Rng.FRandRange(-300.f, 300.f), Rng);

	FFTOBuilding& Room = AddRoom(F, Facing, DoorIndex, EFTOBuildingType::Home, FamilyNames[Rng.RandRange(0, UE_ARRAY_COUNT(FamilyNames) - 1)], GroundHeight);
	Furnish(Room, Rng);
}

// ------------------------------------------------------------------------------------------
// Industrial: warehouses
// ------------------------------------------------------------------------------------------

void AFTOCityGenerator::BuildIndustrialBlock(const FFTOCityBlock& Block, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const float H = Block.HalfSize;
	AddBox(Concrete, FVector(Block.Center.X, Block.Center.Y, StreetZ() + 2.f), FVector(H * 2.f, H * 2.f, 4.f));

	FFootprint F;
	F.PanelsX = Rng.RandRange(10, 12);
	F.PanelsY = Rng.RandRange(8, 10);
	F.Center = FVector(Block.Center.X, Block.Center.Y + Rng.FRandRange(-150.f, 150.f), StreetZ());
	const EFace DoorFace = static_cast<EFace>(Rng.RandRange(0, 3));
	BuildWarehouse(F, DoorFace, Rng);

	// Shipping containers and junk in the yard.
	const FVector Out = FaceNormal(DoorFace);
	const FVector Along(-Out.Y, Out.X, 0.f);
	for (int32 c = 0; c < 4; ++c)
	{
		const float Side = c % 2 ? 1.f : -1.f;
		const FVector Pos = FVector(Block.Center.X, Block.Center.Y, StreetZ()) - Out * (H - 180.f) + Along * Side * (H * 0.35f + (c / 2) * 700.f);
		AddBox(Accent(Rng.RandRange(0, 7)), Pos + FVector(0.f, 0.f, 130.f), FVector(600.f, 250.f, 260.f), Out.Rotation().Yaw + 90.f + Rng.FRandRange(-6.f, 6.f));
	}
	for (int32 b = 0; b < 5; ++b)
	{
		const FVector Spot = FVector(Block.Center.X, Block.Center.Y, StreetZ()) + Along * (H - 150.f + Rng.FRandRange(-100.f, 0.f)) + Out * Rng.FRandRange(-300.f, 300.f);
		Place(TEXT("SM_Barrel"), FTransform(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), Spot), Accent(b));
	}
}

void AFTOCityGenerator::BuildWarehouse(const FFootprint& F, EFace DoorFace, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const FLinearColor Paint = Rng.FRand() < 0.5f ? Warehouse : Building(Rng.RandRange(0, 8)) * 0.8f;
	const int32 N = FacePanels(F, DoorFace);
	const int32 Roller = N / 2 - 1;
	const int32 PersonDoor = FMath::Max(0, Roller - 2);

	for (const EFace Face : { EFace::PosX, EFace::NegX, EFace::PosY, EFace::NegY })
	{
		const int32 Count = FacePanels(F, Face);
		TArray<EPanel> Panels;
		for (int32 i = 0; i < Count; ++i)
		{
			Panels.Add(i % 3 == 1 ? EPanel::Window : EPanel::Plain);
		}
		if (Face == DoorFace)
		{
			Panels[Roller] = EPanel::Roller;
			Panels[Roller + 1] = EPanel::Skip;
			Panels[PersonDoor] = EPanel::Door;
		}
		BuildGroundFace(F, Face, Panels, Paint);
	}
	// One tall storey: clerestory windows above the ground panels, no floor between.
	BuildUpperFloors(F, 1, Paint, Rng, false);
	BuildCorners(F, 1, Paint);
	BuildRoof(F, GroundHeight + UpperHeight, Paint, Rng, false);
	const FTransform RoofSpot = PanelTransform(F, DoorFace, 1.f, GroundHeight + UpperHeight + 20.f);
	Place(TEXT("SM_Roof_AC"), FTransform(RoofSpot.Rotator(), RoofSpot.GetLocation() - FaceNormal(DoorFace) * 300.f));
	Place(TEXT("SM_Roof_Vent"), FTransform(RoofSpot.Rotator(), RoofSpot.GetLocation() - FaceNormal(DoorFace) * 800.f));

	FFTOBuilding& Room = AddRoom(F, DoorFace, PersonDoor, EFTOBuildingType::Warehouse, TEXT("WAREHOUSE"), GroundHeight + UpperHeight);
	Furnish(Room, Rng);
}

// ------------------------------------------------------------------------------------------
// Parks
// ------------------------------------------------------------------------------------------

void AFTOCityGenerator::BuildParkBlock(const FFTOCityBlock& Block, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const float H = Block.HalfSize;
	const FVector Base(Block.Center.X, Block.Center.Y, StreetZ());

	AddBox(Grass, FVector(Block.Center.X, Block.Center.Y, StreetZ() + 2.f), FVector(H * 2.f, H * 2.f, 4.f));
	AddCylinder(Water, Base + FVector(0.f, 0.f, 6.f), FVector(1400.f, 1400.f, 6.f));
	// Paths from each side to the pond.
	AddBox(Path, Base + FVector(-(H + 700.f) * 0.5f, 0.f, 5.f), FVector(H - 700.f, 220.f, 4.f));
	AddBox(Path, Base + FVector((H + 700.f) * 0.5f, 0.f, 5.f), FVector(H - 700.f, 220.f, 4.f));
	AddBox(Path, Base + FVector(0.f, -(H + 700.f) * 0.5f, 5.f), FVector(220.f, H - 700.f, 4.f));
	AddBox(Path, Base + FVector(0.f, (H + 700.f) * 0.5f, 5.f), FVector(220.f, H - 700.f, 4.f));

	const int32 Trees = Rng.RandRange(9, 14);
	for (int32 t = 0; t < Trees; ++t)
	{
		const float Angle = Rng.FRandRange(0.f, 2.f * PI);
		const float Radius = Rng.FRandRange(950.f, H - 200.f);
		const FVector Spot = Base + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
		if (FMath::Abs(Spot.X - Base.X) < 250.f || FMath::Abs(Spot.Y - Base.Y) < 250.f)
		{
			continue; // keep the paths clear
		}
		AddTree(Spot, Rng);
	}

	// Benches and bins round the pond, a few bushes.
	for (int32 b = 0; b < 4; ++b)
	{
		const float Angle = b * PI * 0.5f + PI * 0.25f;
		const FVector Dir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		Place(TEXT("SM_StreetBench"), FTransform((-Dir).Rotation(), Base + Dir * 900.f));
		Place(TEXT("SM_Bin"), FTransform(Base + Dir * 900.f + FVector(-Dir.Y, Dir.X, 0.f) * 130.f));
	}
	for (int32 b = 0; b < 5; ++b)
	{
		const float Angle = Rng.FRandRange(0.f, 2.f * PI);
		Place(TEXT("SM_Bush"), FTransform(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), Base + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Rng.FRandRange(800.f, H - 150.f)));
	}
}

// ------------------------------------------------------------------------------------------
// Precinct: front desk, briefing room, armory and holding cells
// ------------------------------------------------------------------------------------------

void AFTOCityGenerator::BuildPrecinct(const FFTOCityBlock& Block)
{
	using namespace FTOCityPalette;
	const float H = Block.HalfSize;
	const FVector Base(Block.Center.X, Block.Center.Y, StreetZ());

	// Parking lot out front (-X side), station house at the back.
	AddBox(Asphalt, Base + FVector(-H * 0.5f, 0.f, 2.f), FVector(H, H * 2.f, 4.f));
	for (int32 Bay = -3; Bay <= 3; ++Bay)
	{
		AddBox(RoadLine, Base + FVector(-H * 0.5f, Bay * 400.f, 5.f), FVector(600.f, 15.f, 2.f));
	}

	FFootprint F;
	F.PanelsX = 7;
	F.PanelsY = 13;
	F.Center = Base + FVector(H * 0.45f, 0.f, 0.f);
	const EFace Front = EFace::NegX;
	const int32 DoorIndex = 6;
	const FLinearColor Paint = PoliceBlue;

	for (const EFace Face : { EFace::PosX, EFace::NegX, EFace::PosY, EFace::NegY })
	{
		TArray<EPanel> Panels;
		for (int32 i = 0; i < FacePanels(F, Face); ++i)
		{
			Panels.Add(i % 2 ? EPanel::Window : EPanel::Plain);
		}
		if (Face == Front)
		{
			Panels[DoorIndex] = EPanel::Door;
			Panels[DoorIndex - 1] = EPanel::Plain;
			Panels[DoorIndex + 1] = EPanel::Plain;
		}
		BuildGroundFace(F, Face, Panels, Paint);
	}
	FRandomStream Rng(Seed ^ 0xC0DE);
	BuildUpperFloors(F, 1, Paint, Rng, false, int32(Front), 2, TEXT("SM_Wall_U_Plain"));
	BuildCorners(F, 1, Paint);
	const float RoofZ = GroundHeight + UpperHeight;
	PlanLift(F, Front, 2, 1);
	BuildUpperStoreys(F, 1, false, RoofZ - 12.f, QuarterOf(F, PanelTransform(F, Front, 2, 0.f).GetLocation()), Rng);
	BuildRoof(F, RoofZ, Paint, Rng, true);
	for (int32 i = 0; i < F.PanelsY; ++i)
	{
		Place(TEXT("SM_Cornice"), PanelTransform(F, Front, i, GroundHeight));
	}
	Place(TEXT("SM_Sign_Police"), PanelTransform(F, Front, DoorIndex, 0.f), PoliceBlue);
	// A big rooftop sign along the front edge.
	const FVector SignAt = PanelTransform(F, Front, DoorIndex, RoofZ + 150.f).GetLocation() + FVector(60.f, 0.f, 0.f);
	AddBox(PoliceBlue, SignAt, FVector(16.f, 1500.f, 230.f));
	AddBox(White, SignAt - FVector(0.f, 0.f, 150.f), FVector(20.f, 1520.f, 30.f));
	AddLabel(SignAt - FVector(10.f, 0.f, 0.f), 180.f, INVTEXT("PRECINCT"), FColor::White, 190.f);
	// The obligatory flashing-light roof ornaments.
	AddSphere(FLinearColor(0.1f, 0.3f, 1.f), F.Center + FVector(0.f, -600.f, RoofZ + 180.f), FVector(160.f));
	AddSphere(FLinearColor(1.f, 0.1f, 0.1f), F.Center + FVector(0.f, 600.f, RoofZ + 180.f), FVector(160.f));

	FFTOBuilding& B = AddRoom(F, Front, DoorIndex, EFTOBuildingType::Precinct, TEXT("PRECINCT"), GroundHeight);
	const FLinearColor Blue = PoliceBlue;
	const FLinearColor Plaster(0.92f, 0.90f, 0.84f);

	// Lobby (X 0..5.7) and three back rooms behind a cross wall with a door into each.
	const float Cross = 570.f;
	const float Doors[] = { -800.f, 0.f, 800.f };
	InteriorWall(B, true, Cross, B.YMin - WallThickness, B.YMax + WallThickness, Doors, Plaster);
	InteriorWall(B, false, -450.f, Cross, B.Depth + WallThickness, {}, Plaster);
	InteriorWall(B, false, 450.f, Cross, B.Depth + WallThickness, {}, Plaster);

	// Lobby: the front desk facing the door with the desk sergeant sat behind it, benches for whoever's waiting,
	// the wanted board, coffee (and donuts).
	PlaceInRoom(B, TEXT("SM_FrontDesk"), 320.f, 0.f, 180.f);
	PlaceInRoom(B, TEXT("SM_OfficeChair"), 430.f, 0.f, 180.f, Blue);
	B.WorkSpots.Add(SeatedSpot(B, 430.f, 0.f, 180.f, SitBack - 19.f, SeatHeight + 2.f));
	PlaceInRoom(B, TEXT("SM_Bench"), 45.f, -600.f, 0.f);
	PlaceInRoom(B, TEXT("SM_Bench"), 45.f, 600.f, 0.f);
	PlaceInRoom(B, TEXT("SM_WantedBoard"), 0.f, -350.f, 0.f);
	PlaceInRoom(B, TEXT("SM_CoffeeStation"), 150.f, B.YMin, 90.f);
	PlaceInRoom(B, TEXT("SM_WaterCooler"), 150.f, B.YMax, -90.f);
	PlaceInRoom(B, TEXT("SM_Plant"), 60.f, B.YMin + 60.f, 0.f);
	PlaceInRoom(B, TEXT("SM_Plant"), 60.f, B.YMax - 60.f, 0.f);
	PlaceInRoom(B, TEXT("SM_Doormat"), 50.f, 0.f, 0.f, Blue);
	for (const float Y : { -645.f, -555.f, 555.f, 645.f })
	{
		B.VisitSpots.Add(SeatedSpot(B, 45.f, Y, 0.f, SitBack - 20.f, SeatHeight + 2.f));
	}
	B.VisitSpots.Add(StandingSpot(B, 150.f, B.YMin + 110.f, -90.f, EFTOAnimAction::Talk)); // at the coffee

	// Briefing room (Y -12.7..-4.5): whiteboard and podium at the back, rows of chairs.
	PlaceInRoom(B, TEXT("SM_Whiteboard"), B.Depth, -860.f, 180.f);
	PlaceInRoom(B, TEXT("SM_Podium"), B.Depth - 150.f, -860.f, 180.f);
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 Col = 0; Col < 5; ++Col)
		{
			PlaceInRoom(B, TEXT("SM_BriefingChair"), Cross + 130.f + Row * 140.f, -1130.f + Col * 120.f, 0.f);
		}
	}

	// Armory (Y -4.5..4.5): racks on the back wall, lockers down the sides, ammo in the middle.
	for (const float Y : { -230.f, 0.f, 230.f })
	{
		PlaceInRoom(B, TEXT("SM_GunRack"), B.Depth, Y, 180.f);
		B.ArmorySpots.Add(RoomToWorld(B, B.Depth - 90.f, Y, 0.f));
	}
	for (const float X : { Cross + 180.f, Cross + 400.f })
	{
		PlaceInRoom(B, TEXT("SM_Lockers"), X, -440.f, 90.f);
		PlaceInRoom(B, TEXT("SM_Lockers"), X, 440.f, -90.f);
	}
	PlaceInRoom(B, TEXT("SM_AmmoCrate"), Cross + 330.f, -90.f, 0.f);
	PlaceInRoom(B, TEXT("SM_AmmoCrate"), Cross + 330.f, 90.f, 0.f);
	B.WorkSpots.Add(StandingSpot(B, B.Depth - 260.f, 330.f, 180.f, EFTOAnimAction::None)); // the quartermaster

	// Holding cells (Y 4.5..12.7): two cells behind bars at the back, with a bench and a toilet each.
	const float CellFront = 1020.f;
	const float CellMid = 860.f;
	PlaceInRoom(B, TEXT("SM_CellBars"), CellFront, 555.f, 180.f);
	PlaceInRoom(B, TEXT("SM_CellBarsDoor"), CellFront, 755.f, 180.f);
	PlaceInRoom(B, TEXT("SM_CellBarsDoor"), CellFront, 965.f, 180.f);
	PlaceInRoom(B, TEXT("SM_CellBars"), CellFront, 1165.f, 180.f);
	InteriorWall(B, false, CellMid, CellFront, B.Depth + WallThickness, {}, Plaster);
	for (const float Y : { 655.f, 1065.f })
	{
		PlaceInRoom(B, TEXT("SM_CellBench"), B.Depth, Y - 20.f, 180.f);
		PlaceInRoom(B, TEXT("SM_Toilet"), B.Depth, Y + (Y < CellMid ? 150.f : -150.f) + (Y < CellMid ? 20.f : 0.f), 180.f);
		// Two to a bench, backs to the wall, glaring out through the bars; in through the cell's door.
		const float Door = Y < CellMid ? 755.f : 965.f;
		for (const float Along : { -45.f, 45.f })
		{
			B.CellSpots.Add(RoomToWorld(B, B.Depth - SitBack, Y - 20.f + Along, 180.f, SeatHeight + 2.f - SitDrop));
			B.CellDoors.Add(RoomToWorld(B, CellFront, Door, 0.f));
		}
	}
}

// ------------------------------------------------------------------------------------------
// Bank: the banking hall, tellers, and the vault behind its great round door
// ------------------------------------------------------------------------------------------

void AFTOCityGenerator::BuildBank(const FFTOCityBlock& Block)
{
	using namespace FTOCityPalette;
	const FVector Base(Block.Center.X, Block.Center.Y, StreetZ());
	FRandomStream Rng(Seed ^ 0xBA4C);

	FFootprint F;
	F.PanelsX = 13;
	F.PanelsY = 13;
	F.Center = Base;
	const EFace Front = EFace::NegX;
	const int32 DoorIndex = 6;
	const FLinearColor Paint = BankStone;

	for (const EFace Face : { EFace::PosX, EFace::NegX, EFace::PosY, EFace::NegY })
	{
		TArray<EPanel> Panels;
		for (int32 i = 0; i < FacePanels(F, Face); ++i)
		{
			Panels.Add(i % 2 ? EPanel::Window : EPanel::Plain);
		}
		if (Face == Front)
		{
			Panels[DoorIndex] = EPanel::Door;
			Panels[DoorIndex - 1] = EPanel::Plain;
			Panels[DoorIndex + 1] = EPanel::Plain;
		}
		BuildGroundFace(F, Face, Panels, Paint);
	}
	BuildUpperFloors(F, 2, Paint, Rng, false, int32(Front), 2, TEXT("SM_Wall_U_Plain"));
	BuildCorners(F, 2, Paint);
	const float RoofZ = GroundHeight + 2.f * UpperHeight;
	PlanLift(F, Front, 2, 2);
	BuildUpperStoreys(F, 2, false, RoofZ - 12.f, QuarterOf(F, PanelTransform(F, Front, 2, 0.f).GetLocation()), Rng);
	BuildRoof(F, RoofZ, Paint, Rng, true);
	Place(TEXT("SM_Sign_Bank"), PanelTransform(F, Front, DoorIndex, 0.f), BankGold);

	// Grand columns and a gold frieze across the front.
	const float FrontX = F.Center.X - F.HalfX() - 160.f;
	for (int32 c = -3; c <= 3; ++c)
	{
		if (c == 0)
		{
			continue; // keep the door clear
		}
		AddCylinder(White, FVector(FrontX, F.Center.Y + c * 330.f, F.Center.Z + RoofZ * 0.5f), FVector(110.f, 110.f, RoofZ));
		AddBox(White, FVector(FrontX, F.Center.Y + c * 330.f, F.Center.Z + 15.f), FVector(150.f, 150.f, 30.f));
	}
	AddBox(BankGold, FVector(FrontX, F.Center.Y, F.Center.Z + RoofZ + 60.f), FVector(200.f, 2300.f, 120.f));
	AddLabel(FVector(FrontX - 110.f, F.Center.Y, F.Center.Z + RoofZ + 60.f), 180.f, INVTEXT("BANK"), FColor(80, 50, 10), 110.f);

	FFTOBuilding& B = AddRoom(F, Front, DoorIndex, EFTOBuildingType::Bank, TEXT("BANK"), GroundHeight);
	const FLinearColor Plaster(0.95f, 0.92f, 0.84f);

	// The vault wall across the back third, the great door in the middle and a staff door to one side.
	const float VaultLine = 1500.f;
	PlaceInRoom(B, TEXT("SM_VaultWall"), VaultLine - 40.f, 0.f, 180.f);
	const float StaffDoor[] = { -800.f };
	InteriorWall(B, true, VaultLine, B.YMin - WallThickness, -2.f * PanelWidth * 0.5f, StaffDoor, Plaster);
	InteriorWall(B, true, VaultLine, 2.f * PanelWidth * 0.5f, B.YMax + WallThickness, {}, Plaster);

	// Tellers facing the door with customers at their windows, a queue, benches and plants.
	for (const float Y : { -340.f, 340.f })
	{
		PlaceInRoom(B, TEXT("SM_TellerCounter"), VaultLine - 330.f, Y, 180.f);
		B.WorkSpots.Add(StandingSpot(B, VaultLine - 230.f, Y - 80.f, 180.f, EFTOAnimAction::Work));
		B.WorkSpots.Add(StandingSpot(B, VaultLine - 230.f, Y + 80.f, 180.f, EFTOAnimAction::Work));
		B.VisitSpots.Add(StandingSpot(B, VaultLine - 440.f, Y - 80.f, 0.f, EFTOAnimAction::Talk));
	}
	for (int32 k = 0; k < 6; ++k)
	{
		PlaceInRoom(B, TEXT("SM_QueuePost"), 650.f, -300.f + k * 117.f, 0.f);
		B.VisitSpots.Add(StandingSpot(B, 820.f - k * 60.f, 250.f, 0.f, EFTOAnimAction::None));
	}
	for (const float X : { 350.f, 750.f })
	{
		PlaceInRoom(B, TEXT("SM_Bench"), X, B.YMin + 30.f, 90.f);
		PlaceInRoom(B, TEXT("SM_Bench"), X, B.YMax - 30.f, -90.f);
		B.VisitSpots.Add(SeatedSpot(B, X, B.YMin + 30.f, 90.f, SitBack - 20.f, SeatHeight + 2.f));
		B.VisitSpots.Add(SeatedSpot(B, X, B.YMax - 30.f, -90.f, SitBack - 20.f, SeatHeight + 2.f));
	}
	// A hold-up in the hall, facing the tellers; a heist in the vault itself.
	B.CrimeSpots.Add(RoomToWorld(B, VaultLine - 520.f, 0.f, 0.f));
	B.CrimeSpots.Add(RoomToWorld(B, VaultLine + 250.f, 0.f, 0.f));
	PlaceInRoom(B, TEXT("SM_Plant"), 60.f, B.YMin + 60.f, 0.f);
	PlaceInRoom(B, TEXT("SM_Plant"), 60.f, B.YMax - 60.f, 0.f);
	PlaceInRoom(B, TEXT("SM_Doormat"), 50.f, 0.f, 0.f, BankGold);

	// The vault: shelves of money bags and gold bars.
	for (const float Y : { -700.f, -350.f, 0.f, 350.f, 700.f })
	{
		PlaceInRoom(B, TEXT("SM_VaultShelf"), B.Depth, Y, 180.f);
	}
	PlaceInRoom(B, TEXT("SM_VaultShelf"), VaultLine + 450.f, B.YMin, 90.f);
	PlaceInRoom(B, TEXT("SM_VaultShelf"), VaultLine + 450.f, B.YMax, -90.f);
}
