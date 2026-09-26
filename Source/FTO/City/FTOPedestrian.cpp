#include "City/FTOPedestrian.h"
#include "City/FTOCityGenerator.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Crime/FTOIncident.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* SmallTalk[] =
	{
		TEXT("\"Lovely day for it, officer.\""),
		TEXT("\"I didn't do it. Whatever it was.\""),
		TEXT("\"Have you tried the donut place on 5th?\""),
		TEXT("\"My cousin's a cop. Well, a mall cop.\""),
		TEXT("\"Is it true you get a free hat?\""),
		TEXT("\"I've never jaywalked in my life. Today.\""),
	};

	const TCHAR* CompassWord(const FVector& Dir)
	{
		const float Yaw = Dir.Rotation().Yaw;
		if (Yaw >= -45.f && Yaw < 45.f) return TEXT("to the north");
		if (Yaw >= 45.f && Yaw < 135.f) return TEXT("to the east");
		if (Yaw >= -135.f && Yaw < -45.f) return TEXT("to the west");
		return TEXT("to the south");
	}
}

AFTOPedestrian::AFTOPedestrian()
{
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(35.f, 85.f);
	Capsule->SetCollisionProfileName(TEXT("Pawn"));
	RootComponent = Capsule;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Capsule);
	Body->SetStaticMesh(CylinderMesh.Object);
	Body->SetRelativeLocation(FVector(0.f, 0.f, -20.f));
	Body->SetRelativeScale3D(FVector(0.65f, 0.65f, 1.1f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Head = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	Head->SetupAttachment(Capsule);
	Head->SetStaticMesh(SphereMesh.Object);
	Head->SetRelativeLocation(FVector(0.f, 0.f, 55.f));
	Head->SetRelativeScale3D(FVector(0.6f));
	Head->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	TurnRate = 540.f;
}

void AFTOPedestrian::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOPedestrian, LookSeed);
}

void AFTOPedestrian::BeginPlay()
{
	Super::BeginPlay();
	OnRep_Look();
}

void AFTOPedestrian::OnRep_Look()
{
	FRandomStream LookRng(LookSeed);
	if (!BodyMaterial)
	{
		BodyMaterial = Body->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (BodyMaterial)
	{
		const FLinearColor Shirt = FLinearColor::MakeFromHSV8(uint8(LookRng.RandRange(0, 255)), 150, 230);
		BodyMaterial->SetVectorParameterValue(TEXT("Color"), Shirt);
	}
	const float Size = LookRng.FRandRange(0.85f, 1.15f);
	Body->SetRelativeScale3D(FVector(0.65f * LookRng.FRandRange(0.85f, 1.3f), 0.65f, 1.1f) * Size);
	Head->SetRelativeScale3D(FVector(0.6f * Size));
}

void AFTOPedestrian::StartWandering(AFTOCityGenerator* InCity, int32 InBlockX, int32 InBlockY, int32 InCorner, int32 InSeed)
{
	check(HasAuthority());
	City = InCity;
	BlockX = InBlockX;
	BlockY = InBlockY;
	Corner = InCorner;
	Rng.Initialize(InSeed);
	LookSeed = InSeed;
	OnRep_Look();

	Direction = Rng.FRand() < 0.5f ? 1 : -1;
	WalkSpeed = Rng.FRandRange(110.f, 190.f);

	const FVector Start = City->GetSidewalkCorner(BlockX, BlockY, Corner) + FVector(0.f, 0.f, 85.f);
	Segment.From = Start;
	Segment.To = Start;
	SetActorLocation(Start);
	WalkToNextCorner();
}

void AFTOPedestrian::WalkToNextCorner()
{
	if (!City)
	{
		return;
	}

	// Mostly stroll around the block; sometimes cross the street to the next block.
	const float Roll = Rng.FRand();
	if (Roll < 0.2f)
	{
		// Cross towards the neighbour across the road from this corner.
		const int32 DX = (Corner == 1 || Corner == 2) ? 1 : -1;
		const int32 DY = (Corner == 2 || Corner == 3) ? 1 : -1;
		const bool bCrossX = Rng.FRand() < 0.5f;
		const int32 NewX = BlockX + (bCrossX ? DX : 0);
		const int32 NewY = BlockY + (bCrossX ? 0 : DY);

		const int32 MaxX = City->NumIntersectionsX() - 2;
		const int32 MaxY = City->NumIntersectionsY() - 2;
		if (NewX >= 0 && NewX <= MaxX && NewY >= 0 && NewY <= MaxY)
		{
			// The mirrored corner on the neighbouring block.
			static const int32 MirrorX[] = { 1, 0, 3, 2 };
			static const int32 MirrorY[] = { 3, 2, 1, 0 };
			BlockX = NewX;
			BlockY = NewY;
			Corner = bCrossX ? MirrorX[Corner] : MirrorY[Corner];
			MoveTo(City->GetSidewalkCorner(BlockX, BlockY, Corner) + FVector(0.f, 0.f, 85.f), WalkSpeed * 1.3f);
			return;
		}
	}

	Corner = (Corner + Direction + 4) % 4;
	MoveTo(City->GetSidewalkCorner(BlockX, BlockY, Corner) + FVector(0.f, 0.f, 85.f), WalkSpeed);
}

void AFTOPedestrian::OnArrived()
{
	// Occasionally stop to look at a pigeon.
	if (Rng.FRand() < 0.15f)
	{
		Hold();
		GetWorldTimerManager().SetTimer(ResumeTimer, this, &AFTOPedestrian::WalkToNextCorner, Rng.FRandRange(1.5f, 5.f), false);
		return;
	}
	WalkToNextCorner();
}

void AFTOPedestrian::TickCosmetics(float DeltaSeconds)
{
	if (GetCurrentSpeed() > 0.f)
	{
		const float T = GetWorld()->GetTimeSeconds() * 10.f + LookSeed;
		Body->SetRelativeLocation(FVector(0.f, 0.f, -20.f + FMath::Abs(FMath::Sin(T)) * 8.f));
		Body->SetRelativeRotation(FRotator(0.f, 0.f, FMath::Sin(T) * 5.f));
	}
}

bool AFTOPedestrian::CanInteract(const AFTOCharacter* Officer) const
{
	return Officer != nullptr;
}

FText AFTOPedestrian::GetInteractPrompt(const AFTOCharacter* Officer) const
{
	return INVTEXT("Chat with citizen");
}

void AFTOPedestrian::Interact(AFTOCharacter* Officer)
{
	check(HasAuthority());
	AFTOPlayerController* PC = Officer ? Cast<AFTOPlayerController>(Officer->GetController()) : nullptr;
	if (!PC)
	{
		return;
	}

	// Stop and face the officer for a moment.
	Hold();
	SetActorRotation(FRotator(0.f, (Officer->GetActorLocation() - GetActorLocation()).Rotation().Yaw, 0.f));
	GetWorldTimerManager().SetTimer(ResumeTimer, this, &AFTOPedestrian::WalkToNextCorner, 2.5f, false);

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= ChatCooldownUntil)
	{
		ChatCooldownUntil = Now + 30.f;

		// Tip-off: the nearest crime nobody has called in yet.
		if (const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
		{
			AFTOIncident* Best = nullptr;
			float BestDist = 6000.f;
			for (AFTOIncident* Incident : GS->GetIncidents())
			{
				if (Incident && Incident->GetState() == EFTOIncidentState::Unreported)
				{
					const float Dist = FVector::Dist2D(Incident->GetActorLocation(), GetActorLocation());
					if (Dist < BestDist)
					{
						Best = Incident;
						BestDist = Dist;
					}
				}
			}

			if (Best)
			{
				Best->ForceReport();
				const FVector Dir = (Best->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
				const int32 Meters = FMath::RoundToInt(BestDist / 100.f);
				const FString Line = Rng.FRand() < 0.5f
					? FString::Printf(TEXT("\"Psst... something shady going on %s, about %dm away.\""), CompassWord(Dir), Meters)
					: FString::Printf(TEXT("\"I saw someone acting weird %s. Like, %dm that way.\""), CompassWord(Dir), Meters);
				PC->ClientToast(FText::FromString(Line), FLinearColor(1.f, 0.85f, 0.3f));
				return;
			}
		}
	}

	PC->ClientToast(FText::FromString(SmallTalk[Rng.RandRange(0, int32(UE_ARRAY_COUNT(SmallTalk)) - 1)]), FLinearColor::White);
}
