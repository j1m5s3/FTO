#include "Crime/FTOCrimeCatalog.h"


namespace
{
	struct FTemplateBuilder
	{
		FFTOCrimeTemplate& Tpl;

		FTemplateBuilder& Flavor(std::initializer_list<const TCHAR*> Lines)
		{
			for (const TCHAR* Line : Lines) { Tpl.Flavor.Add(FText::FromString(Line)); }
			return *this;
		}
		FTemplateBuilder& Chaos(float PerSecond, float Relief, float Fail) { Tpl.ChaosPerSecond = PerSecond; Tpl.ChaosRelief = Relief; Tpl.FailPenalty = Fail; return *this; }
		FTemplateBuilder& Work(float Seconds, int32 Officers) { Tpl.ResolveSeconds = Seconds; Tpl.OfficersRequired = Officers; return *this; }
		FTemplateBuilder& Report(float Chance, float MinDelay, float MaxDelay) { Tpl.ReportChance = Chance; Tpl.ReportDelay = FVector2D(MinDelay, MaxDelay); return *this; }
		FTemplateBuilder& Escalate(float After, FName To = NAME_None) { Tpl.TimeToEscalate = After; Tpl.EscalatesTo = To; return *this; }
		FTemplateBuilder& Spawn(float Weight, float MinChaos = 0.f, bool bUnique = false) { Tpl.Weight = Weight; Tpl.MinChaos = MinChaos; Tpl.bUnique = bUnique; return *this; }
		FTemplateBuilder& NoArrest() { Tpl.bArrest = false; return *this; }
		FTemplateBuilder& Armed() { Tpl.bArmed = true; return *this; }
		/** Might run when the police come (EscapeChance), and leaves with the goods after DeedSeconds (0 = never). */
		FTemplateBuilder& Getaway(float EscapeChance, float DeedSeconds = 0.f) { Tpl.EscapeChance = EscapeChance; Tpl.DeedSeconds = DeedSeconds; return *this; }
		/** A classic cartoon crook in the striped jumper (robbers, burglars, heist crews), not someone in street clothes. */
		FTemplateBuilder& Crook() { Tpl.bStreetClothes = false; return *this; }
	};

	FTemplateBuilder Add(TArray<FFTOCrimeTemplate>& Out, FName Id, const TCHAR* Title, EFTOCrimeTier Tier)
	{
		FFTOCrimeTemplate& Tpl = Out.AddDefaulted_GetRef();
		Tpl.Id = Id;
		Tpl.Title = FText::FromString(Title);
		Tpl.Tier = Tier;
		return FTemplateBuilder{ Tpl };
	}
}

const FFTOCrimeTemplate* UFTOCrimeCatalog::FindTemplate(FName Id) const
{
	return Templates.FindByPredicate([Id](const FFTOCrimeTemplate& T) { return T.Id == Id; });
}

void UFTOCrimeCatalog::PopulateDefaults()
{
	Templates.Reset();
	Modifiers.Reset();

	using E = EFTOCrimeTier;

	// Response windows (Escalate) are generous: the city's big and a cruiser takes a while to cross it. Unattended
	// crimes still bleed chaos the whole time, so leaving them is never free.

	// ---- Petty: small day-to-day calls ------------------------------------------------
	Add(Templates, "CatInTree", TEXT("Cat Stuck in Tree"), E::Petty)
		.Flavor({ TEXT("Caller says the cat is 'judging everyone'."), TEXT("It's the same cat as yesterday.") })
		.Chaos(0.02f, 2.f, 1.f).Work(3.f, 1).Report(1.f, 2.f, 6.f).Escalate(240.f).Spawn(1.2f).NoArrest();

	Add(Templates, "LostTourist", TEXT("Lost Tourist"), E::Petty)
		.Flavor({ TEXT("Tourist is holding the map upside down."), TEXT("They're looking for 'the famous statue'. There is no statue.") })
		.Chaos(0.015f, 1.5f, 1.f).Work(2.f, 1).Report(0.3f, 1.f, 3.f).Escalate(210.f).Spawn(1.f).NoArrest();

	Add(Templates, "NoiseComplaint", TEXT("Noise Complaint"), E::Petty)
		.Flavor({ TEXT("Neighbour has been practising the bagpipes since 6am."), TEXT("Karaoke night got out of hand.") })
		.Chaos(0.025f, 2.f, 2.f).Work(3.f, 1).Report(1.f, 3.f, 8.f).Escalate(210.f, "BarFight").Spawn(1.2f).NoArrest();

	Add(Templates, "Jaywalking", TEXT("Jaywalking"), E::Petty)
		.Flavor({ TEXT("Pedestrian doing cartwheels across the intersection."), TEXT("Someone keeps crossing the road without looking. Back and forth. For fun.") })
		.Chaos(0.02f, 1.5f, 1.f).Work(2.f, 1).Report(0.f, 0.f, 0.f).Escalate(120.f).Spawn(1.f).NoArrest();

	Add(Templates, "Graffiti", TEXT("Graffiti in Progress"), E::Petty)
		.Flavor({ TEXT("Tagger is spelling their own name wrong."), TEXT("It's actually quite good. Still illegal.") })
		.Chaos(0.03f, 2.5f, 2.f).Work(3.f, 1).Report(0.5f, 5.f, 12.f).Escalate(180.f, "Vandalism").Spawn(1.f).Getaway(0.5f, 150.f);

	Add(Templates, "Shoplifting", TEXT("Shoplifting"), E::Petty)
		.Flavor({ TEXT("Suspect has 14 rotisserie chickens under their coat."), TEXT("Suspect is filling a sack with scratch cards.") })
		.Chaos(0.035f, 3.f, 2.f).Work(3.f, 1).Report(0.8f, 2.f, 6.f).Escalate(180.f, "ArmedRobbery").Spawn(1.2f).Getaway(0.45f, 90.f);

	// Never rolled: it's what a crook lying low in a building confesses to when an officer questions them.
	Add(Templates, "StolenGoods", TEXT("Possession of Stolen Goods"), E::Petty)
		.Flavor({ TEXT("Forty garden gnomes in the back room. 'They followed me home.'"), TEXT("Pockets full of other people's spoons.") })
		.Chaos(0.02f, 3.f, 1.f).Work(3.f, 1).Report(1.f, 0.f, 0.f).Escalate(240.f).Spawn(0.f);

	Add(Templates, "IllegalParking", TEXT("Illegal Parking"), E::Petty)
		.Flavor({ TEXT("Car parked on the roof of another car."), TEXT("Food truck blocking a fire hydrant again.") })
		.Chaos(0.02f, 1.5f, 1.f).Work(2.f, 1).Report(0.6f, 3.f, 10.f).Escalate(240.f).Spawn(0.8f).NoArrest();

	// ---- Minor: the daily grind --------------------------------------------------------
	Add(Templates, "DomesticDispute", TEXT("Domestic Dispute"), E::Minor)
		.Flavor({ TEXT("Argument about whose turn it was to do the dishes."), TEXT("Two roommates, one TV remote.") })
		.Chaos(0.06f, 5.f, 4.f).Work(5.f, 2).Report(0.9f, 2.f, 5.f).Escalate(180.f, "Standoff").Spawn(1.f).Getaway(0.15f);

	Add(Templates, "BarFight", TEXT("Bar Fight"), E::Minor)
		.Flavor({ TEXT("Dispute over trivia night answers."), TEXT("Someone said pineapple belongs on pizza.") })
		.Chaos(0.07f, 5.f, 4.f).Work(5.f, 2).Report(0.9f, 1.f, 4.f).Escalate(180.f, "Riot").Spawn(0.9f).Getaway(0.3f);

	Add(Templates, "Vandalism", TEXT("Vandalism"), E::Minor)
		.Flavor({ TEXT("Someone is kicking over every bin on the street."), TEXT("Suspect is 'redecorating' the street furniture.") })
		.Chaos(0.055f, 4.f, 3.f).Work(4.f, 1).Report(0.6f, 4.f, 10.f).Escalate(180.f).Spawn(0.8f).Getaway(0.5f, 120.f);

	Add(Templates, "Mugging", TEXT("Mugging"), E::Minor)
		.Flavor({ TEXT("Suspect demanding a pensioner's crossword. And their wallet."), TEXT("Victim says the mugger 'had very nice shoes'.") })
		.Chaos(0.07f, 5.f, 4.f).Work(4.f, 1).Report(0.8f, 1.f, 4.f).Escalate(180.f).Spawn(0.9f).Getaway(0.6f, 45.f);

	Add(Templates, "Speeding", TEXT("Reckless Driver"), E::Minor)
		.Flavor({ TEXT("Driver doing donuts in the mall car park."), TEXT("Grandma in a sports car. Again.") })
		.Chaos(0.06f, 4.f, 3.f).Work(3.f, 1).Report(0.2f, 1.f, 3.f).Escalate(150.f, "CarChase").Spawn(1.f).NoArrest();

	Add(Templates, "PettyTheft", TEXT("Pickpocket"), E::Minor)
		.Flavor({ TEXT("Suspect is going through a tourist's backpack. The tourist hasn't noticed."), TEXT("Someone's lifting wallets at the bus stop.") })
		.Chaos(0.05f, 4.f, 3.f).Work(4.f, 1).Report(0.7f, 2.f, 6.f).Escalate(180.f).Spawn(0.9f).Getaway(0.7f, 60.f);

	// ---- Major: needs a team -----------------------------------------------------------
	Add(Templates, "ArmedRobbery", TEXT("Armed Robbery"), E::Major)
		.Flavor({ TEXT("Corner store held up with a suspiciously banana-shaped weapon."), TEXT("Robber demanded 'all the scratch cards'.") })
		.Chaos(0.15f, 10.f, 8.f).Work(7.f, 2).Report(1.f, 1.f, 3.f).Escalate(180.f, "HostageSituation").Spawn(0.6f, 15.f).Armed().Crook().Getaway(0.4f, 75.f);

	Add(Templates, "CarChase", TEXT("Car Chase"), E::Major)
		.Flavor({ TEXT("Getaway vehicle is an ice cream truck. The music is still playing.") })
		.Chaos(0.18f, 10.f, 8.f).Work(6.f, 2).Report(1.f, 0.f, 2.f).Escalate(150.f).Spawn(0.5f, 20.f);

	Add(Templates, "Standoff", TEXT("Barricaded Suspect"), E::Major)
		.Flavor({ TEXT("Suspect has barricaded themselves inside a bouncy castle.") })
		.Chaos(0.15f, 12.f, 10.f).Work(8.f, 3).Report(1.f, 0.f, 2.f).Escalate(210.f, "HostageSituation").Spawn(0.3f, 25.f).Armed();

	Add(Templates, "Riot", TEXT("Street Brawl"), E::Major)
		.Flavor({ TEXT("Two rival barbershop quartets. Harmonies have turned violent."), TEXT("A disagreement about the best sandwich has become a street fight.") })
		.Chaos(0.2f, 12.f, 10.f).Work(8.f, 3).Report(1.f, 0.f, 1.f).Escalate(180.f).Spawn(0.3f, 30.f).Getaway(0.3f);

	Add(Templates, "Burglary", TEXT("Burglary"), E::Major)
		.Flavor({ TEXT("Burglar is only stealing left shoes."), TEXT("Neighbour reports someone 'tiptoeing very loudly'.") })
		.Chaos(0.12f, 8.f, 6.f).Work(6.f, 2).Report(0.6f, 5.f, 15.f).Escalate(210.f).Spawn(0.6f, 10.f).Crook().Getaway(0.5f, 100.f);

	// ---- Critical: whole-team set pieces -----------------------------------------------
	Add(Templates, "BankHeist", TEXT("Bank Heist"), E::Critical)
		.Flavor({ TEXT("Crew in matching clown masks. One is a real clown."), TEXT("Vault drill is plugged into the bank's own extension cord.") })
		.Chaos(0.35f, 25.f, 20.f).Work(12.f, 4).Report(1.f, 0.f, 1.f).Escalate(300.f).Spawn(0.25f, 40.f, true).Armed().Crook();

	Add(Templates, "HostageSituation", TEXT("Hostage Situation"), E::Critical)
		.Flavor({ TEXT("Hostage is a very calm goldfish. Demands include a bigger bowl.") })
		.Chaos(0.35f, 25.f, 20.f).Work(12.f, 4).Report(1.f, 0.f, 1.f).Escalate(300.f).Spawn(0.15f, 50.f, true).Armed().Crook();

	Add(Templates, "TerrorPlot", TEXT("Evil Masterplan"), E::Critical)
		.Flavor({ TEXT("A villain is threatening to release 10,000 bees at City Hall."), TEXT("Suspicious device ticking downtown. It might be a very loud clock.") })
		.Chaos(0.45f, 35.f, 30.f).Work(15.f, 4).Report(1.f, 0.f, 1.f).Escalate(300.f).Spawn(0.1f, 60.f, true).Crook();

	// ---- Modifiers ---------------------------------------------------------------------
	auto AddMod = [this](FName Id, const TCHAR* Prefix, const TCHAR* Note, float Chaos, float Resolve, int32 Extra, EFTOCrimeTier MinTier, float Weight)
	{
		FFTOCrimeModifier& M = Modifiers.AddDefaulted_GetRef();
		M.Id = Id;
		M.Prefix = FText::FromString(Prefix);
		M.Note = FText::FromString(Note);
		M.ChaosMultiplier = Chaos;
		M.ResolveMultiplier = Resolve;
		M.ExtraOfficers = Extra;
		M.MinTier = MinTier;
		M.Weight = Weight;
	};

	AddMod("Armed",    TEXT("Armed"),    TEXT("Suspect may be armed (probably with a baguette)."), 1.5f, 1.3f, 1, E::Minor, 1.f);
	AddMod("Drunk",    TEXT("Drunk"),    TEXT("Suspect keeps trying to hug the officers."),         1.1f, 1.2f, 0, E::Petty, 1.f);
	AddMod("Repeat",   TEXT("Repeat"),   TEXT("It's Gary. It's always Gary."),                      1.2f, 0.8f, 0, E::Petty, 1.f);
	AddMod("Costumed", TEXT(""),         TEXT("Suspect is dressed as a hot dog."),                  1.0f, 1.0f, 0, E::Petty, 1.f);
	AddMod("Livestream", TEXT("Viral"),  TEXT("Someone is livestreaming this. Chaos spreads faster."), 1.8f, 1.0f, 0, E::Petty, 0.6f);
	AddMod("Fleeing",  TEXT("Fleeing"),  TEXT("Suspect is on the move."),                           1.3f, 1.5f, 0, E::Minor, 0.8f);
}

FFTOIncidentInfo UFTOCrimeCatalog::RollIncident(const FFTOCrimeTemplate& Template, FRandomStream& Rng) const
{
	FFTOIncidentInfo Info;
	Info.TemplateId = Template.Id;
	Info.Tier = Template.Tier;
	Info.OfficersRequired = Template.OfficersRequired;
	Info.ResolveSeconds = Template.ResolveSeconds;
	Info.ChaosPerSecond = Template.ChaosPerSecond;
	Info.ChaosRelief = Template.ChaosRelief;
	Info.FailPenalty = Template.FailPenalty;
	Info.TimeToEscalate = Template.TimeToEscalate;
	Info.EscalatesTo = Template.EscalatesTo;
	Info.bArrest = Template.bArrest;
	Info.bArmed = Template.bArmed;
	Info.EscapeChance = Template.bArrest ? Template.EscapeChance : 0.f;
	Info.DeedSeconds = Template.bArrest ? Template.DeedSeconds : 0.f;
	Info.bStreetClothes = Template.bStreetClothes;

	FString Description = Template.Flavor.Num() > 0
		? Template.Flavor[Rng.RandRange(0, Template.Flavor.Num() - 1)].ToString()
		: FString();
	FText Title = Template.Title;

	if (Modifiers.Num() > 0 && Rng.FRand() < ModifierChance)
	{
		float TotalWeight = 0.f;
		for (const FFTOCrimeModifier& M : Modifiers)
		{
			if (M.MinTier <= Template.Tier) { TotalWeight += M.Weight; }
		}

		float Pick = Rng.FRand() * TotalWeight;
		for (const FFTOCrimeModifier& M : Modifiers)
		{
			if (M.MinTier > Template.Tier) { continue; }
			Pick -= M.Weight;
			if (Pick <= 0.f)
			{
				if (!M.Prefix.IsEmpty())
				{
					Title = FText::Format(INVTEXT("{0} {1}"), M.Prefix, Template.Title);
				}
				Description = FString::Printf(TEXT("%s %s"), *Description, *M.Note.ToString()).TrimStartAndEnd();
				Info.ChaosPerSecond *= M.ChaosMultiplier;
				Info.ChaosRelief *= M.ChaosMultiplier;
				Info.ResolveSeconds *= M.ResolveMultiplier;
				Info.OfficersRequired = FMath::Clamp(Info.OfficersRequired + M.ExtraOfficers, 1, 4);
				Info.bArmed |= M.Id == TEXT("Armed") && Info.bArrest;
				Info.Twist = M.Id;
				// On the move: they'll run from the police for sure.
				Info.EscapeChance = M.Id == TEXT("Fleeing") ? 1.f : Info.EscapeChance;
				break;
			}
		}
	}

	Info.Title = Title;
	Info.Description = FText::FromString(Description);
	return Info;
}

namespace FTOCrime
{
	FLinearColor TierColor(EFTOCrimeTier Tier)
	{
		switch (Tier)
		{
		case EFTOCrimeTier::Petty:    return FLinearColor(0.3f, 0.8f, 1.0f);
		case EFTOCrimeTier::Minor:    return FLinearColor(1.0f, 0.85f, 0.2f);
		case EFTOCrimeTier::Major:    return FLinearColor(1.0f, 0.45f, 0.1f);
		case EFTOCrimeTier::Critical: return FLinearColor(1.0f, 0.1f, 0.3f);
		}
		return FLinearColor::White;
	}

	FText TierName(EFTOCrimeTier Tier)
	{
		switch (Tier)
		{
		case EFTOCrimeTier::Petty:    return INVTEXT("Petty");
		case EFTOCrimeTier::Minor:    return INVTEXT("Minor");
		case EFTOCrimeTier::Major:    return INVTEXT("Major");
		case EFTOCrimeTier::Critical: return INVTEXT("CRITICAL");
		}
		return FText::GetEmpty();
	}
}

