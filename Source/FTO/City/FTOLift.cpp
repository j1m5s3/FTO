#include "City/FTOLift.h"
#include "Art/FTOArt.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FLinearColor Steel(0.62f, 0.64f, 0.68f);
	const FLinearColor FrameGrey(0.2f, 0.21f, 0.23f);
	/** Half the doorway's width, and its height (cm). */
	constexpr float HalfWide = 55.f;
	constexpr float Tall = 220.f;
}

AFTOLift::AFTOLift()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetNetUpdateFrequency(4.f);
	SetNetCullDistanceSquared(FMath::Square(12000.f));

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto MakePart = [&](const TCHAR* Name, const FVector& Location, const FVector& Scale)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Root);
		Part->SetStaticMesh(Cube.Object);
		Part->SetRelativeLocation(Location);
		Part->SetRelativeScale3D(Scale);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Part;
	};
	// X points out of the wall, towards whoever's waiting; the doors sit a hand's width proud of it.
	Frame = MakePart(TEXT("Frame"), FVector(3.f, 0.f, Tall * 0.5f + 10.f), FVector(0.06f, (HalfWide * 2.f + 40.f) / 100.f, (Tall + 20.f) / 100.f));
	DoorLeft = MakePart(TEXT("DoorLeft"), FVector(8.f, -HalfWide * 0.5f, Tall * 0.5f), FVector(0.05f, HalfWide / 100.f, Tall / 100.f));
	DoorRight = MakePart(TEXT("DoorRight"), FVector(8.f, HalfWide * 0.5f, Tall * 0.5f), FVector(0.05f, HalfWide / 100.f, Tall / 100.f));
	Lamp = MakePart(TEXT("Lamp"), FVector(8.f, 0.f, Tall + 25.f), FVector(0.04f, 0.5f, 0.14f));

	Sign = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Sign"));
	Sign->SetupAttachment(Root);
	Sign->SetRelativeLocation(FVector(11.f, 0.f, Tall + 25.f));
	Sign->SetHorizontalAlignment(EHTA_Center);
	Sign->SetVerticalAlignment(EVRTA_TextCenter);
	Sign->SetWorldSize(16.f);
	Sign->SetTextRenderColor(FColor(255, 200, 80));
	Sign->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// What an officer's interact focus finds (object queries only; it blocks nothing).
	Focus = CreateDefaultSubobject<UBoxComponent>(TEXT("Focus"));
	Focus->SetupAttachment(Root);
	Focus->SetRelativeLocation(FVector(50.f, 0.f, Tall * 0.5f));
	Focus->SetBoxExtent(FVector(50.f, HalfWide + 20.f, Tall * 0.5f));
	Focus->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Focus->SetCollisionObjectType(ECC_WorldDynamic);
	Focus->SetCollisionResponseToAllChannels(ECR_Ignore);
}

void AFTOLift::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOLift, Floor);
	DOREPLIFETIME(AFTOLift, NumFloors);
	DOREPLIFETIME(AFTOLift, bClosed);
}

void AFTOLift::LinkStops(const TArray<AFTOLift*>& InStops)
{
	for (int32 i = 0; i < InStops.Num(); ++i)
	{
		AFTOLift* Stop = InStops[i];
		if (!Stop)
		{
			continue;
		}
		Stop->Floor = i;
		Stop->NumFloors = InStops.Num();
		Stop->Stops.Reset();
		for (AFTOLift* Other : InStops)
		{
			Stop->Stops.Add(Other);
		}
		Stop->OnRep_Floor();
	}
}

void AFTOLift::OnRep_Floor()
{
	Sign->SetText(FloorName(Floor));
	if (!LampMaterial)
	{
		LampMaterial = FTOArt::ApplyColor(Lamp, LoadObject<UMaterialInterface>(nullptr, FTOArt::BaseMaterialPath), FLinearColor(1.f, 0.7f, 0.2f), 1.5f);
		FTOArt::ApplyColor(Frame, LoadObject<UMaterialInterface>(nullptr, FTOArt::BaseMaterialPath), FrameGrey);
		FTOArt::ApplyColor(DoorLeft, LoadObject<UMaterialInterface>(nullptr, FTOArt::BaseMaterialPath), Steel);
		FTOArt::ApplyColor(DoorRight, LoadObject<UMaterialInterface>(nullptr, FTOArt::BaseMaterialPath), Steel);
	}
}

FText AFTOLift::FloorName(int32 Which) const
{
	return Which == 0 ? INVTEXT("STREET") : FText::Format(INVTEXT("FLOOR {0}"), FText::AsNumber(Which));
}

void AFTOLift::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Doors slide shut for a ride and open again at the other end.
	const float Want = bClosed ? 0.f : 1.f;
	if (Open != Want)
	{
		Open = FMath::FInterpConstantTo(Open, Want, DeltaSeconds, 1.6f);
		const float Slide = HalfWide * 0.9f * Open;
		DoorLeft->SetRelativeLocation(FVector(8.f, -HalfWide * 0.5f - Slide, Tall * 0.5f));
		DoorRight->SetRelativeLocation(FVector(8.f, HalfWide * 0.5f + Slide, Tall * 0.5f));
	}
	if (LampMaterial)
	{
		FTOArt::SetColor(LampMaterial, bClosed ? FLinearColor(1.f, 0.3f, 0.1f) : FLinearColor(1.f, 0.7f, 0.2f), bClosed ? 3.f : 1.5f);
	}
}

void AFTOLift::BeginPlay()
{
	Super::BeginPlay();
	OnRep_Floor();
}

FVector AFTOLift::GetArrivalPoint() const
{
	return GetActorLocation() + GetActorForwardVector() * 110.f + FVector(0.f, 0.f, 98.f);
}

FVector AFTOLift::GetInteractLocation() const
{
	return GetActorLocation() + GetActorForwardVector() * 40.f;
}

FText AFTOLift::GetInteractPrompt(const AFTOCharacter* Officer) const
{
	return FText::Format(INVTEXT("Take the lift ({0})"), FloorName(Floor));
}

void AFTOLift::Interact(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (Officer && !bClosed)
	{
		Officer->BeginTalk(this);
	}
}

FText AFTOLift::GetTalkTitle() const
{
	return FText::Format(INVTEXT("Lift: {0} of {1} floors"), FloorName(Floor), FText::AsNumber(NumFloors - 1));
}

void AFTOLift::GetTalkOptions(const AFTOCharacter* Officer, TArray<FText>& OutOptions) const
{
	const int32 Top = NumFloors - 1;
	OutOptions.Add(Floor < Top ? FText::Format(INVTEXT("Up a floor ({0})"), FloorName(Floor + 1)) : INVTEXT("(this is the top floor)"));
	OutOptions.Add(Floor > 0 ? FText::Format(INVTEXT("Down a floor ({0})"), FloorName(Floor - 1)) : INVTEXT("(this is street level)"));
	OutOptions.Add(Floor < Top ? FText::Format(INVTEXT("Top floor ({0})"), FloorName(Top)) : INVTEXT("(already at the top)"));
	OutOptions.Add(Floor > 0 ? INVTEXT("Street level") : INVTEXT("(already at street level)"));
}

bool AFTOLift::TalkChoice(AFTOCharacter* Officer, int32 Index)
{
	check(HasAuthority());
	const int32 Top = NumFloors - 1;
	int32 Target = Floor;
	switch (Index)
	{
	case 0: Target = FMath::Min(Floor + 1, Top); break;
	case 1: Target = FMath::Max(Floor - 1, 0); break;
	case 2: Target = Top; break;
	case 3: Target = 0; break;
	default: break;
	}
	if (Target == Floor || bClosed)
	{
		return true; // nothing to do: the buttons stay up
	}
	Ride(Target);
	return false;
}

void AFTOLift::Ride(int32 Target)
{
	check(HasAuthority());
	if (bClosed || !Stops.IsValidIndex(Target) || !Stops[Target].IsValid())
	{
		return;
	}
	// Everyone standing at the doors goes along.
	Riders.Reset();
	const FVector Front = GetArrivalPoint();
	for (TActorIterator<AFTOCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->GetCurrentVehicle() && FVector::Dist(It->GetActorLocation(), Front) < 260.f)
		{
			Riders.Add(*It);
			if (It->GetTalkingTo() == this)
			{
				It->EndTalk();
			}
		}
	}
	RideTo = Target;
	bClosed = true;
	ForceNetUpdate();
	if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
	{
		GS->MulticastPlaySound(AFTOGameState::Sounds().ElevatorDing, GetInteractLocation() + FVector(0.f, 0.f, 150.f), 0.8f);
		GS->MulticastPlaySound(AFTOGameState::Sounds().DoorClose, GetInteractLocation() + FVector(0.f, 0.f, 100.f), 0.6f);
	}
	GetWorldTimerManager().SetTimer(RideTimer, this, &AFTOLift::Arrive, RideSeconds, false);
}

void AFTOLift::Arrive()
{
	AFTOLift* To = Stops.IsValidIndex(RideTo) ? Stops[RideTo].Get() : nullptr;
	if (To)
	{
		// Out of the doors upstairs (or down), in the same order they got in, side by side.
		int32 Slot = 0;
		for (const TWeakObjectPtr<AActor>& Rider : Riders)
		{
			AActor* Who = Rider.Get();
			if (!Who)
			{
				continue;
			}
			const FVector Side = FVector::CrossProduct(FVector::UpVector, To->GetActorForwardVector()) * ((Slot % 3) - 1) * 70.f;
			const FVector Behind = To->GetActorForwardVector() * (Slot / 3) * 80.f;
			Who->TeleportTo(To->GetArrivalPoint() + Side + Behind, To->GetActorForwardVector().Rotation());
			if (AController* Controller = Who->GetInstigatorController())
			{
				Controller->SetControlRotation(To->GetActorForwardVector().Rotation());
			}
			++Slot;
		}
		To->bClosed = false;
		To->ForceNetUpdate();
		if (AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>())
		{
			GS->MulticastPlaySound(AFTOGameState::Sounds().ElevatorDing, To->GetInteractLocation() + FVector(0.f, 0.f, 150.f), 0.8f);
			GS->MulticastPlaySound(AFTOGameState::Sounds().DoorOpen, To->GetInteractLocation() + FVector(0.f, 0.f, 100.f), 0.6f);
		}
	}
	Riders.Reset();
	RideTo = INDEX_NONE;
	// These doors open again once the car's gone.
	bClosed = false;
	ForceNetUpdate();
}
