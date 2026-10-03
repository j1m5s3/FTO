#include "Core/FTOCareer.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "FTO.h"

namespace FTOCareer
{
	TConstArrayView<FItem> Outfits()
	{
		static const FItem Items[] =
		{
			{ TEXT("Classic"),  TEXT("Classic Blue"),   0,    TEXT("Your badge colour.") },
			{ TEXT("HiVis"),    TEXT("Hi-Vis"),         800,  TEXT("Visible from space.") },
			{ TEXT("Tactical"), TEXT("Tactical Black"), 2000, TEXT("Very serious. Very dark.") },
			{ TEXT("Hawaiian"), TEXT("Hawaiian Shirt"), 3500, TEXT("Off duty energy, on duty.") },
			{ TEXT("HotDog"),   TEXT("Hot Dog Suit"),   6000, TEXT("Undercover. Nobody will suspect a thing.") },
			{ TEXT("Gold"),     TEXT("Gold Braid"),     9000, TEXT("For the commissioner's favourite.") },
		};
		return Items;
	}

	TConstArrayView<FItem> Liveries()
	{
		static const FItem Items[] =
		{
			{ TEXT("Standard"),    TEXT("Standard Cruiser"),  0,    TEXT("Badge-colour stripes. Reliable.") },
			{ TEXT("Interceptor"), TEXT("Interceptor"),       1500, TEXT("Black and fast: +12% top speed.") },
			{ TEXT("RiotWagon"),   TEXT("Riot Wagon"),        3000, TEXT("Armoured: 1.6x tougher, 8% slower.") },
			{ TEXT("IceCream"),    TEXT("Ice Cream Patrol"),  5000, TEXT("Nobody suspects the ice cream van.") },
		};
		return Items;
	}

	TConstArrayView<FItem> Upgrades()
	{
		static const FItem Items[] =
		{
			{ TEXT("Coffee"),      TEXT("Coffee Machine"),     600,  TEXT("Everyone 12% quicker on their feet.") },
			{ TEXT("Radios"),      TEXT("Better Radios"),      800,  TEXT("Calls come in twice as fast; spot crimes from further.") },
			{ TEXT("MotorPool"),   TEXT("Motor Pool Mechanic"), 900, TEXT("Cruisers 1.5x tougher.") },
			{ TEXT("Cells"),       TEXT("Bigger Holding Cells"), 1000, TEXT("Booking a suspect calms the city 50% more.") },
			{ TEXT("BodyArmour"),  TEXT("Body Armour"),        1200, TEXT("Half the shots that would put you down don't.") },
		};
		return Items;
	}

	const FItem* Find(TConstArrayView<FItem> Items, FName Id)
	{
		for (const FItem& Item : Items)
		{
			if (Item.Id == Id)
			{
				return &Item;
			}
		}
		return nullptr;
	}

	FString RankFor(int32 Earned)
	{
		if (Earned >= 16000) return TEXT("Commissioner");
		if (Earned >= 10000) return TEXT("Captain");
		if (Earned >= 6000)  return TEXT("Lieutenant");
		if (Earned >= 3000)  return TEXT("Sergeant");
		if (Earned >= 1000)  return TEXT("Officer");
		return TEXT("Rookie");
	}

	int32 LevelFor(const FFTOCareerState& State)
	{
		// (Every second shift survived: a gentle climb, so a new squad joining an old precinct isn't thrown in at the deep end.)
		return FMath::Clamp(State.ShiftsSurvived / 2, 0, 10);
	}

	FFTOCareerState Load(const FString& Slot)
	{
		if (UGameplayStatics::DoesSaveGameExist(Slot, 0))
		{
			if (const UFTOCareerSave* Saved = Cast<UFTOCareerSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0)))
			{
				return Saved->State;
			}
		}
		return FFTOCareerState();
	}

	void Save(const FString& Slot, const FFTOCareerState& State)
	{
		UFTOCareerSave* Saved = Cast<UFTOCareerSave>(UGameplayStatics::CreateSaveGameObject(UFTOCareerSave::StaticClass()));
		if (Saved)
		{
			Saved->State = State;
			if (!UGameplayStatics::SaveGameToSlot(Saved, Slot, 0))
			{
				UE_LOG(LogFTO, Warning, TEXT("Career: couldn't save to slot %s."), *Slot);
			}
		}
	}

	FString SlotName()
	{
		// (The smoke test keeps a career of its own, so it never touches the real one.)
		FString Slot;
		if (FParse::Value(FCommandLine::Get(), TEXT("FTOCareerSlot="), Slot) && !Slot.IsEmpty())
		{
			return Slot;
		}
		return FParse::Param(FCommandLine::Get(), TEXT("FTOSmokeTest")) ? FString(TEXT("FTOSmokeCareer")) : FString(TEXT("FTOCareer"));
	}

	FName& RememberedOutfit()
	{
		static FName Outfit = TEXT("Classic");
		return Outfit;
	}

	bool OutfitColor(FName Outfit, FLinearColor& OutColor)
	{
		if (Outfit == TEXT("HiVis"))    { OutColor = FLinearColor(0.95f, 0.85f, 0.05f); return true; }
		if (Outfit == TEXT("Tactical")) { OutColor = FLinearColor(0.04f, 0.045f, 0.05f); return true; }
		if (Outfit == TEXT("Hawaiian")) { OutColor = FLinearColor(1.f, 0.25f, 0.55f); return true; }
		if (Outfit == TEXT("HotDog"))   { OutColor = FLinearColor(0.9f, 0.7f, 0.4f); return true; }
		if (Outfit == TEXT("Gold"))     { OutColor = FLinearColor(0.85f, 0.65f, 0.12f); return true; }
		return false; // the badge colour
	}

	void LiveryStats(FName Livery, FLinearColor& OutPaint, bool& bOutRepaint, float& OutSpeed, float& OutToughness)
	{
		bOutRepaint = true;
		OutSpeed = 1.f;
		OutToughness = 1.f;
		if (Livery == TEXT("Interceptor")) { OutPaint = FLinearColor(0.03f, 0.03f, 0.035f); OutSpeed = 1.12f; return; }
		if (Livery == TEXT("RiotWagon"))   { OutPaint = FLinearColor(0.12f, 0.16f, 0.22f); OutSpeed = 0.92f; OutToughness = 1.6f; return; }
		if (Livery == TEXT("IceCream"))    { OutPaint = FLinearColor(1.f, 0.55f, 0.75f); return; }
		bOutRepaint = false; // standard: badge-colour stripes
	}
}
