#include "Crime/FTOCrimeDirector.h"
#include "Core/FTOGameState.h"
#include "Crime/FTOCrimeCatalog.h"
#include "Crime/FTOCrimeSpawnPoint.h"
#include "Crime/FTOIncident.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "FTO.h"

UFTOCrimeDirector::UFTOCrimeDirector()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
	SetIsReplicatedByDefault(false);
	IncidentClass = AFTOIncident::StaticClass();
}

void UFTOCrimeDirector::BeginPlay()
{
	Super::BeginPlay();

	if (CatalogOverride)
	{
		Catalog = CatalogOverride;
	}
	else
	{
		Catalog = NewObject<UFTOCrimeCatalog>(this, TEXT("DefaultCrimeCatalog"));
		Catalog->PopulateDefaults();
	}
}

AFTOGameState* UFTOCrimeDirector::GetFTOGameState() const
{
	return GetWorld() ? GetWorld()->GetGameState<AFTOGameState>() : nullptr;
}

void UFTOCrimeDirector::BeginShift(int32 Seed)
{
	AFTOGameState* GS = GetFTOGameState();
	if (!GS)
	{
		UE_LOG(LogFTO, Error, TEXT("Crime director needs an AFTOGameState."));
		return;
	}

	Rng.Initialize(Seed);
	GS->ShiftSeed = Seed;

	const float Now = GetWorld()->GetTimeSeconds();
	GS->SetShiftTimes(Now + BriefingSeconds, Now + BriefingSeconds + ShiftLengthSeconds);
	GS->SetShiftPhase(EFTOShiftPhase::Briefing);
	NextSpawnTime = Now + BriefingSeconds + 3.f;
	bShiftStarted = true;

	RefreshSpawnPoints();
	UE_LOG(LogFTO, Log, TEXT("Shift started. Seed %d, %d crime spawn points."), Seed, SpawnPoints.Num());
}

void UFTOCrimeDirector::RefreshSpawnPoints()
{
	SpawnPoints.Reset();
	for (TActorIterator<AFTOCrimeSpawnPoint> It(GetWorld()); It; ++It)
	{
		SpawnPoints.Add(*It);
	}
}

int32 UFTOCrimeDirector::GetOfficerCount() const
{
	const AFTOGameState* GS = GetFTOGameState();
	return GS ? FMath::Max(1, GS->PlayerArray.Num()) : 1;
}

int32 UFTOCrimeDirector::CountActiveIncidents() const
{
	int32 Count = 0;
	if (const AFTOGameState* GS = GetFTOGameState())
	{
		for (const AFTOIncident* Incident : GS->GetIncidents())
		{
			if (Incident && Incident->IsActive()) { ++Count; }
		}
	}
	return Count;
}

void UFTOCrimeDirector::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AFTOGameState* GS = GetFTOGameState();
	if (!bShiftStarted || !GS)
	{
		return;
	}

	switch (GS->GetShiftPhase())
	{
	case EFTOShiftPhase::Briefing:
		if (GS->GetBriefingTimeRemaining() <= 0.f)
		{
			GS->SetShiftPhase(EFTOShiftPhase::OnDuty);
		}
		break;

	case EFTOShiftPhase::OnDuty:
		TickOnDuty(DeltaTime);
		break;

	default:
		break;
	}
}

void UFTOCrimeDirector::TickOnDuty(float DeltaTime)
{
	AFTOGameState* GS = GetFTOGameState();
	const float Now = GetWorld()->GetTimeSeconds();

	// 1. Pump chaos from every live incident, minus the city's natural calm.
	float ChaosDelta = -PassiveDecayPerSecond * DeltaTime;
	for (const AFTOIncident* Incident : GS->GetIncidents())
	{
		if (Incident)
		{
			ChaosDelta += Incident->GetChaosRate() * DeltaTime;
		}
	}
	GS->AddChaos(ChaosDelta);

	// 2. Win / lose.
	if (GS->GetChaos() >= AFTOGameState::MaxChaos)
	{
		GS->SetShiftPhase(EFTOShiftPhase::Overrun);
		UE_LOG(LogFTO, Log, TEXT("The city has fallen into chaos. Shift over."));
		return;
	}
	if (GS->GetShiftTimeRemaining() <= 0.f)
	{
		GS->SetShiftPhase(EFTOShiftPhase::Survived);
		UE_LOG(LogFTO, Log, TEXT("Shift survived! Resolved %d, failed %d."), GS->IncidentsResolved, GS->IncidentsFailed);
		return;
	}

	// 3. Spawn new trouble.
	const int32 Officers = GetOfficerCount();
	const int32 MaxActive = BaseMaxActiveIncidents + MaxActiveIncidentsPerOfficer * Officers;
	if (Now >= NextSpawnTime)
	{
		if (CountActiveIncidents() < MaxActive)
		{
			if (const FFTOCrimeTemplate* Template = PickTemplate(GS->GetChaos()))
			{
				FVector Location;
				if (PickLocation(Template->Id, Location))
				{
					SpawnFromTemplate(*Template, Location, false);
				}
			}
		}

		const float ChaosAlpha = GS->GetChaosAlpha();
		const float Pacing = OfficerPacing.IsValidIndex(Officers - 1) ? OfficerPacing[Officers - 1] : 1.f;
		const float Interval = FMath::Lerp(SpawnIntervalRange.X, SpawnIntervalRange.Y, ChaosAlpha) * Pacing;
		NextSpawnTime = Now + Interval * Rng.FRandRange(0.7f, 1.3f);
	}
}

const FFTOCrimeTemplate* UFTOCrimeDirector::PickTemplate(float Chaos)
{
	const AFTOGameState* GS = GetFTOGameState();

	auto IsEligible = [&](const FFTOCrimeTemplate& T)
	{
		if (T.MinChaos > Chaos || T.Weight <= 0.f)
		{
			return false;
		}
		if (T.bUnique && GS)
		{
			for (const AFTOIncident* Incident : GS->GetIncidents())
			{
				if (Incident && Incident->IsActive() && Incident->GetInfo().TemplateId == T.Id)
				{
					return false;
				}
			}
		}
		return true;
	};

	// Bigger crimes get more likely as the city gets rowdier.
	auto ScaledWeight = [Chaos](const FFTOCrimeTemplate& T)
	{
		const float TierBoost = 1.f + (static_cast<float>(T.Tier) * Chaos / 100.f);
		return T.Weight * TierBoost;
	};

	float Total = 0.f;
	for (const FFTOCrimeTemplate& T : Catalog->Templates)
	{
		if (IsEligible(T)) { Total += ScaledWeight(T); }
	}
	if (Total <= 0.f)
	{
		return nullptr;
	}

	float Pick = Rng.FRand() * Total;
	for (const FFTOCrimeTemplate& T : Catalog->Templates)
	{
		if (!IsEligible(T)) { continue; }
		Pick -= ScaledWeight(T);
		if (Pick <= 0.f)
		{
			return &T;
		}
	}
	return nullptr;
}

bool UFTOCrimeDirector::IsTooCloseToActiveIncident(const FVector& Location) const
{
	if (const AFTOGameState* GS = GetFTOGameState())
	{
		for (const AFTOIncident* Incident : GS->GetIncidents())
		{
			if (Incident && Incident->IsActive() && FVector::DistSquared2D(Incident->GetActorLocation(), Location) < FMath::Square(MinIncidentSpacing))
			{
				return true;
			}
		}
	}
	return false;
}

bool UFTOCrimeDirector::PickLocation(FName TemplateId, FVector& OutLocation)
{
	// Prefer authored / generated spawn points.
	TArray<AFTOCrimeSpawnPoint*> Candidates;
	for (AFTOCrimeSpawnPoint* Point : SpawnPoints)
	{
		if (Point && Point->Allows(TemplateId) && !IsTooCloseToActiveIncident(Point->GetActorLocation()))
		{
			Candidates.Add(Point);
		}
	}
	if (Candidates.Num() > 0)
	{
		OutLocation = Candidates[Rng.RandRange(0, Candidates.Num() - 1)]->GetActorLocation();
		return true;
	}

	// Otherwise anywhere reachable on the navmesh, or failing that, anywhere on the ground.
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		const FVector Guess(Rng.FRandRange(-FallbackRadius, FallbackRadius), Rng.FRandRange(-FallbackRadius, FallbackRadius), 0.f);

		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			FNavLocation NavLocation;
			if (Nav->ProjectPointToNavigation(Guess, NavLocation, FVector(500.f, 500.f, 2000.f)))
			{
				if (!IsTooCloseToActiveIncident(NavLocation.Location))
				{
					OutLocation = NavLocation.Location;
					return true;
				}
				continue;
			}
		}

		FHitResult Hit;
		const FVector Start = Guess + FVector(0.f, 0.f, 5000.f);
		const FVector End = Guess - FVector(0.f, 0.f, 5000.f);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility) && !IsTooCloseToActiveIncident(Hit.ImpactPoint))
		{
			OutLocation = Hit.ImpactPoint;
			return true;
		}
	}
	return false;
}

AFTOIncident* UFTOCrimeDirector::SpawnIncident(FName TemplateId, bool bForceReported)
{
	const FFTOCrimeTemplate* Template = Catalog ? Catalog->FindTemplate(TemplateId) : nullptr;
	FVector Location;
	if (!Template || !PickLocation(TemplateId, Location))
	{
		return nullptr;
	}
	return SpawnFromTemplate(*Template, Location, bForceReported);
}

AFTOIncident* UFTOCrimeDirector::SpawnFromTemplate(const FFTOCrimeTemplate& Template, const FVector& Location, bool bForceReported)
{
	AFTOGameState* GS = GetFTOGameState();
	if (!GS)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFTOIncident* Incident = GetWorld()->SpawnActor<AFTOIncident>(IncidentClass, Location, FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), Params);
	if (!Incident)
	{
		return nullptr;
	}

	const FFTOIncidentInfo Info = Catalog->RollIncident(Template, Rng);
	const bool bReported = bForceReported || Rng.FRand() < Template.ReportChance;
	const float Delay = bForceReported ? 0.f : Rng.FRandRange(Template.ReportDelay.X, Template.ReportDelay.Y);
	Incident->InitIncident(Info, bReported, Delay);

	Incident->OnResolved.AddUObject(this, &UFTOCrimeDirector::HandleResolved);
	Incident->OnFailed.AddUObject(this, &UFTOCrimeDirector::HandleFailed);
	Incident->OnReported.AddUObject(this, &UFTOCrimeDirector::HandleReported);
	Incident->OnDestroyed.AddDynamic(this, &UFTOCrimeDirector::HandleIncidentDestroyed);

	GS->RegisterIncident(Incident);

	UE_LOG(LogFTO, Log, TEXT("New incident: %s (%s) at %s%s"),
		*Info.Title.ToString(), *FTOCrime::TierName(Info.Tier).ToString(), *Location.ToCompactString(),
		bReported ? TEXT("") : TEXT(" [unreported]"));
	return Incident;
}

void UFTOCrimeDirector::HandleIncidentDestroyed(AActor* DestroyedActor)
{
	if (AFTOGameState* GS = GetFTOGameState())
	{
		GS->UnregisterIncident(Cast<AFTOIncident>(DestroyedActor));
	}
}

void UFTOCrimeDirector::HandleReported(AFTOIncident* Incident)
{
	AFTOGameState* GS = GetFTOGameState();
	if (GS && Incident && Incident->WasWitnessed())
	{
		++GS->IncidentsWitnessed;
	}
}

void UFTOCrimeDirector::HandleResolved(AFTOIncident* Incident)
{
	AFTOGameState* GS = GetFTOGameState();
	if (!GS || !Incident)
	{
		return;
	}

	const FFTOIncidentInfo& Info = Incident->GetInfo();
	const float Relief = Info.ChaosRelief * (Incident->WasWitnessed() ? WitnessBonus : 1.f);
	GS->AddChaos(-Relief);
	++GS->IncidentsResolved;
}

void UFTOCrimeDirector::HandleFailed(AFTOIncident* Incident)
{
	AFTOGameState* GS = GetFTOGameState();
	if (!GS || !Incident)
	{
		return;
	}

	const FFTOIncidentInfo Info = Incident->GetInfo();
	GS->AddChaos(Info.FailPenalty);
	++GS->IncidentsFailed;

	// Ignored problems grow into bigger problems.
	if (!Info.EscalatesTo.IsNone() && GS->GetShiftPhase() == EFTOShiftPhase::OnDuty)
	{
		if (const FFTOCrimeTemplate* Next = Catalog->FindTemplate(Info.EscalatesTo))
		{
			const FVector Offset(Rng.FRandRange(-300.f, 300.f), Rng.FRandRange(-300.f, 300.f), 0.f);
			SpawnFromTemplate(*Next, Incident->GetActorLocation() + Offset, true);
		}
	}
}
