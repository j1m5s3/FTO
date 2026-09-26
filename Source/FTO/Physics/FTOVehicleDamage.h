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
 * (replicated); every machine shows it: panels flying off at each hard knock, the dented body mesh (from
 * Tools/Blender/build_vehicles.py --dented), smoke off the bonnet, then fire with the paint scorching, then a wreck.
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
