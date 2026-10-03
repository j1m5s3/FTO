#include "Crime/FTOBomb.h"
#include "Art/FTOArt.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Crime/FTOIncident.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Physics/FTODestruction.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* WireNames[AFTOBomb::NumWires] = { TEXT("red"), TEXT("blue"), TEXT("green"), TEXT("yellow") };
	const FLinearColor WireColors[AFTOBomb::NumWires] =
	{
		FLinearColor(0.9f, 0.08f, 0.05f), FLinearColor(0.1f, 0.25f, 0.95f), FLinearColor(0.1f, 0.75f, 0.15f), FLinearColor(1.f, 0.85f, 0.1f)
	};
	/** Riddles for each wire (the villain's very helpful label maker). */
	const TCHAR* Riddles[AFTOBomb::NumWires][3] =
	{
		{ TEXT("the colour of a fire engine"), TEXT("the colour of a ripe tomato"), TEXT("the colour of a stop sign") },
		{ TEXT("the colour of the sky on a nice day"), TEXT("the colour of the deep blue sea"), TEXT("the colour of a blueberry") },
		{ TEXT("the colour of a frog"), TEXT("the colour of freshly cut grass"), TEXT("the colour of my envy") },
		{ TEXT("the colour of a rubber duck"), TEXT("the colour of a banana"), TEXT("the colour of a smiley face") },
	};

	float BombNow(const UWorld* World)
	{
		const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
		return GS ? GS->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.f);
	}
}

AFTOBomb::AFTOBomb()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// A battered crate with a big clock on the front and four wires looping over the top.
	Case = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Case"));
	Case->SetupAttachment(Root);
	Case->SetStaticMesh(CubeMesh.Object);
	Case->SetRelativeLocation(FVector(0.f, 0.f, 30.f));
	Case->SetRelativeScale3D(FVector(0.7f, 0.9f, 0.6f));
	Case->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Case->SetCollisionResponseToAllChannels(ECR_Ignore);
	Case->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	Dial = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Dial"));
	Dial->SetupAttachment(Root);
	Dial->SetStaticMesh(CylinderMesh.Object);
	Dial->SetRelativeLocation(FVector(36.f, 0.f, 34.f));
	Dial->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	Dial->SetRelativeScale3D(FVector(0.4f, 0.4f, 0.04f));
	Dial->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	for (int32 i = 0; i < NumWires; ++i)
	{
		UStaticMeshComponent* Wire = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Wire%d"), i));
		Wire->SetupAttachment(Root);
		Wire->SetStaticMesh(CylinderMesh.Object);
		Wire->SetRelativeLocation(FVector(0.f, -30.f + i * 20.f, 64.f));
		Wire->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
		Wire->SetRelativeScale3D(FVector(0.05f, 0.05f, 0.6f));
		Wire->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wire->SetCastShadow(false);
		Wires.Add(Wire);
	}

	Readout = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Readout"));
	Readout->SetupAttachment(Root);
	Readout->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	Readout->SetHorizontalAlignment(EHTA_Center);
	Readout->SetVerticalAlignment(EVRTA_TextCenter);
	Readout->SetWorldSize(40.f);
	Readout->SetTextRenderColor(FColor(255, 60, 40));

	Focus = CreateDefaultSubobject<UBoxComponent>(TEXT("Focus"));
	Focus->SetupAttachment(Root);
	Focus->SetRelativeLocation(FVector(0.f, 0.f, 50.f));
	Focus->SetBoxExtent(FVector(60.f, 60.f, 60.f));
	Focus->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Focus->SetCollisionObjectType(ECC_WorldDynamic);
	Focus->SetCollisionResponseToAllChannels(ECR_Ignore);
}

void AFTOBomb::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOBomb, Incident);
	DOREPLIFETIME(AFTOBomb, FuseEndTime);
	DOREPLIFETIME(AFTOBomb, Seed);
	DOREPLIFETIME(AFTOBomb, CutMask);
	DOREPLIFETIME(AFTOBomb, Stage);
	DOREPLIFETIME(AFTOBomb, bDefused);
	DOREPLIFETIME(AFTOBomb, bExploded);
}

void AFTOBomb::Arm(AFTOIncident* InIncident, float Fuse, int32 InSeed)
{
	check(HasAuthority());
	Incident = InIncident;
	Seed = InSeed;
	FuseEndTime = GetWorld()->GetTimeSeconds() + Fuse;
	OnRep_Wires();
}

float AFTOBomb::GetTimeLeft() const
{
	return FMath::Max(0.f, FuseEndTime - BombNow(GetWorld()));
}

void AFTOBomb::SetTimeLeft(float Seconds)
{
	FuseEndTime = GetWorld()->GetTimeSeconds() + Seconds;
}

int32 AFTOBomb::WireFor(int32 Step) const
{
	// Three of the four, in an order of the seed's choosing.
	int32 Order[NumWires] = { 0, 1, 2, 3 };
	FRandomStream Rng(Seed);
	for (int32 i = NumWires - 1; i > 0; --i)
	{
		Swap(Order[i], Order[Rng.RandRange(0, i)]);
	}
	return Order[FMath::Clamp(Step, 0, NumWires - 1)];
}

int32 AFTOBomb::GetNextWire() const
{
	return WireFor(Stage);
}

FString AFTOBomb::Clue() const
{
	const int32 Wire = WireFor(Stage);
	return Riddles[Wire][(uint32(Seed) + uint32(Stage) * 7u) % 3u];
}

void AFTOBomb::OnRep_Wires()
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, FTOArt::BaseMaterialPath);
	FTOArt::ApplyColor(Case, Base, FLinearColor(0.25f, 0.2f, 0.15f));
	FTOArt::ApplyColor(Dial, Base, bDefused ? FLinearColor(0.2f, 1.f, 0.3f) : FLinearColor(1.f, 0.1f, 0.05f), 2.f);
	for (int32 i = 0; i < Wires.Num(); ++i)
	{
		FTOArt::ApplyColor(Wires[i], Base, WireColors[i], 0.3f);
		// A cut wire is two stubs, not a loop.
		Wires[i]->SetRelativeScale3D(FVector(0.05f, 0.05f, (CutMask & (1 << i)) ? 0.15f : 0.6f));
	}
}

void AFTOBomb::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Left = GetTimeLeft();
	if (bDefused)
	{
		Readout->SetText(INVTEXT("DEFUSED"));
		Readout->SetTextRenderColor(FColor(80, 255, 100));
	}
	else if (!bExploded)
	{
		const int32 Secs = FMath::CeilToInt(Left);
		Readout->SetText(FText::FromString(FString::Printf(TEXT("%d:%02d"), Secs / 60, Secs % 60)));
		// Ticking: a click a second, faster in the last ten.
		const int32 Beat = Left < 10.f ? FMath::FloorToInt(Left * 2.f) : Secs;
		if (Beat != LastBeep && GetNetMode() != NM_DedicatedServer)
		{
			LastBeep = Beat;
			UGameplayStatics::PlaySoundAtLocation(this, AFTOGameState::Sounds().Click, GetActorLocation(), 0.6f, Left < 10.f ? 1.6f : 1.f, 0.f, AFTOGameState::Sounds().World);
		}
	}
	// Always face whoever's looking.
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		const FVector To = PC->PlayerCameraManager->GetCameraLocation() - Readout->GetComponentLocation();
		Readout->SetWorldRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
	}

	if (HasAuthority() && !bDefused && !bExploded && Incident && FuseEndTime > 0.f)
	{
		// The city holds its breath while the squad votes on overtime (the fuse with it), and once the shift's over
		// it's somebody else's problem.
		const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
		const EFTOShiftPhase Phase = GS ? GS->GetShiftPhase() : EFTOShiftPhase::OnDuty;
		if (Phase == EFTOShiftPhase::OvertimeVote)
		{
			FuseEndTime += DeltaSeconds;
		}
		else if (Phase == EFTOShiftPhase::OnDuty && GetWorld()->GetTimeSeconds() >= FuseEndTime)
		{
			Explode();
		}
	}
}

void AFTOBomb::Explode()
{
	bExploded = true;
	// It's not a very good bomb, but it's good enough: windows, walls, street furniture and anyone nearby.
	if (AFTODestruction* Wreckage = AFTODestruction::Get(GetWorld()))
	{
		Wreckage->Blast(GetActorLocation(), BlastRadius, BlastStrength, nullptr);
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AFTOPlayerController* PC = Cast<AFTOPlayerController>(It->Get()))
		{
			PC->ClientToast(INVTEXT("KABOOM! The Evil Masterplan went off. The city is NOT impressed."), FLinearColor(1.f, 0.3f, 0.2f));
		}
	}
	if (Incident)
	{
		Incident->FailNow();
	}
	EndTalks();
	SetActorHiddenInGame(true);
}

void AFTOBomb::EndTalks()
{
	for (TActorIterator<AFTOCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetTalkingTo() == this)
		{
			It->EndTalk();
		}
	}
}

void AFTOBomb::CutWire(int32 Index, AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (bDefused || bExploded || Index < 0 || Index >= NumWires || (CutMask & (1 << Index)))
	{
		return;
	}
	AFTOPlayerController* PC = Officer ? Cast<AFTOPlayerController>(Officer->GetController()) : nullptr;
	if (Officer)
	{
		Officer->PlayTimedAction(EFTOAnimAction::Interact, 0.8f);
	}
	if (Incident)
	{
		Incident->ReportByOfficer();
	}
	AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
	if (Index != WireFor(Stage))
	{
		// Wrong one: it sparks, buzzes and the clock jumps (the wire's still whole: the villain's very forgiving).
		FuseEndTime -= WrongWirePenalty;
		if (GS)
		{
			GS->MulticastPlaySound(AFTOGameState::Sounds().Fail, GetActorLocation(), 1.f);
		}
		if (PC)
		{
			PC->ClientToast(FText::FromString(FString::Printf(TEXT("BZZT! Not the %s wire! The clock just jumped %.0f seconds."), WireNames[Index], WrongWirePenalty)), FLinearColor(1.f, 0.4f, 0.3f));
		}
		return;
	}
	CutMask |= (1 << Index);
	OnRep_Wires();
	++Stage;
	if (GS)
	{
		GS->MulticastPlaySound(AFTOGameState::Sounds().Click, GetActorLocation(), 1.f);
	}
	if (Stage >= WiresToCut)
	{
		bDefused = true;
		OnRep_Wires();
		if (GS)
		{
			GS->MulticastPlaySound(AFTOGameState::Sounds().Fanfare, GetActorLocation(), 1.f);
		}
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (AFTOPlayerController* Any = Cast<AFTOPlayerController>(It->Get()))
			{
				Any->ClientToast(INVTEXT("Bomb defused! The Evil Masterplan is foiled (it was full of glitter anyway)."), FLinearColor(0.4f, 1.f, 0.5f));
			}
		}
		if (Incident)
		{
			Incident->HandledPeacefully();
		}
		EndTalks();
		return;
	}
	if (PC)
	{
		PC->ClientToast(FText::FromString(FString::Printf(TEXT("Snip! The %s wire. %d to go."), WireNames[Index], WiresToCut - Stage)), FLinearColor(0.6f, 0.85f, 1.f));
	}
}

bool AFTOBomb::CanInteract(const AFTOCharacter* Officer) const
{
	return Officer && !bDefused && !bExploded && Officer->IsReadyForAction();
}

FText AFTOBomb::GetInteractPrompt(const AFTOCharacter* Officer) const
{
	return INVTEXT("Defuse the bomb");
}

void AFTOBomb::Interact(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (CanInteract(Officer))
	{
		Officer->BeginTalk(this);
	}
}

FVector AFTOBomb::GetInteractLocation() const
{
	return GetActorLocation() + FVector(0.f, 0.f, 50.f);
}

FText AFTOBomb::GetTalkTitle() const
{
	const int32 Secs = FMath::CeilToInt(GetTimeLeft());
	return FText::FromString(FString::Printf(TEXT("BOMB %d:%02d  |  Label says: cut the wire that's %s"), Secs / 60, Secs % 60, *Clue()));
}

void AFTOBomb::GetTalkOptions(const AFTOCharacter* Officer, TArray<FText>& OutOptions) const
{
	for (int32 i = 0; i < NumWires; ++i)
	{
		OutOptions.Add(FText::FromString((CutMask & (1 << i)) ? FString::Printf(TEXT("(the %s wire's cut)"), WireNames[i]) : FString::Printf(TEXT("Cut the %s wire"), WireNames[i])));
	}
}

bool AFTOBomb::TalkChoice(AFTOCharacter* Officer, int32 Index)
{
	CutWire(Index, Officer);
	return !bDefused && !bExploded;
}
