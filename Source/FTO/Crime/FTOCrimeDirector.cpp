#include "Crime/FTOCrimeDirector.h"
#include "Physics/FTODestruction.h"
#include "Core/FTOGameState.h"
#include "Crime/FTOCrimeCatalog.h"
#include "Crime/FTOCrimeSpawnPoint.h"
#include "Crime/FTOIncident.h"
#include "Crime/FTOArrestee.h"
#include "Crime/FTOPerp.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOPlayerController.h"
#include "Core/FTOPlayerState.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "Scoring/FTOScoring.h"
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
	EnsureCatalog();
}

void UFTOCrimeDirector::EnsureCatalog()
{
	if (Catalog)
	{
		return;
	}

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

	EnsureCatalog();
	Rng.Initialize(Seed);
	GS->ShiftSeed = Seed;

	const float Now = GetWorld()->GetTimeSeconds();
	GS->SetShiftTimes(Now + BriefingSeconds, Now + BriefingSeconds + ShiftLengthSeconds);
	GS->SetShiftPhase(EFTOShiftPhase::Briefing);
	NextSpawnTime = Now + BriefingSeconds + FirstCrimeDelay;
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

	case EFTOShiftPhase::OvertimeVote:
		TickOvertimeVote();
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
		// Made it! The city holds its breath while the squad decides: overtime, or clock off.
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS))
			{
				Officer->SetShiftVote(EFTOShiftVote::None);
			}
		}
		GS->BeginOvertimeVote(OvertimeVoteSeconds, OvertimeSeconds);
		UE_LOG(LogFTO, Log, TEXT("Shift clock ran out (resolved %d, failed %d): overtime vote."), GS->IncidentsResolved, GS->IncidentsFailed);
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
				FTransform Where;
				int32 Building = INDEX_NONE;
				if (PickLocation(Template->Id, Where, Building))
				{
					SpawnFromTemplate(*Template, Where, Building, false);
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

TArray<FVector> UFTOCrimeDirector::GetOfficerLocations() const
{
	TArray<FVector> Out;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr)
		{
			Out.Add(Pawn->GetActorLocation());
		}
	}
	return Out;
}

bool UFTOCrimeDirector::PickLocation(FName TemplateId, FTransform& OutWhere, int32& OutBuilding)
{
	OutBuilding = INDEX_NONE;

	// Prefer authored / generated spawn points (indoors, the perp stands facing their victim).
	TArray<AFTOCrimeSpawnPoint*> Candidates;
	// (Nothing happens in a building that's come down.)
	const AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld());
	for (AFTOCrimeSpawnPoint* Point : SpawnPoints)
	{
		if (Point && Point->Allows(TemplateId) && !IsTooCloseToActiveIncident(Point->GetActorLocation()) &&
			!(Wreckage && Wreckage->IsBuildingDown(Point->BuildingIndex)))
		{
			Candidates.Add(Point);
		}
	}
	// Usually somewhere near one of the officers (each in turn), so nobody spends the shift driving between calls: a
	// short run away first, a bit further if there's nothing that close.
	const TArray<FVector> Officers = GetOfficerLocations();
	if (Candidates.Num() > 0 && Officers.Num() > 0 && Rng.FRand() < NearOfficerChance)
	{
		const FVector Focus = Officers[NextOfficerFocus++ % Officers.Num()];
		for (const float Stretch : { 1.f, 2.f })
		{
			TArray<AFTOCrimeSpawnPoint*> Near = Candidates.FilterByPredicate([&](const AFTOCrimeSpawnPoint* Point)
			{
				const float Dist = FVector::Dist2D(Point->GetActorLocation(), Focus);
				return Dist >= NearOfficerRange.X / Stretch && Dist <= NearOfficerRange.Y * Stretch;
			});
			if (Near.Num() > 0)
			{
				Candidates = MoveTemp(Near);
				break;
			}
		}
	}
	if (Candidates.Num() > 0)
	{
		const AFTOCrimeSpawnPoint* Point = Candidates[Rng.RandRange(0, Candidates.Num() - 1)];
		OutWhere = FTransform(Point->IsIndoors() ? Point->GetActorRotation() : FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), Point->GetActorLocation());
		OutBuilding = Point->BuildingIndex;
		return true;
	}

	// Otherwise anywhere reachable on the navmesh, or failing that, anywhere on the ground.
	const FRotator AnyWay(0.f, Rng.FRandRange(0.f, 360.f), 0.f);
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
					OutWhere = FTransform(AnyWay, NavLocation.Location);
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
			OutWhere = FTransform(AnyWay, Hit.ImpactPoint);
			return true;
		}
	}
	return false;
}

AFTOIncident* UFTOCrimeDirector::SpawnIncident(FName TemplateId, bool bForceReported)
{
	const FFTOCrimeTemplate* Template = Catalog ? Catalog->FindTemplate(TemplateId) : nullptr;
	FTransform Where;
	int32 Building = INDEX_NONE;
	if (!Template || !PickLocation(TemplateId, Where, Building))
	{
		return nullptr;
	}
	return SpawnFromTemplate(*Template, Where, Building, bForceReported);
}

AFTOIncident* UFTOCrimeDirector::SpawnFromTemplate(const FFTOCrimeTemplate& Template, const FTransform& Where, int32 BuildingIndex, bool bForceReported)
{
	AFTOGameState* GS = GetFTOGameState();
	if (!GS)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFTOIncident* Incident = GetWorld()->SpawnActor<AFTOIncident>(IncidentClass, Where, Params);
	if (!Incident)
	{
		return nullptr;
	}
	const FVector Location = Where.GetLocation();
	if (BuildingIndex != INDEX_NONE)
	{
		Incident->SetBuilding(BuildingIndex);
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

	UE_LOG(LogFTO, Log, TEXT("New incident: %s (%s) at %s%s%s"),
		*Info.Title.ToString(), *FTOCrime::TierName(Info.Tier).ToString(), *Location.ToCompactString(),
		BuildingIndex != INDEX_NONE ? *FString::Printf(TEXT(" inside building %d"), BuildingIndex) : TEXT(""),
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
	// Credit whoever put the cuffs on (else whoever put the perp on the floor).
	const AActor* Arrester = Incident->GetArrestingOfficer();
	FTOScoring::IncidentResolved(Incident, Arrester ? Arrester : Incident->GetSubduedBy());

	// Perps get cuffed and have to be walked or driven back to the precinct for the rest of the credit.
	AFTOPerp* Perp = Incident->GetPerp();
	if (Info.bArrest)
	{
		// Whoever put the cuffs on (else whoever put them on the floor, else the nearest officer).
		const AController* Subduer = Incident->GetSubduedBy();
		AFTOCharacter* Arresting = Incident->GetArrestingOfficer();
		if (!Arresting && Subduer)
		{
			Arresting = Cast<AFTOCharacter>(Subduer->GetPawn());
		}
		float BestDistSq = FMath::Square(Incident->GetSceneRadius() * 3.f);
		for (TActorIterator<AFTOCharacter> It(GetWorld()); It && !Arresting; ++It)
		{
			const float DistSq = FVector::DistSquared2D(It->GetActorLocation(), Incident->GetActorLocation());
			if (DistSq < BestDistSq && It->GetController())
			{
				Arresting = *It;
				BestDistSq = DistSq;
			}
		}
		if (!Arresting)
		{
			// Officers in cruisers are hidden but still "there".
			for (TActorIterator<AFTOCharacter> It(GetWorld()); It; ++It)
			{
				const float DistSq = FVector::DistSquared2D(It->GetActorLocation(), Incident->GetActorLocation());
				if (DistSq < BestDistSq)
				{
					Arresting = *It;
					BestDistSq = DistSq;
				}
			}
		}
		if (Arresting)
		{
			// The perp becomes a cuffed arrestee right where they knelt (and gets up to go with the officer).
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector SpawnAt = Perp ? Perp->GetActorLocation() : Incident->GetActorLocation() + FVector(0.f, 0.f, AFTOPedestrian::HalfHeight);
			const FRotator Facing = Perp ? FRotator(0.f, Perp->GetActorRotation().Yaw, 0.f) : Incident->GetActorRotation();
			if (AFTOArrestee* Cuffed = GetWorld()->SpawnActor<AFTOArrestee>(AFTOArrestee::StaticClass(), SpawnAt, Facing, Params))
			{
				Cuffed->Init(Arresting, FMath::Max(2.f, Info.ChaosRelief * 0.6f), Info.Title);
				if (Perp)
				{
					Perp->Destroy();
				}
			}
		}
	}
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

	// Ignored problems grow into bigger problems (indoors, right there in the same room).
	if (!Info.EscalatesTo.IsNone() && GS->GetShiftPhase() == EFTOShiftPhase::OnDuty)
	{
		if (const FFTOCrimeTemplate* Next = Catalog->FindTemplate(Info.EscalatesTo))
		{
			FTransform Where = Incident->GetActorTransform();
			if (!Incident->IsIndoors())
			{
				Where.AddToTranslation(FVector(Rng.FRandRange(-300.f, 300.f), Rng.FRandRange(-300.f, 300.f), 0.f));
			}
			SpawnFromTemplate(*Next, Where, Incident->GetBuildingIndex(), true);
		}
	}
}

AFTOIncident* UFTOCrimeDirector::SpawnIncidentAt(FName TemplateId, const FVector& Location, bool bForceReported)
{
	return SpawnIncidentAt(TemplateId, FTransform(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), Location), INDEX_NONE, bForceReported);
}

AFTOIncident* UFTOCrimeDirector::SpawnIncidentAt(FName TemplateId, const FTransform& Where, int32 BuildingIndex, bool bForceReported)
{
	const FFTOCrimeTemplate* Template = Catalog ? Catalog->FindTemplate(TemplateId) : nullptr;
	return Template ? SpawnFromTemplate(*Template, Where, BuildingIndex, bForceReported) : nullptr;
}

void UFTOCrimeDirector::TickOvertimeVote()
{
	// The city's paused, but anything still going on (a struggle, a booking) can tip it over.
	AFTOGameState* GS = GetFTOGameState();
	if (GS->GetChaos() >= AFTOGameState::MaxChaos)
	{
		GS->SetShiftPhase(EFTOShiftPhase::Overrun);
		return;
	}
	// Over as soon as every officer has had their say, or the time's up.
	int32 Officers = 0;
	int32 Voted = 0;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS); Officer && !Officer->IsSpectator())
		{
			++Officers;
			Voted += Officer->GetShiftVote() != EFTOShiftVote::None ? 1 : 0;
		}
	}
	if ((Officers > 0 && Voted == Officers) || GS->GetVoteTimeRemaining() <= 0.f)
	{
		ResolveOvertimeVote();
	}
}

void UFTOCrimeDirector::ResolveOvertimeVote()
{
	AFTOGameState* GS = GetFTOGameState();
	if (!GS || GS->GetShiftPhase() != EFTOShiftPhase::OvertimeVote)
	{
		return;
	}
	// Most votes wins. The host speaks for anyone who said nothing and settles a tie (clocking off if the host said
	// nothing either).
	int32 Keep = 0;
	int32 Off = 0;
	int32 Silent = 0;
	EFTOShiftVote HostVote = EFTOShiftVote::None;
	for (APlayerState* PS : GS->PlayerArray)
	{
		const AFTOPlayerState* Officer = Cast<AFTOPlayerState>(PS);
		if (!Officer || Officer->IsSpectator())
		{
			continue;
		}
		Keep += Officer->GetShiftVote() == EFTOShiftVote::Overtime ? 1 : 0;
		Off += Officer->GetShiftVote() == EFTOShiftVote::ClockOff ? 1 : 0;
		Silent += Officer->GetShiftVote() == EFTOShiftVote::None ? 1 : 0;
		if (const APlayerController* PC = Officer->GetPlayerController(); PC && PC->IsLocalController())
		{
			HostVote = Officer->GetShiftVote();
		}
	}
	if (HostVote == EFTOShiftVote::Overtime)
	{
		Keep += Silent;
	}
	else if (HostVote == EFTOShiftVote::ClockOff)
	{
		Off += Silent;
	}
	const bool bOvertime = Keep != Off ? Keep > Off : HostVote == EFTOShiftVote::Overtime;

	FText Message;
	if (bOvertime)
	{
		GS->StartOvertime(OvertimeSeconds);
		NextSpawnTime = GetWorld()->GetTimeSeconds() + FirstCrimeDelay;
		Message = FText::Format(INVTEXT("OVERTIME! Another {0} minutes on the clock."), FText::AsNumber(FMath::RoundToInt(OvertimeSeconds / 60.f)));
	}
	else
	{
		GS->SetShiftPhase(EFTOShiftPhase::Survived);
		Message = INVTEXT("Clocking off. Good shift, officers!");
	}
	UE_LOG(LogFTO, Log, TEXT("Overtime vote: %d for, %d against, host %s: %s."), Keep, Off, *StaticEnum<EFTOShiftVote>()->GetNameStringByValue(int64(HostVote)),
		bOvertime ? TEXT("overtime") : TEXT("clocking off"));
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(It->Get()))
		{
			PC->ClientToast(Message, bOvertime ? FLinearColor(1.f, 0.75f, 0.2f) : FLinearColor(0.4f, 1.f, 0.5f));
		}
	}
}