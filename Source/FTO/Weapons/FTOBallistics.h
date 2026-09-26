#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Weapons/FTOWeapons.h"
#include "FTOBallistics.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;

/** Bullets, pellets and taser probes are traced on this channel (DefaultEngine.ini "Projectile"). */
#define ECC_FTOProjectile ECC_GameTraceChannel1

/**
 * Every round in flight, flown for real: it leaves the muzzle at the weapon's velocity, drops under gravity, slows
 * with air drag, takes time to get there, punches through glass, and glances off walls it meets at a shallow angle.
 * The server's copy of each round decides what it hits (people go down: see FTOImpact::Shot); every machine flies
 * its own copy of the same round (same seed) to draw the tracer, the muzzle flash and the puffs where it lands.
 */
UCLASS()
class FTO_API UFTOBallistics : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Fire one shot (all of its pellets) from Origin along Aim. bAuthoritative rounds knock people down (server only);
	 * bShow draws them and plays the bang.
	 */
	void Fire(AActor* Shooter, EFTOWeapon Weapon, const FVector& Origin, const FVector& Aim, int32 Seed, bool bAimed, bool bAuthoritative, bool bShow);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

	static UFTOBallistics* Get(const UWorld* World);

	/** Rounds in flight right now. */
	int32 NumInFlight() const { return Rounds.Num(); }

private:
	struct FRound
	{
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		/** Where it left the muzzle (a taser's wire runs back to here). */
		FVector Origin = FVector::ZeroVector;
		FVector LastDrawn = FVector::ZeroVector;
		float Travelled = 0.f;
		EFTOWeapon Weapon = EFTOWeapon::None;
		TWeakObjectPtr<AActor> Shooter;
		bool bAuthoritative = false;
		bool bShow = false;
		int32 Bounces = 0;
		int32 Tracer = INDEX_NONE;
	};

	/** Flies a round Dt further; false once it's done (stopped in something, or spent). */
	bool Advance(FRound& Round, float Dt);
	/** It met something solid: knock them down, glance off, or stop. False if it stops. */
	bool Land(FRound& Round, const FHitResult& Hit);

	// ---- Looks: pooled glowing streaks, puffs and a muzzle flash, owned by one transient actor ----
	AActor* GetFXHost();
	int32 TakeTracer(EFTOWeapon Weapon);
	void DrawTracer(FRound& Round);
	void ReleaseTracer(int32 Index);
	void Puff(const FVector& At, const FLinearColor& Color, float Size, float Life, float Glow = 0.f);
	void Flash(const FVector& At);
	void TickLooks(float DeltaTime);

	TArray<FRound> Rounds;

	UPROPERTY(Transient) TObjectPtr<AActor> FXHost;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Tracers;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> TracerMaterials;
	TArray<bool> TracerInUse;

	struct FPuff
	{
		float Age = 0.f;
		float Life = 0.f;
		float Size = 0.f;
	};
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> PuffMeshes;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PuffMaterials;
	TArray<FPuff> Puffs;
	int32 NextPuff = 0;

	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> FlashLight;
	float FlashUntil = 0.f;
};
