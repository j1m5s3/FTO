#include "Core/FTOPrecinctBoard.h"
#include "Art/FTOArt.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/FTOCareer.h"
#include "Core/FTOCharacter.h"
#include "Core/FTOGameState.h"
#include "Core/FTOPlayerController.h"
#include "Core/FTOPlayerState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "FTO.h"

AFTOPrecinctBoard::AFTOPrecinctBoard()
{
	bReplicates = true;
	bAlwaysRelevant = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// A cork board on the wall, at eye height.
	Board = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Board"));
	Board->SetupAttachment(Root);
	Board->SetStaticMesh(CubeMesh.Object);
	Board->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
	Board->SetRelativeScale3D(FVector(0.05f, 1.4f, 0.9f));
	Board->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Sign = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Sign"));
	Sign->SetupAttachment(Root);
	Sign->SetRelativeLocation(FVector(4.f, 0.f, 150.f));
	Sign->SetHorizontalAlignment(EHTA_Center);
	Sign->SetVerticalAlignment(EVRTA_TextCenter);
	Sign->SetWorldSize(16.f);
	Sign->SetTextRenderColor(FColor(30, 30, 30));

	Focus = CreateDefaultSubobject<UBoxComponent>(TEXT("Focus"));
	Focus->SetupAttachment(Root);
	Focus->SetRelativeLocation(FVector(30.f, 0.f, 120.f));
	Focus->SetBoxExtent(FVector(30.f, 70.f, 60.f));
	Focus->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Focus->SetCollisionObjectType(ECC_WorldDynamic);
	Focus->SetCollisionResponseToAllChannels(ECR_Ignore);
}

void AFTOPrecinctBoard::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOPrecinctBoard, Kind);
}

void AFTOPrecinctBoard::SetKind(EFTOBoardKind InKind)
{
	Kind = InKind;
	OnRep_Kind();
}

void AFTOPrecinctBoard::OnRep_Kind()
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, FTOArt::BaseMaterialPath);
	FTOArt::ApplyColor(Board, Base, Kind == EFTOBoardKind::Locker ? FLinearColor(0.55f, 0.38f, 0.2f) : FLinearColor(0.75f, 0.6f, 0.35f));
	Sign->SetText(Kind == EFTOBoardKind::Locker ? INVTEXT("LOCKER ROOM\nOUTFITS & FLEET") : INVTEXT("PRECINCT\nUPGRADES"));
}

FText AFTOPrecinctBoard::GetInteractPrompt(const AFTOCharacter* Officer) const
{
	return Kind == EFTOBoardKind::Locker ? INVTEXT("Outfits and the fleet") : INVTEXT("Precinct upgrades");
}

void AFTOPrecinctBoard::Interact(AFTOCharacter* Officer)
{
	check(HasAuthority());
	if (Officer)
	{
		Officer->BeginTalk(this);
	}
}

FVector AFTOPrecinctBoard::GetInteractLocation() const
{
	return GetActorLocation() + FVector(0.f, 0.f, 130.f);
}

TArray<FName> AFTOPrecinctBoard::OnOffer() const
{
	const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
	TArray<FName> Out;
	for (const FTOCareer::FItem& Item : FTOCareer::Upgrades())
	{
		if (Out.Num() < 3 && !(GS && GS->GetCareer().Has(Item.Id)))
		{
			Out.Add(Item.Id);
		}
	}
	return Out;
}

FText AFTOPrecinctBoard::GetTalkTitle() const
{
	const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
	const FFTOCareerState Career = GS ? GS->GetCareer() : FFTOCareerState();
	return FText::FromString(FString::Printf(TEXT("%s  |  Rank: %s (%d pts earned)  |  Bank: %d pts  |  Level %d"),
		Kind == EFTOBoardKind::Locker ? TEXT("LOCKER ROOM") : TEXT("UPGRADES"), *FTOCareer::RankFor(Career.Earned), Career.Earned, Career.Bank,
		FTOCareer::LevelFor(Career)));
}

void AFTOPrecinctBoard::GetTalkOptions(const AFTOCharacter* Officer, TArray<FText>& OutOptions) const
{
	const AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
	const FFTOCareerState Career = GS ? GS->GetCareer() : FFTOCareerState();
	if (Kind == EFTOBoardKind::Locker)
	{
		const AFTOPlayerState* PS = Officer ? Officer->GetPlayerState<AFTOPlayerState>() : nullptr;
		const FTOCareer::FItem* Outfit = FTOCareer::Find(FTOCareer::Outfits(), PS ? PS->GetOutfit() : NAME_None);
		const FTOCareer::FItem* Livery = FTOCareer::Find(FTOCareer::Liveries(), Career.Livery);
		int32 Unlocked = 0;
		for (const FTOCareer::FItem& Item : FTOCareer::Outfits())
		{
			Unlocked += Career.Earned >= Item.Points ? 1 : 0;
		}
		OutOptions.Add(FText::FromString(FString::Printf(TEXT("Outfit: %s (next; %d unlocked)"), Outfit ? Outfit->Name : TEXT("Classic Blue"), Unlocked)));
		OutOptions.Add(FText::FromString(FString::Printf(TEXT("Fleet: %s (next): %s"), Livery ? Livery->Name : TEXT("Standard Cruiser"), Livery ? Livery->Blurb : TEXT(""))));
		// The next thing the rank unlocks, to aim for.
		const FTOCareer::FItem* Next = nullptr;
		for (const TConstArrayView<FTOCareer::FItem>& Items : { FTOCareer::Outfits(), FTOCareer::Liveries() })
		{
			for (const FTOCareer::FItem& Item : Items)
			{
				if (Item.Points > Career.Earned && (!Next || Item.Points < Next->Points))
				{
					Next = &Item;
				}
			}
		}
		OutOptions.Add(FText::FromString(Next ? FString::Printf(TEXT("(Next unlock at %d pts: %s)"), Next->Points, Next->Name) : FString(TEXT("(Everything's unlocked!)"))));
	}
	else
	{
		const TArray<FName> Offer = OnOffer();
		for (const FName& Id : Offer)
		{
			const FTOCareer::FItem* Item = FTOCareer::Find(FTOCareer::Upgrades(), Id);
			OutOptions.Add(FText::FromString(FString::Printf(TEXT("Buy %s (%d pts): %s"), Item->Name, Item->Points, Item->Blurb)));
		}
		if (Offer.Num() == 0)
		{
			OutOptions.Add(INVTEXT("(Every upgrade's bought. What a precinct!)"));
		}
		while (OutOptions.Num() < 3)
		{
			OutOptions.Add(FText::GetEmpty());
		}
	}
	OutOptions.Add(INVTEXT("That's all."));
}

bool AFTOPrecinctBoard::TalkChoice(AFTOCharacter* Officer, int32 Index)
{
	check(HasAuthority());
	AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
	AFTOPlayerController* PC = Officer ? Cast<AFTOPlayerController>(Officer->GetController()) : nullptr;
	if (!GS || !Officer || Index == 3)
	{
		return false;
	}
	UE_LOG(LogFTO, Log, TEXT("Precinct board (%s): %s chose option %d."), Kind == EFTOBoardKind::Locker ? TEXT("locker") : TEXT("upgrades"), *GetNameSafe(Officer), Index);
	FFTOCareerState Career = GS->GetCareer();
	if (Kind == EFTOBoardKind::Locker)
	{
		if (Index == 0)
		{
			// The next outfit the precinct's rank has unlocked.
			AFTOPlayerState* PS = Officer->GetPlayerState<AFTOPlayerState>();
			const TConstArrayView<FTOCareer::FItem> Outfits = FTOCareer::Outfits();
			int32 At = 0;
			for (int32 i = 0; i < Outfits.Num(); ++i)
			{
				At = PS && Outfits[i].Id == PS->GetOutfit() ? i : At;
			}
			for (int32 Step = 1; Step <= Outfits.Num() && PS; ++Step)
			{
				const FTOCareer::FItem& Item = Outfits[(At + Step) % Outfits.Num()];
				if (Career.Earned >= Item.Points)
				{
					PS->SetOutfit(Item.Id);
					if (PC)
					{
						PC->ClientRememberOutfit(Item.Id);
					}
					UE_LOG(LogFTO, Log, TEXT("%s changed into the %s outfit."), *PS->GetPlayerName(), Item.Name);
					break;
				}
			}
		}
		else if (Index == 1 && GS->GetShiftPhase() != EFTOShiftPhase::Lobby)
		{
			if (PC)
			{
				PC->ClientToast(INVTEXT("The motor pool only repaints the fleet between shifts."), FLinearColor(1.f, 0.6f, 0.3f));
			}
		}
		else if (Index == 1)
		{
			const TConstArrayView<FTOCareer::FItem> Liveries = FTOCareer::Liveries();
			int32 At = 0;
			for (int32 i = 0; i < Liveries.Num(); ++i)
			{
				At = Liveries[i].Id == Career.Livery ? i : At;
			}
			for (int32 Step = 1; Step <= Liveries.Num(); ++Step)
			{
				const FTOCareer::FItem& Item = Liveries[(At + Step) % Liveries.Num()];
				if (Career.Earned >= Item.Points)
				{
					Career.Livery = Item.Id;
					GS->SetCareer(Career, true);
					break;
				}
			}
		}
		return true;
	}
	// Buying an upgrade: between shifts, and not twice from one double-tap (the offer moves up once something's bought).
	const TArray<FName> Offer = OnOffer();
	const float Now = GetWorld()->GetTimeSeconds();
	if (!Offer.IsValidIndex(Index) || (LastPurchaser == Officer && Now - LastPurchaseTime < 1.f))
	{
		return true;
	}
	if (GS->GetShiftPhase() != EFTOShiftPhase::Lobby)
	{
		if (PC)
		{
			PC->ClientToast(INVTEXT("The quartermaster only takes orders between shifts."), FLinearColor(1.f, 0.6f, 0.3f));
		}
		return true;
	}
	const FTOCareer::FItem* Item = FTOCareer::Find(FTOCareer::Upgrades(), Offer[Index]);
	if (Career.Bank < Item->Points)
	{
		if (PC)
		{
			PC->ClientToast(FText::FromString(FString::Printf(TEXT("Not enough in the bank for the %s (%d of %d pts). Survive a few more shifts!"), Item->Name, Career.Bank, Item->Points)), FLinearColor(1.f, 0.6f, 0.3f));
		}
		return true;
	}
	LastPurchaseTime = Now;
	LastPurchaser = Officer;
	Career.Bank -= Item->Points;
	Career.Upgrades.AddUnique(Item->Id);
	GS->SetCareer(Career, true);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AFTOPlayerController* Any = Cast<AFTOPlayerController>(It->Get()))
		{
			Any->ClientToast(FText::FromString(FString::Printf(TEXT("The precinct's got a new %s! %s"), Item->Name, Item->Blurb)), FLinearColor(0.4f, 1.f, 0.5f));
		}
	}
	return true;
}
