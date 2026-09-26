#include "Crime/FTOIncident.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
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
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
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

	Suspect = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Suspect"));
	Suspect->SetupAttachment(Root);
	Suspect->SetStaticMesh(SphereMesh.Object);
	Suspect->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	Suspect->SetRelativeScale3D(FVector(0.9f, 0.9f, 1.3f));
	Suspect->SetCollisionEnabled(ECollisionEnabled::NoCollision);

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
}

void AFTOIncident::BeginPlay()
{
	Super::BeginPlay();
	RefreshVisuals();
}

void AFTOIncident::InitIncident(const FFTOIncidentInfo& InInfo, bool bInWillBeReported, float InReportDelay)
{
	check(HasAuthority());
	Info = InInfo;
	StartTime = GetWorld()->GetTimeSeconds();
	bWillBeReported = bInWillBeReported;
	ReportAt = StartTime + InReportDelay;
	State = EFTOIncidentState::Unreported;
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
	if (!IsActive())
	{
		return 0.f;
	}
	// Incidents being handled still hurt, just less.
	return State == EFTOIncidentState::Responding ? Info.ChaosPerSecond * 0.35f : Info.ChaosPerSecond;
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

void AFTOIncident::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		ServerTick(DeltaSeconds);
	}

	// Cosmetic: spin the beacon, bob the suspect, keep the label facing the local camera.
	const float Time = GetWorld()->GetTimeSeconds();
	Beacon->AddLocalRotation(FRotator(0.f, 90.f * DeltaSeconds, 0.f));
	if (IsActive())
	{
		Suspect->SetRelativeLocation(FVector(0.f, 0.f, 60.f + FMath::Abs(FMath::Sin(Time * 6.f)) * 25.f));
	}

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
	if (!IsActive())
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

	OfficersOnScene = CountOfficersOnScene();

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
			SetState(EFTOIncidentState::Resolved);
			OnResolved.Broadcast(this);
			SetLifeSpan(CleanupDelay);
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

	int32 Count = 0;
	const FVector Here = GetActorLocation();
	for (const APlayerState* PS : GS->PlayerArray)
	{
		const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
		if (Pawn && FVector::DistSquared2D(Pawn->GetActorLocation(), Here) <= FMath::Square(SceneRadius))
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

	const FVector Target = Suspect->GetComponentLocation();
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
		// Suspects wear stripy-convict orange until real models land.
		FTOArt::ApplyColor(Suspect, BaseMaterial, FLinearColor(1.f, 0.35f, 0.05f));
	}

	FLinearColor Color = FTOCrime::TierColor(Info.Tier);
	if (State == EFTOIncidentState::Resolved)
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

	// Unreported incidents are just "something happening": no beacon until someone knows.
	Beacon->SetVisibility(State != EFTOIncidentState::Unreported);

	FText LabelText = Info.Title;
	switch (State)
	{
	case EFTOIncidentState::Unreported: LabelText = INVTEXT("!"); break;
	case EFTOIncidentState::Resolved:   LabelText = FText::Format(INVTEXT("{0}\nHANDLED"), Info.Title); break;
	case EFTOIncidentState::Failed:     LabelText = FText::Format(INVTEXT("{0}\nWENT COLD"), Info.Title); break;
	default: break;
	}
	Label->SetText(LabelText);
	Label->SetTextRenderColor(Color.ToFColor(true));
}
