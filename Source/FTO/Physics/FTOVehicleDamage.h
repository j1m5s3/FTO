#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FTOVehicleDamage.generated.h"

class AController;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** How bad a car looks. */
UENUM()
enum class EFTOCarDamage : uint8
{
	Fine,
	Dented,		// the beaten-up body mesh
	Smoking,	// smoke pouring off the bonnet
	Burning,	// on fire, paint scorching
	Wrecked		// written off: it's not going anywhere
};

/** One dent in a car's body, in the body mesh's own space (M_FTOVehicle pushes the vertices round it in). */
USTRUCT()
struct FFTODent
{
	GENERATED_BODY()

	/** Middle of the dent, and how far out it reaches. */
	UPROPERTY() FVector_NetQuantize10 Center = FVector::ZeroVector;
	UPROPERTY() float Radius = 40.f;
	/** Which way and how far the middle's pushed in (cm). */
	UPROPERTY() FVector_NetQuantize10 Push = FVector::ZeroVector;
	/** 0-1: how much of the paint's scraped back to bare metal. */
	UPROPERTY() float Scrape = 0.f;
};

USTRUCT()
struct FFTOCarHealth
{
	GENERATED_BODY()

	UPROPERTY() float Health = 100.f;
	/** Where the latest knock landed (in the car's own frame) and how hard, so every machine throws bits off the right corner. */
	UPROPERTY() FVector_NetQuantize10 LastHit = FVector::ZeroVector;
	UPROPERTY() float LastDamage = 0.f;
	/** Bumped every knock so each one replicates. */
	UPROPERTY() uint8 Serial = 0;
};

/**
 * A car that can be knocked about: cruisers crashing into things, cars they ram, gunfire. The server keeps the health
 * (replicated); every machine shows it: panels flying off at each hard knock, smoke off the bonnet, then fire with
 * the paint scorching, then a wreck (the crumpled body mesh from Tools/Blender/build_vehicles.py --dented).
 *
 * Every knock also leaves a dent right where it landed, pushed in the way it was hit and as deep as it was hard, with
 * the paint scraped back to bare metal round it; grinding along a wall leaves scrapes. Dents build up (a second knock
 * in the same place goes deeper) and stay till the motor pool repairs the car. They're a short replicated list the
 * body's materials (M_FTOVehicle) turn into moved vertices, so every machine sees the same crumples.
 */
UCLASS(ClassGroup=(FTO), meta=(BlueprintSpawnableComponent))
class FTO_API UFTOVehicleDamage : public UActorComponent
{
	GENERATED_BODY()

public:
	UFTOVehicleDamage();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Every machine: the body to dent and scorch, and its beaten-up mesh. */
	void SetBody(UStaticMeshComponent* InBody, UStaticMesh* InDented);

	/** Server: a knock (a crash, a round) landing at At. Instigator: who's to blame. */
	void ApplyDamage(float Amount, const FVector& At, AController* Instigator);
	/** Server: good as new (the motor pool's been at it). */
	void Repair();

	/** Server: paint scraped off where the car grinds along something (At), without a knock. */
	void AddScrape(const FVector& At);

	const TArray<FFTODent>& GetDents() const { return Dents; }
	/** Dents a car's materials can show at once (M_FTOVehicle's DENTS in Tools/Unreal/create_materials.py). */
	static constexpr int32 MaxDents = 12;

	float GetHealth() const { return State.Health; }
	EFTOCarDamage GetStage() const;
	bool IsWrecked() const { return State.Health <= 0.f; }

	/** A citizen's car: the police writing it off costs the city chaos. */
	UPROPERTY(EditAnywhere, Category="Damage") bool bCitizensCar = true;

	/** Server: the moment it's written off. */
	FSimpleMulticastDelegate OnWrecked;

	static constexpr float DentedBelow = 60.f;
	static constexpr float SmokingBelow = 35.f;
	static constexpr float BurningBelow = 15.f;

protected:
	UFUNCTION() void OnRep_State();
	UFUNCTION() void OnRep_Dents();
	/** Server: a dent of Depth cm pressed in at world point At, pushed along Dir (merged into one nearby). */
	void AddDent(const FVector& At, const FVector& Dir, float Depth, float Radius, float Scrape);
	/** Every machine: hand the dents to the body's materials. */
	void ApplyDents();
	/** Every machine: look the part for the health (bits fly off for a fresh knock). */
	void ApplyState(bool bFreshKnock);
	/** The smoke and flames billowing off the bonnet. */
	void EnsurePlume();
	void TickPlume(float DeltaTime);
	/** Paint darkened by Amount (0 none, 1 charcoal). */
	void Scorch(float Amount);
	/** Where smoke and flames come from: the bonnet, in the body's frame. */
	FVector PlumeOrigin() const;
	FLinearColor PaintColor() const;

	UPROPERTY(ReplicatedUsing=OnRep_State) FFTOCarHealth State;
	UPROPERTY(ReplicatedUsing=OnRep_Dents) TArray<FFTODent> Dents;
	/** Every material on the body (paint, glass, lights), each told about the dents. */
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> DentMaterials;

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Pristine;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Dented;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Plume;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PlumeMaterials;
	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> FireLight;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> FireSound;

	/** The body's paint instances and their colours before any scorching. */
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PaintMaterials;
	TArray<FLinearColor> CleanPaint;
	float Scorched = 0.f;

	EFTOCarDamage Shown = EFTOCarDamage::Fine;
	uint8 SeenSerial = 0;
	bool bSeenState = false;
	float PlumeTime = 0.f;
	TWeakObjectPtr<AController> LastInstigator;

	static constexpr int32 SmokePuffs = 8;
	static constexpr int32 Flames = 5;
};
