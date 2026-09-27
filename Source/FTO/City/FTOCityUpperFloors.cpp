// Upper floors for AFTOCityGenerator: the storeys above the ground-floor rooms (floors, ceilings, lights and a few
// rooms' worth of furniture), a lift for every building with floors to reach (street doors outside, a stop on every
// floor), and outside stairs up to the bedroom of a two-storey house.
#include "City/FTOCityGenerator.h"
#include "City/FTOCityPalette.h"

using namespace FTOKit;

namespace
{
	/** How the furniture on a floor is laid out: a quarter at a time. */
	enum class EQuarter : uint8 { Office, Lounge, Dining, Study };
}

float AFTOCityGenerator::StoreyFloorZ(int32 Storey)
{
	// The first floor stands on the ground floor's ceiling; each floor above, one storey up.
	return GroundHeight + (Storey - 1) * UpperHeight;
}

void AFTOCityGenerator::BuildUpperStoreys(const FFootprint& F, int32 Floors, bool bHomes, float CeilingTopZ, int32 SkipQuarter, FRandomStream& Rng)
{
	using namespace FTOCityPalette;
	const float IX = F.HalfX() - WallThickness;
	const float IY = F.HalfY() - WallThickness;
	for (int32 Storey = 1; Storey <= Floors; ++Storey)
	{
		const float Z = StoreyFloorZ(Storey);
		const bool bHomeFloor = bHomes || Storey > 2;
		// The slab (the ground floor's ceiling already is one), with the floor covering on top.
		if (Storey > 1)
		{
			AddBox(Ceiling, F.Center + FVector(0.f, 0.f, Z - 12.f), FVector(IX * 2.f + 4.f, IY * 2.f + 4.f, 20.f), 0.f, true);
		}
		AddBox(bHomeFloor ? FloorWood : FloorCarpet, F.Center + FVector(0.f, 0.f, Z - 1.f), FVector(IX * 2.f, IY * 2.f, 2.f), 0.f, true);

		// Lights under the ceiling (the next slab, or the roof).
		const float CeilingZ = Storey < Floors ? StoreyFloorZ(Storey + 1) - 22.f : CeilingTopZ;
		for (const FVector2D& Q : { FVector2D(0.5f, 0.5f), FVector2D(-0.5f, 0.5f), FVector2D(-0.5f, -0.5f), FVector2D(0.5f, -0.5f) })
		{
			Place(TEXT("SM_CeilingLight"), FTransform(F.Center + FVector(Q.X * IX, Q.Y * IY, CeilingZ)), White, true);
		}

		// A few rooms' worth of furniture, a quarter of the floor each (the lift's quarter kept clear).
		const FVector2D Quarters[] = { FVector2D(0.5f, 0.5f), FVector2D(-0.5f, 0.5f), FVector2D(-0.5f, -0.5f), FVector2D(0.5f, -0.5f) };
		for (int32 q = 0; q < 4; ++q)
		{
			if (q == SkipQuarter)
			{
				continue;
			}
			const FVector Mid = F.Center + FVector(Quarters[q].X * IX, Quarters[q].Y * IY, Z);
			const float Yaw = Rng.RandRange(0, 3) * 90.f;
			const FRotator Facing(0.f, Yaw, 0.f);
			auto Put = [&](const TCHAR* Piece, float X, float Y, float TurnYaw, const FLinearColor& Tint = White)
			{
				Place(Piece, FTransform(FRotator(0.f, Yaw + TurnYaw, 0.f), Mid + Facing.RotateVector(FVector(X, Y, 0.f))), Tint, true);
			};
			const EQuarter Kind = bHomeFloor ? static_cast<EQuarter>(1 + (q + Storey) % 3) : EQuarter::Office;
			switch (Kind)
			{
			case EQuarter::Office:
				Put(TEXT("SM_Desk"), 0.f, -60.f, 0.f);
				Put(TEXT("SM_OfficeChair"), -70.f, -60.f, 0.f, Accent(Rng.RandRange(0, 7)));
				Put(TEXT("SM_Desk"), 0.f, 90.f, 0.f);
				Put(TEXT("SM_OfficeChair"), -70.f, 90.f, 0.f, Accent(Rng.RandRange(0, 7)));
				Put(TEXT("SM_FilingCabinet"), 120.f, -150.f, 180.f);
				Put(TEXT("SM_Plant"), 120.f, 170.f, 0.f);
				break;
			case EQuarter::Lounge:
				Put(TEXT("SM_Rug"), 0.f, 0.f, 0.f, Accent(Rng.RandRange(0, 7)));
				Put(TEXT("SM_Sofa"), -90.f, 0.f, 0.f, Accent(Rng.RandRange(0, 7)));
				Put(TEXT("SM_CoffeeTable"), 10.f, 0.f, 0.f);
				Put(TEXT("SM_TVStand"), 120.f, 0.f, 180.f);
				Put(TEXT("SM_FloorLamp"), -100.f, 130.f, 0.f);
				break;
			case EQuarter::Dining:
				Put(TEXT("SM_DiningTable"), 0.f, 0.f, 0.f);
				Put(TEXT("SM_Chair"), -80.f, 0.f, 0.f);
				Put(TEXT("SM_Chair"), 80.f, 0.f, 180.f);
				Put(TEXT("SM_Fridge"), 130.f, 150.f, 180.f);
				break;
			default:
				Put(TEXT("SM_Bookshelf"), 130.f, 0.f, 180.f);
				Put(TEXT("SM_Armchair"), -40.f, 60.f, 0.f, Accent(Rng.RandRange(0, 7)));
				Put(TEXT("SM_Plant"), -120.f, -140.f, 0.f);
				break;
			}
		}
	}
}

int32 AFTOCityGenerator::QuarterOf(const FFootprint& F, const FVector& Where)
{
	const FVector Local = Where - F.Center;
	if (Local.X >= 0.f)
	{
		return Local.Y >= 0.f ? 0 : 3;
	}
	return Local.Y >= 0.f ? 1 : 2;
}

void AFTOCityGenerator::PlanLift(const FFootprint& F, EFace Face, int32 Index, int32 Floors)
{
	// Street doors on the outside of the wall; a stop on the inside of the same wall on every floor above.
	FLiftPlan& Plan = LiftPlans.AddDefaulted_GetRef();
	const FVector Out = FaceNormal(Face);
	const FVector Street = PanelTransform(F, Face, Index, 0.f).GetLocation();
	Plan.Stops.Add(FTransform(Out.Rotation(), Street + Out * 2.f));
	for (int32 Storey = 1; Storey <= Floors; ++Storey)
	{
		Plan.Stops.Add(FTransform((-Out).Rotation(), Street - Out * (WallThickness + 2.f) + FVector(0.f, 0.f, StoreyFloorZ(Storey))));
	}
}

void AFTOCityGenerator::BuildOutsideStairs(const FFootprint& F, EFace Face, int32 Index, const FLinearColor& Tint)
{
	using namespace FTOCityPalette;
	// A landing at the upstairs doorway (a gap left in the wall), and a flight down along the wall to the garden.
	const FVector Out = FaceNormal(Face);
	const FTransform Opening = PanelTransform(F, Face, Index, 0.f);
	const FTransform Next = PanelTransform(F, Face, Index + 1, 0.f);
	const FVector Along = (Next.GetLocation() - Opening.GetLocation()).GetSafeNormal();
	const float Top = GroundHeight;
	const float Wide = 120.f;
	const FVector Landing = Opening.GetLocation() + Out * (Wide * 0.5f + 2.f);
	AddBox(Tint, Landing + FVector(0.f, 0.f, Top - 10.f), FVector(Out.X != 0.f ? Wide : PanelWidth, Out.Y != 0.f ? Wide : PanelWidth, 20.f));

	const int32 Steps = 20;
	const float Rise = Top / Steps;
	const float Run = 28.f;
	const FVector FlightStart = Landing + Along * (PanelWidth * 0.5f);
	for (int32 s = 0; s < Steps; ++s)
	{
		// Step s from the top: each a solid block down to the ground, so there's nothing to fall through.
		const float StepTop = Top - Rise * (s + 1);
		if (StepTop <= 0.f)
		{
			break;
		}
		const FVector At = FlightStart + Along * (Run * (s + 0.5f));
		const FVector Size = Out.X != 0.f ? FVector(Wide, Run, StepTop) : FVector(Run, Wide, StepTop);
		AddBox(Tint, At + FVector(0.f, 0.f, StepTop * 0.5f), Size);
	}
}
