#include "Crime/FTOIncident.h"
#include "Crime/FTOPerp.h"
#include "Core/FTOCharacter.h"
#include "City/FTOCityGenerator.h"
#include "City/FTOTrafficCar.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Art/FTOArt.h"

AFTOIncident::AFTOIncident()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// Every officer needs the dispatch board, no matter where they are.
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(5.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMat(FTOArt::BaseMaterialPath);
	BaseMaterial = BaseMat.Object;

	Beacon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beacon"));
	Beacon->SetupAttachment(Root);
	Beacon->SetStaticMesh(ConeMesh.Object);
	Beacon->SetRelativeLocation(FVector(0.f, 0.f, 450.f));
	Beacon->SetRelativeRotation(FRotator(180.f, 0.f, 0.f)); // point down at the scene
	Beacon->SetRelativeScale3D(FVector(1.2f, 1.2f, 1.6f));
	Beacon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beacon->SetCastShadow(false);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Root);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 620.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(60.f);
	Label->SetTextRenderColor(FColor::White);
}

void AFTOIncident::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOIncident, Info);
	DOREPLIFETIME(AFTOIncident, State);
	DOREPLIFETIME(AFTOIncident, Progress);
	DOREPLIFETIME(AFTOIncident, OfficersOnScene);
	DOREPLIFETIME(AFTOIncident, bWitnessed);
	DOREPLIFETIME(AFTOIncident, StartTime);
	DOREPLIFETIME(AFTOIncident, NeglectTime);
	DOREPLIFETIME(AFTOIncident, bMobile);
	DOREPLIFETIME(AFTOIncident, BuildingIndex);
	DOREPLIFETIME(AFTOIncident, Perp);
	DOREPLIFETIME(AFTOIncident, bSubdued);
	DOREPLIFETIME(AFTOIncident, bFootChase);
}

void AFTOIncident::SetBuilding(int32 Index)
{
	check(HasAuthority());
	BuildingIndex = Index;
	RefreshVisuals();
}

void AFTOIncident::BeginPlay()
{
	Super::BeginPlay();
	RefreshVisuals();
}

void AFTOIncident::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The perp goes with the incident (once handled they've been swapped for a cuffed arrestee anyway).
	if (HasAuthority() && IsValid(Perp))
	{
		Perp->Destroy();
	}
	Super::EndPlay(EndPlayReason);
}

void AFTOIncident::InitIncident(const FFTOIncidentInfo& InInfo, bool bInWillBeReported, float InReportDelay)
{
	check(HasAuthority());
	Info = InInfo;
	StartTime = GetWorld()->GetTimeSeconds();
	bWillBeReported = bInWillBeReported;
	ReportAt = StartTime + InReportDelay;
	State = EFTOIncidentState::Unreported;

	// Whoever's at the heart of it stands right here, facing the way the incident faces: a crook for crimes, a
	// citizen for calls (the cat's owner, the lost tourist).
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Perp = GetWorld()->SpawnActor<AFTOPerp>(AFTOPerp::StaticClass(), GetActorLocation() + FVector(0.f, 0.f, AFTOPedestrian::HalfHeight),
		FRotator(0.f, GetActorRotation().Yaw, 0.f), Params);
	if (Perp)
	{
		Perp->Setup(this, Info.bArrest, Info.bArmed, GetTypeHash(GetActorLocation()) + int32(StartTime * 100.f));
	}
	RefreshVisuals();
}

float AFTOIncident::GetAge() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	const float Now = GS ? GS->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.f);
	return FMath::Max(0.f, Now - StartTime);
}

float AFTOIncident::GetUrgency() const
{
	return Info.TimeToEscalate > 0.f ? FMath::Clamp(NeglectTime / Info.TimeToEscalate, 0.f, 1.f) : 0.f;
}

float AFTOIncident::GetChaosRate() const
{
	if (!IsActive() || bSubdued)
	{
		return 0.f;
	}
	// Incidents being handled still hurt, just less.
	return State == EFTOIncidentState::Responding ? Info.ChaosPerSecond * 0.35f : Info.ChaosPerSecond;
}

void AFTOIncident::ReportByOfficer()
{
	check(HasAuthority());
	if (State == EFTOIncidentState::Unreported)
	{
		bWitnessed = true;
		ForceReport();
	}
}

void AFTOIncident::ForceReport()
{
	check(HasAuthority());
	if (State == EFTOIncidentState::Unreported)
	{
		SetState(EFTOIncidentState::Reported);
		OnReported.Broadcast(this);
	}
}

void AFTOIncident::FollowActor(AActor* Target)
{
	check(HasAuthority());
	if (!Target)
	{
		return;
	}
	// The perp is in the car.
	if (IsValid(Perp))
	{
		Perp->Destroy();
		Perp = nullptr;
	}
	// Attachment only replicates for actors that replicate movement.
	SetReplicateMovement(true);
	AttachToActor(Target, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	bMobile = true;
}

void AFTOIncident::Subdue(AController* ByPolice)
{
	check(HasAuthority());
	if (ByPolice)
	{
		SubduedBy = ByPolice;
	}
	if (!IsActive() || bSubdued)
	{
		return;
	}
	// Caught red-handed counts as witnessed (and as reported, if nobody had called it in yet). Handled once the
	// cuffs are on (CompleteArrest).
	bSubdued = true;
	if (State == EFTOIncidentState::Unreported)
	{
		bWitnessed = true;
		OnReported.Broadcast(this);
	}
	Progress = 1.f;
	SetState(EFTOIncidentState::Responding);
	RefreshVisuals();
}

void AFTOIncident::CompleteArrest(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (!IsActive())
	{
		return;
	}
	ArrestingOfficer = Officer;
	bSubdued = true;
	Resolve();
}

void AFTOIncident::StartFootChase()
{
	check(HasAuthority());
	if (!IsValid(Perp) || bFootChase || !IsActive())
	{
		return;
	}
	// The call's out of the building now (the people inside can relax), and the marker runs with the suspect.
	bFootChase = true;
	BuildingIndex = INDEX_NONE;
	SetReplicateMovement(true); // attachment only replicates for actors that replicate movement
	AttachToActor(Perp, FAttachmentTransformRules::KeepWorldTransform);
	SetActorRelativeLocation(FVector(0.f, 0.f, -AFTOPedestrian::HalfHeight));
	RefreshVisuals();
}

void AFTOIncident::PerpGotAway()
{
	check(HasAuthority());
	if (!IsActive())
	{
		return;
	}
	// Gone: the call goes cold (and doesn't turn into anything worse right here).
	Info.EscalatesTo = NAME_None;
	SetState(EFTOIncidentState::Failed);
	OnFailed.Broadcast(this);
	SetLifeSpan(CleanupDelay);
}

void AFTOIncident::TalkedDown()
{
	if (!IsValid(Perp) && bMobile)
	{
		BringOutTheDriver();
	}
	if (!IsValid(Perp))
	{
		Resolve(); // nobody to cuff after all
		return;
	}
	Perp->ToastOfficersNear(INVTEXT("They've given up: cuff them (E)!"), FLinearColor(0.6f, 0.85f, 1.f), GetSceneRadius() * 2.f);
	Perp->GiveUp(nullptr);
}

void AFTOIncident::BringOutTheDriver()
{
	AFTOTrafficCar* Car = Cast<AFTOTrafficCar>(GetAttachParentActor());
	if (!Car)
	{
		return;
	}
	// Pulled over for good: the driver climbs out of their door and the scene is right there beside the car.
	const FVector Door = Car->GetDriverDoorLocation();
	const float Yaw = (Door - Car->GetActorLocation()).GetSafeNormal2D().Rotation().Yaw;
	Car->DriverSurrenders();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorLocationAndRotation(Door, FRotator(0.f, Yaw, 0.f));
	bMobile = false;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Perp = GetWorld()->SpawnActor<AFTOPerp>(AFTOPerp::StaticClass(), Door + FVector(0.f, 0.f, AFTOPedestrian::HalfHeight), FRotator(0.f, Yaw, 0.f), Params);
	if (Perp)
	{
		Perp->Setup(this, true, false, GetTypeHash(Door));
	}
	RefreshVisuals();
}

void AFTOIncident::Resolve()
{
	if (!IsActive())
	{
		return;
	}
	Progress = 1.f;
	SetState(EFTOIncidentState::Resolved);
	OnResolved.Broadcast(this);
	SetLifeSpan(CleanupDelay);
}

void AFTOIncident::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		ServerTick(DeltaSeconds);
	}

	// Cosmetic: spin the beacon, keep the label facing the local camera.
	Beacon->AddLocalRotation(FRotator(0.f, 90.f * DeltaSeconds, 0.f));

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			const FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - Label->GetComponentLocation();
			Label->SetWorldRotation(FRotator(0.f, ToCamera.Rotation().Yaw, 0.f));
		}
	}
}

void AFTOIncident::ServerTick(float DeltaSeconds)
{
	// Subdued: nothing to do but wait for the cuffs.
	if (!IsActive() || bSubdued)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	// A citizen calls it in.
	if (State == EFTOIncidentState::Unreported && bWillBeReported && Now >= ReportAt)
	{
		SetState(EFTOIncidentState::Reported);
		OnReported.Broadcast(this);
	}

	// Officers on patrol can catch it in the act (throttled; traces aren't free).
	if (State == EFTOIncidentState::Unreported)
	{
		WitnessCheckAccumulator += DeltaSeconds;
		if (WitnessCheckAccumulator >= 0.25f)
		{
			WitnessCheckAccumulator = 0.f;
			if (IsWitnessedByAnyOfficer())
			{
				bWitnessed = true;
				SetState(EFTOIncidentState::Reported);
				OnReported.Broadcast(this);
			}
		}
	}

	// A perp on the run: the chase is on while an officer's close behind (and nobody's talked down meanwhile).
	if (bFootChase)
	{
		int32 Chasing = 0;
		for (const APlayerState* PS : GetWorld()->GetGameState()->PlayerArray)
		{
			const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
			Chasing += Pawn && FVector::DistSquared2D(Pawn->GetActorLocation(), GetActorLocation()) <= FMath::Square(FootChaseRadius) ? 1 : 0;
		}
		OfficersOnScene = Chasing;
		SetState(Chasing > 0 ? EFTOIncidentState::Responding : EFTOIncidentState::Reported);
		return;
	}

	OfficersOnScene = CountOfficersOnScene();

	// Wrestling an officer, or being cuffed: the scene waits on how that goes.
	if (IsValid(Perp) && Perp->IsInArrest())
	{
		SetState(EFTOIncidentState::Responding);
		return;
	}

	if (OfficersOnScene > 0)
	{
		if (State != EFTOIncidentState::Responding)
		{
			// Officers can stumble onto an unreported incident too.
			if (State == EFTOIncidentState::Unreported)
			{
				bWitnessed = true;
				OnReported.Broadcast(this);
			}
			SetState(EFTOIncidentState::Responding);
		}

		const float Crew = FMath::Min(OfficersOnScene, Info.OfficersRequired) / float(FMath::Max(1, Info.OfficersRequired));
		// Under-staffed scenes still progress, but slowly.
		const float Rate = (OfficersOnScene >= Info.OfficersRequired ? 1.f : Crew * 0.4f) / FMath::Max(0.1f, Info.ResolveSeconds);
		Progress = FMath::Min(1.f, Progress + Rate * DeltaSeconds);

		if (Progress >= 1.f)
		{
			// Calls are handled; crooks give up and wait for the cuffs.
			if (Info.bArrest)
			{
				TalkedDown();
			}
			else
			{
				Resolve();
			}
		}
	}
	else
	{
		if (State == EFTOIncidentState::Responding)
		{
			SetState(EFTOIncidentState::Reported);
		}
		Progress = FMath::Max(0.f, Progress - 0.05f * DeltaSeconds);
		NeglectTime += DeltaSeconds;

		if (Info.TimeToEscalate > 0.f && NeglectTime >= Info.TimeToEscalate)
		{
			SetState(EFTOIncidentState::Failed);
			OnFailed.Broadcast(this);
			SetLifeSpan(CleanupDelay);
		}
	}
}

int32 AFTOIncident::CountOfficersOnScene() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	if (!GS)
	{
		return 0;
	}

	// Indoor scenes are handled from inside, not through the shop window.
	const FFTOBuilding* Building = nullptr;
	if (IsIndoors())
	{
		if (!City.IsValid())
		{
			for (TActorIterator<AFTOCityGenerator> It(GetWorld()); It; ++It)
			{
				City = *It;
				break;
			}
		}
		Building = City.IsValid() ? City->GetBuilding(BuildingIndex) : nullptr;
	}

	int32 Count = 0;
	const FVector Here = GetActorLocation();
	for (const APlayerState* PS : GS->PlayerArray)
	{
		const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		// Scenes are handled on foot; chases can be won from behind the wheel.
		if (!bMobile && !Pawn->IsA<AFTOCharacter>())
		{
			continue;
		}
		if (FVector::DistSquared2D(Pawn->GetActorLocation(), Here) <= FMath::Square(GetSceneRadius()) &&
			(!Building || Building->Contains(Pawn->GetActorLocation() - FVector(0.f, 0.f, 90.f), 60.f)))
		{
			++Count;
		}
	}
	return Count;
}

bool AFTOIncident::IsWitnessedByAnyOfficer() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	if (!GS)
	{
		return false;
	}

	// Can any officer nearby see the perp's chest?
	const FVector Target = IsValid(Perp) ? Perp->GetActorLocation() + FVector(0.f, 0.f, 20.f) : GetActorLocation() + FVector(0.f, 0.f, 110.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FTOWitness), false, this);

	for (const APlayerState* PS : GS->PlayerArray)
	{
		const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
		if (!Pawn || FVector::DistSquared(Pawn->GetActorLocation(), Target) > FMath::Square(WitnessRadius))
		{
			continue;
		}

		Params.ClearIgnoredSourceObjects();
		Params.AddIgnoredActor(this);
		Params.AddIgnoredActor(Pawn);
		Params.AddIgnoredActor(Perp);

		FHitResult Hit;
		const FVector Eye = Pawn->GetPawnViewLocation();
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, Target, ECC_Visibility, Params))
		{
			return true;
		}
	}
	return false;
}

void AFTOIncident::SetState(EFTOIncidentState NewState)
{
	if (State != NewState)
	{
		State = NewState;
		RefreshVisuals();
	}
}

void AFTOIncident::OnRep_State()
{
	RefreshVisuals();
}

void AFTOIncident::OnRep_Info()
{
	RefreshVisuals();
}

void AFTOIncident::RefreshVisuals()
{
	if (!BeaconMaterial)
	{
		BeaconMaterial = FTOArt::ApplyColor(Beacon, BaseMaterial, FLinearColor::White, 0.6f);
	}

	FLinearColor Color = FTOCrime::TierColor(Info.Tier);
	if (State == EFTOIncidentState::Resolved || bSubdued)
	{
		Color = FLinearColor(0.1f, 1.f, 0.3f);
	}
	else if (State == EFTOIncidentState::Failed)
	{
		Color = FLinearColor(0.15f, 0.15f, 0.15f);
	}

	if (BeaconMaterial)
	{
		FTOArt::SetColor(BeaconMaterial, Color, 0.6f);
	}

	// Unreported incidents are just "something happening": no beacon until someone knows. Indoors, a smaller
	// beacon and label hang under the ceiling.
	Beacon->SetVisibility(State != EFTOIncidentState::Unreported);
	Beacon->SetRelativeLocation(FVector(0.f, 0.f, IsIndoors() ? 300.f : 450.f));
	Beacon->SetRelativeScale3D(IsIndoors() ? FVector(0.6f, 0.6f, 0.8f) : FVector(1.2f, 1.2f, 1.6f));
	Label->SetRelativeLocation(FVector(0.f, 0.f, IsIndoors() ? 345.f : 620.f));
	Label->SetWorldSize(IsIndoors() ? 34.f : 60.f);

	FText LabelText = Info.Title;
	switch (State)
	{
	case EFTOIncidentState::Unreported: LabelText = INVTEXT("!"); break;
	case EFTOIncidentState::Resolved:   LabelText = FText::Format(INVTEXT("{0}\nHANDLED"), Info.Title); break;
	case EFTOIncidentState::Failed:     LabelText = FText::Format(INVTEXT("{0}\nWENT COLD"), Info.Title); break;
	default:
		if (bSubdued)
		{
			LabelText = FText::Format(INVTEXT("{0}\nSUBDUED"), Info.Title);
		}
		else if (bFootChase)
		{
			LabelText = FText::Format(INVTEXT("{0}\nON THE RUN!"), Info.Title);
		}
		break;
	}
	Label->SetText(LabelText);
	Label->SetTextRenderColor(Color.ToFColor(true));
}
