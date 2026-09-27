#include "Crime/FTOGraffitiTag.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const TCHAR* Words[] = { TEXT("GARY"), TEXT("ZAP!"), TEXT("B00M"), TEXT("4EVA"), TEXT("KAOS"), TEXT("YO!"), TEXT("DUDE"), TEXT("BLAM"), TEXT("SK8"), TEXT("WOW") };
	const FColor Paints[] = { FColor(255, 60, 170), FColor(60, 220, 255), FColor(255, 220, 40), FColor(120, 255, 90), FColor(255, 120, 30), FColor(180, 90, 255) };
}

AFTOGraffitiTag::AFTOGraffitiTag()
{
	bReplicates = true;
	SetNetUpdateFrequency(2.f);
	SetNetCullDistanceSquared(FMath::Square(15000.f));

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// Big chunky letters a hair off the wall, with a dark outline behind them.
	Outline = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Outline"));
	Outline->SetupAttachment(Root);
	Outline->SetRelativeLocation(FVector(0.5f, 2.f, -2.f));
	Outline->SetHorizontalAlignment(EHTA_Center);
	Outline->SetVerticalAlignment(EVRTA_TextCenter);
	Outline->SetWorldSize(92.f);
	Outline->SetTextRenderColor(FColor(20, 20, 30));
	Outline->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Letters = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Letters"));
	Letters->SetupAttachment(Root);
	Letters->SetRelativeLocation(FVector(1.5f, 0.f, 0.f));
	Letters->SetHorizontalAlignment(EHTA_Center);
	Letters->SetVerticalAlignment(EVRTA_TextCenter);
	Letters->SetWorldSize(88.f);
	Letters->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AFTOGraffitiTag::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOGraffitiTag, Word);
	DOREPLIFETIME(AFTOGraffitiTag, Paint);
	DOREPLIFETIME(AFTOGraffitiTag, Progress);
}

AFTOGraffitiTag* AFTOGraffitiTag::Spray(UWorld* World, const FVector& OnWall, const FVector& WallNormal, int32 Seed)
{
	if (!World)
	{
		return nullptr;
	}
	// Scrub the oldest if the city's walls are full.
	TArray<AFTOGraffitiTag*> Existing;
	for (TActorIterator<AFTOGraffitiTag> It(World); It; ++It)
	{
		Existing.Add(*It);
	}
	if (Existing.Num() >= MaxTags)
	{
		Existing.Sort([](const AFTOGraffitiTag& A, const AFTOGraffitiTag& B) { return A.GetGameTimeSinceCreation() > B.GetGameTimeSinceCreation(); });
		Existing[0]->Destroy();
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFTOGraffitiTag* Tag = World->SpawnActor<AFTOGraffitiTag>(StaticClass(), OnWall, WallNormal.GetSafeNormal2D().Rotation(), Params);
	if (Tag)
	{
		FRandomStream Rng(Seed);
		Tag->Word = Words[Rng.RandRange(0, UE_ARRAY_COUNT(Words) - 1)];
		Tag->Paint = Paints[Rng.RandRange(0, UE_ARRAY_COUNT(Paints) - 1)];
		Tag->SetActorRotation(Tag->GetActorRotation() + FRotator(0.f, 0.f, Rng.FRandRange(-8.f, 8.f)));
		Tag->OnRep_Tag();
	}
	return Tag;
}

void AFTOGraffitiTag::SetProgress(float InProgress)
{
	const float Clamped = FMath::Clamp(InProgress, 0.f, 1.f);
	if (!FMath::IsNearlyEqual(Clamped, Progress, 0.01f))
	{
		Progress = Clamped;
		OnRep_Tag();
	}
}

void AFTOGraffitiTag::OnRep_Tag()
{
	// A letter at a time, as it's sprayed.
	const int32 Shown = FMath::Clamp(FMath::CeilToInt(Progress * Word.Len()), 0, Word.Len());
	const FText Text = FText::FromString(Word.Left(Shown));
	Letters->SetText(Text);
	Outline->SetText(Text);
	Letters->SetTextRenderColor(Paint);
}
