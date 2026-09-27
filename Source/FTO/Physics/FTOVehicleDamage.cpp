#include "Physics/FTOVehicleDamage.h"
#include "Art/FTOArt.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/FTOGameState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Physics/FTODebris.h"

namespace
{
	const FLinearColor SmokeGrey(0.22f, 0.22f, 0.23f);
	const FLinearColor SmokeBlack(0.08f, 0.08f, 0.09f);
	const FLinearColor FlameOrange(1.f, 0.3f, 0.03f);
	const FLinearColor FlameYellow(1.f, 0.72f, 0.12f);
	/** Knocks below this don't shake anything loose. */
	constexpr float BitsFrom = 8.f;
}

UFTOVehicleDamage::UFTOVehicleDamage()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // only once there's smoke to show
	SetIsReplicatedByDefault(true);
}

void UFTOVehicleDamage::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UFTOVehicleDamage, State);
	DOREPLIFETIME(UFTOVehicleDamage, Dents);
}

void UFTOVehicleDamage::SetBody(UStaticMeshComponent* InBody, UStaticMesh* InDented)
{
	const bool bNewBody = Body != InBody;
	Body = InBody;
	Pristine = InBody ? InBody->GetStaticMesh() : nullptr;
	Dented = InDented;
	if (bNewBody)
	{
		PaintMaterials.Reset();
		CleanPaint.Reset();
	}
	// Every section of the body dents together (paint, windows, lights): each needs its own instance to be told
	// (gathered afresh: a restyled car has new paint).
	DentMaterials.Reset();
	if (Body)
	{
		for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Material = Body->GetMaterial(Slot);
			UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Material);
			if (!Instance && Material)
			{
				Instance = Body->CreateDynamicMaterialInstance(Slot, Material);
			}
			if (Instance)
			{
				DentMaterials.Add(Instance);
			}
		}
	}
	ApplyState(false);
	ApplyDents();
}

EFTOCarDamage UFTOVehicleDamage::GetStage() const
{
	const float Health = State.Health;
	if (Health <= 0.f)          return EFTOCarDamage::Wrecked;
	if (Health < BurningBelow)  return EFTOCarDamage::Burning;
	if (Health < SmokingBelow)  return EFTOCarDamage::Smoking;
	if (Health < DentedBelow)   return EFTOCarDamage::Dented;
	return EFTOCarDamage::Fine;
}

void UFTOVehicleDamage::ApplyDamage(float Amount, const FVector& At, AController* Instigator)
{
	check(GetOwner()->HasAuthority());
	if (Amount <= 0.f || IsWrecked())
	{
		return;
	}
	const float Before = State.Health;
	State.Health = FMath::Max(0.f, State.Health - Amount);

	// A dent where it landed, pushed towards the middle of the car (mostly sideways: cars get hit side-on and
	// head-on, rarely from underneath), as deep as the knock was hard.
	const FVector Middle = Body ? Body->Bounds.Origin : GetOwner()->GetActorLocation();
	FVector Inward = Middle - At;
	Inward.Z *= 0.3f;
	AddDent(At, Inward.GetSafeNormal(), FMath::Clamp(Amount * 0.9f, 4.f, 26.f), FMath::Clamp(35.f + Amount * 1.4f, 40.f, 85.f),
		FMath::Clamp(Amount / 25.f, 0.25f, 1.f));
	State.LastHit = GetOwner()->GetActorTransform().InverseTransformPosition(At);
	State.LastDamage = Amount;
	++State.Serial;
	if (Instigator)
	{
		LastInstigator = Instigator;
	}
	GetOwner()->ForceNetUpdate();
	OnRep_State();

	// The police knocking a citizen's car about: the city notices, and a write-off goes on the report card.
	AFTOGameState* GS = GetWorld()->GetGameState<AFTOGameState>();
	const bool bPolice = Cast<APlayerController>(Instigator) != nullptr;
	if (GS && bCitizensCar && bPolice)
	{
		GS->AddChaos((Before - State.Health) * 0.02f);
	}
	if (IsWrecked())
	{
		if (GS && bCitizensCar && bPolice)
		{
			GS->AddChaos(3.f);
			++GS->CarsWrecked;
		}
		OnWrecked.Broadcast();
	}
}

void UFTOVehicleDamage::AddScrape(const FVector& At, const FVector& Along)
{
	check(GetOwner()->HasAuthority());
	// Paint off, barely a dent: a long, shallow mark.
	const FVector Middle = Body ? Body->Bounds.Origin : GetOwner()->GetActorLocation();
	FVector Inward = Middle - At;
	Inward.Z = 0.f;
	AddDent(At, Inward.GetSafeNormal(), 0.8f, 45.f, 0.5f);
}

void UFTOVehicleDamage::AddDent(const FVector& At, const FVector& Dir, float Depth, float Radius, float Scrape)
{
	if (!Body)
	{
		return;
	}
	const FTransform Frame = Body->GetComponentTransform();
	FFTODent Dent;
	Dent.Center = Frame.InverseTransformPosition(At);
	Dent.Push = Frame.InverseTransformVectorNoScale(Dir) * Depth;
	Dent.Radius = Radius;
	Dent.Scrape = Scrape;

	// Where the knock landed isn't quite on the paint (it's on the collision box, or a round's hit point): find the
	// paint itself along the line of the push, against the body's own triangles (switched on for queries just for the
	// trace: the rest of the time the body's only for looking at), and fall back to the edge of its bounds.
	{
		const FVector Dir = Frame.TransformVectorNoScale(FVector(Dent.Push).GetSafeNormal());
		FHitResult Paint;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FTODent), true);
		const ECollisionEnabled::Type Was = Body->GetCollisionEnabled();
		Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		const bool bFound = !Dir.IsNearlyZero() && Body->LineTraceComponent(Paint, At - Dir * 300.f, At + Dir * 300.f, Params);
		Body->SetCollisionEnabled(Was);
		if (bFound)
		{
			Dent.Center = Frame.InverseTransformPosition(Paint.ImpactPoint);
		}
		else if (const UStaticMesh* Mesh = Body->GetStaticMesh())
		{
			const FBox Box = Mesh->GetBounds().GetBox();
			const FVector Local = FVector(Dent.Center);
			Dent.Center = Box.GetClosestPointTo(Local) == Local ? Local : Box.GetClosestPointTo(Local);
		}
	}

	// Another knock where there's already a dent goes deeper (up to a point); when the list's full, the nearest takes it.
	int32 Nearest = INDEX_NONE;
	float NearestDist = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Dents.Num(); ++i)
	{
		const float Dist = FVector::Dist(Dents[i].Center, Dent.Center);
		if (Dist < NearestDist)
		{
			NearestDist = Dist;
			Nearest = i;
		}
	}
	if (Nearest != INDEX_NONE && (NearestDist < Dents[Nearest].Radius * 0.6f || Dents.Num() >= MaxDents))
	{
		FFTODent& Into = Dents[Nearest];
		Into.Push = (FVector(Into.Push) + FVector(Dent.Push)).GetClampedToMaxSize(32.f);
		Into.Radius = FMath::Min(FMath::Max(Into.Radius, Dent.Radius) + 4.f, 90.f);
		Into.Scrape = FMath::Min(1.f, Into.Scrape + Dent.Scrape);
	}
	else
	{
		Dents.Add(Dent);
	}
	GetOwner()->ForceNetUpdate();
	ApplyDents();
}

void UFTOVehicleDamage::OnRep_Dents()
{
	ApplyDents();
}

void UFTOVehicleDamage::ApplyDents()
{
	for (UMaterialInstanceDynamic* Material : DentMaterials)
	{
		if (!Material)
		{
			continue;
		}
		for (int32 i = 0; i < MaxDents; ++i)
		{
			const bool bUsed = Dents.IsValidIndex(i);
			const FFTODent Dent = bUsed ? Dents[i] : FFTODent();
			// Unused slots sit far away with nothing pushed.
			Material->SetVectorParameterValue(*FString::Printf(TEXT("Dent%d"), i),
				bUsed ? FLinearColor(Dent.Center.X, Dent.Center.Y, Dent.Center.Z, Dent.Radius) : FLinearColor(0.f, 0.f, -100000.f, 1.f));
			Material->SetVectorParameterValue(*FString::Printf(TEXT("Push%d"), i),
				bUsed ? FLinearColor(Dent.Push.X, Dent.Push.Y, Dent.Push.Z, Dent.Scrape) : FLinearColor(0.f, 0.f, 0.f, 0.f));
		}
	}
}

void UFTOVehicleDamage::Repair()
{
	check(GetOwner()->HasAuthority());
	State = FFTOCarHealth();
	Dents.Reset();
	ApplyDents();
	LastInstigator.Reset();
	GetOwner()->ForceNetUpdate();
	OnRep_State();
}

void UFTOVehicleDamage::OnRep_State()
{
	// The first we hear of a car already knocked about (joining late, or it just came into range) is old news, unless
	// it is its very first knock.
	const bool bFresh = State.Serial != SeenSerial && State.LastDamage > 0.f && (bSeenState || State.Serial == 1);
	SeenSerial = State.Serial;
	bSeenState = true;
	ApplyState(bFresh);
}

void UFTOVehicleDamage::ApplyState(bool bFreshKnock)
{
	if (!Body)
	{
		return;
	}
	const EFTOCarDamage Stage = GetStage();

	// A hard knock shakes bits loose (panels and trim in the paint, a hubcap), off the corner that took it.
	if (bFreshKnock && State.LastDamage >= BitsFrom)
	{
		if (UFTODebris* Debris = UFTODebris::Get(GetWorld()))
		{
			const FTransform Car = GetOwner()->GetActorTransform();
			const FVector At = Car.TransformPosition(State.LastHit);
			const FVector Out = (At - Car.GetLocation()).GetSafeNormal2D() + FVector(0.f, 0.f, 0.5f);
			const int32 Count = FMath::Clamp(FMath::RoundToInt(State.LastDamage / 12.f), 1, 4);
			Debris->Chunks(At, Out * 450.f, PaintColor(), Count, 26.f, 5.f);
			Debris->Chunks(At, Out * 300.f, FLinearColor(0.12f, 0.12f, 0.13f), 1, 14.f, 4.f);
		}
	}

	// A write-off is crumpled all over: the beaten-up body (same sockets, so wheels and people stay put). Before that
	// the dents are wherever it was actually hit (ApplyDents).
	UStaticMesh* Wanted = Stage >= EFTOCarDamage::Wrecked && Dented ? Dented.Get() : Pristine.Get();
	if (Wanted && Body->GetStaticMesh() != Wanted)
	{
		Body->SetStaticMesh(Wanted);
	}

	// Smoke, then fire; the paint chars as it burns.
	const bool bSmoke = Stage >= EFTOCarDamage::Smoking;
	if (bSmoke)
	{
		EnsurePlume();
	}
	SetComponentTickEnabled(bSmoke);
	if (!bSmoke)
	{
		for (UStaticMeshComponent* Puff : Plume)
		{
			Puff->SetVisibility(false);
		}
		if (FireLight)
		{
			FireLight->SetIntensity(0.f);
		}
		if (FireSound && FireSound->IsPlaying())
		{
			FireSound->Stop();
		}
		Scorch(0.f);
	}
	else if (Stage < EFTOCarDamage::Burning)
	{
		Scorch(0.f);
	}
	Shown = Stage;
}

FLinearColor UFTOVehicleDamage::PaintColor() const
{
	if (Body)
	{
		for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
		{
			if (const UMaterialInstanceDynamic* Paint = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(Slot)))
			{
				FLinearColor Color;
				if (Paint->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Color")), Color))
				{
					return Color;
				}
			}
		}
	}
	return FLinearColor(0.6f, 0.6f, 0.62f);
}

void UFTOVehicleDamage::Scorch(float Amount)
{
	if (!Body || Amount == Scorched)
	{
		return;
	}
	// Every slot drawn with the master material gets its own instance to char (paint, trim, lights alike).
	if (PaintMaterials.IsEmpty() && Amount > 0.f)
	{
		for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Material = Body->GetMaterial(Slot);
			const UMaterial* Master = Material ? Material->GetMaterial() : nullptr;
			if (!Master || Master->GetName() != TEXT("M_FTOBase"))
			{
				continue;
			}
			UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Material);
			if (!Instance)
			{
				Instance = UMaterialInstanceDynamic::Create(Material, Body);
				Body->SetMaterial(Slot, Instance);
			}
			PaintMaterials.Add(Instance);
		}
	}
	for (UMaterialInstanceDynamic* Paint : PaintMaterials)
	{
		Paint->SetScalarParameterValue(TEXT("Scorch"), Amount);
	}
	Scorched = Amount;
}

FVector UFTOVehicleDamage::PlumeOrigin() const
{
	// Over the front of the bonnet.
	const UStaticMesh* Mesh = Body ? Body->GetStaticMesh() : nullptr;
	if (!Mesh)
	{
		return FVector::ZeroVector;
	}
	const FBox Bounds = Mesh->GetBoundingBox();
	return FVector(Bounds.Max.X * 0.62f, 0.f, Bounds.Min.Z + (Bounds.Max.Z - Bounds.Min.Z) * 0.5f);
}

void UFTOVehicleDamage::EnsurePlume()
{
	if (!Plume.IsEmpty() || !Body)
	{
		return;
	}
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, FTOArt::BaseMaterialPath);
	for (int32 i = 0; i < SmokePuffs + Flames; ++i)
	{
		UStaticMeshComponent* Puff = NewObject<UStaticMeshComponent>(GetOwner());
		Puff->SetupAttachment(Body);
		Puff->SetUsingAbsoluteScale(true);
		Puff->SetStaticMesh(i < SmokePuffs ? Sphere : Cone); // smoke billows; flames lick up in tongues
		Puff->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Puff->SetCastShadow(i < SmokePuffs);
		Puff->SetVisibility(false);
		Puff->RegisterComponent();
		PlumeMaterials.Add(FTOArt::ApplyColor(Puff, Base, i < SmokePuffs ? SmokeGrey : FlameOrange, i < SmokePuffs ? 0.f : 0.8f));
		Plume.Add(Puff);
	}
	FireLight = NewObject<UPointLightComponent>(GetOwner());
	FireLight->SetupAttachment(Body);
	FireLight->SetRelativeLocation(PlumeOrigin() + FVector(0.f, 0.f, 60.f));
	FireLight->SetLightColor(FlameOrange);
	FireLight->SetAttenuationRadius(700.f);
	FireLight->SetCastShadows(false);
	FireLight->SetIntensity(0.f);
	FireLight->RegisterComponent();
	if (USoundBase* Crackle = AFTOGameState::Sounds().FireLoop)
	{
		FireSound = NewObject<UAudioComponent>(GetOwner());
		FireSound->SetupAttachment(Body);
		FireSound->SetSound(Crackle);
		FireSound->AttenuationSettings = AFTOGameState::Sounds().World;
		FireSound->bAutoActivate = false;
		FireSound->RegisterComponent();
	}
}

void UFTOVehicleDamage::TickPlume(float DeltaTime)
{
	PlumeTime += DeltaTime;
	const EFTOCarDamage Stage = GetStage();
	const bool bBurning = Stage >= EFTOCarDamage::Burning;
	const FVector Origin = PlumeOrigin();

	// Smoke: puffs rolling up off the bonnet, swelling as they rise then thinning away, blacker once it's alight.
	for (int32 i = 0; i < SmokePuffs; ++i)
	{
		UStaticMeshComponent* Puff = Plume[i];
		const float Life = FMath::Frac(PlumeTime * 0.45f + float(i) / SmokePuffs);
		const float Drift = FMath::Sin(i * 2.1f + PlumeTime * 0.7f);
		Puff->SetRelativeLocation(Origin + FVector(-60.f * Life + 10.f * Drift, 25.f * Drift, 20.f + 230.f * Life));
		const float Size = (0.35f + 0.9f * Life) * FMath::Sin(PI * FMath::Min(1.f, Life * 1.15f)) * (bBurning ? 1.3f : 1.f);
		Puff->SetWorldScale3D(FVector(FMath::Max(0.02f, Size)));
		FTOArt::SetColor(PlumeMaterials[i], bBurning ? FMath::Lerp(SmokeBlack, SmokeGrey, Life) : SmokeGrey);
		if (!Puff->IsVisible())
		{
			Puff->SetVisibility(true);
		}
	}

	// Fire: flickering tongues of flame, a dancing glow, and the crackle.
	for (int32 i = 0; i < Flames; ++i)
	{
		UStaticMeshComponent* Flame = Plume[SmokePuffs + i];
		Flame->SetVisibility(bBurning);
		if (!bBurning)
		{
			continue;
		}
		const float Flicker = 0.5f + 0.5f * FMath::Sin(PlumeTime * (9.f + i * 2.3f) + i * 1.7f);
		const float Spread = (i - (Flames - 1) * 0.5f) * 22.f;
		Flame->SetRelativeLocation(Origin + FVector(10.f * FMath::Sin(PlumeTime * 5.f + i), Spread, 25.f + 35.f * Flicker));
		Flame->SetWorldScale3D(FVector(0.3f + 0.12f * Flicker, 0.3f + 0.12f * Flicker, 0.5f + 0.55f * Flicker));
		FTOArt::SetColor(PlumeMaterials[SmokePuffs + i], FMath::Lerp(FlameOrange, FlameYellow, Flicker * 0.7f), 0.25f + 0.35f * Flicker);
	}
	if (FireLight)
	{
		FireLight->SetIntensity(bBurning ? 900.f + 500.f * FMath::Sin(PlumeTime * 13.f) : 0.f);
	}
	if (FireSound && FireSound->IsPlaying() != bBurning)
	{
		bBurning ? FireSound->Play() : FireSound->Stop();
	}

	// The paint chars while it burns (a wreck ends up charcoal).
	if (bBurning)
	{
		const bool bWrecked = Stage == EFTOCarDamage::Wrecked;
		Scorch(FMath::Min(bWrecked ? 0.9f : 0.55f, Scorched + DeltaTime * (bWrecked ? 0.9f : 0.25f)));
	}
}

void UFTOVehicleDamage::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Plume.IsEmpty() && GetNetMode() != NM_DedicatedServer)
	{
		TickPlume(DeltaTime);
	}
}
