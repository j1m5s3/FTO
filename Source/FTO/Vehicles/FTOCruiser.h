#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Interaction/FTOInteractable.h"
#include "FTOCruiser.generated.h"

class AFTOCharacter;
class UBoxComponent;
class UCameraComponent;
class UAudioComponent;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
struct FInputActionValue;

/** What everyone else sees of a cruiser. */
USTRUCT()
struct FFTOCruiserState
{
	GENERATED_BODY()

	UPROPERTY() FVector_NetQuantize10 Location = FVector::ZeroVector;
	UPROPERTY() float Yaw = 0.f;
	/** Forward speed, cm/s (negative when reversing). */
	UPROPERTY() float Speed = 0.f;
	/** -1..1, for the front wheels. */
	UPROPERTY() float Steer = 0.f;
};

/**
 * A drivable police cruiser with arcade handling.
 *
 * Networking is driver-authoritative (fine for a co-op party game, and it keeps driving
 * snappy for clients): whoever drives simulates locally and streams their transform to the
 * server, which replicates it to everyone else, who interpolate.
 */
UCLASS()
class FTO_API AFTOCruiser : public APawn, public IFTOInteractable
{
	GENERATED_BODY()

public:
	AFTOCruiser();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;

	/** Server: paint the door stripe (badge colour of the officer this cruiser belongs to). */
	void SetStripeColor(const FLinearColor& Color);

	// IFTOInteractable (entering)
	virtual bool CanInteract(const AFTOCharacter* Officer) const override;
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual void Interact(AFTOCharacter* Officer) override;
	virtual FVector GetInteractLocation() const override { return GetActorLocation(); }
	virtual float GetInteractRange() const override { return 380.f; }

	UFUNCTION(BlueprintPure, Category="Cruiser") float GetSpeed() const { return ForwardSpeed; }
	UFUNCTION(BlueprintPure, Category="Cruiser") bool IsSirenOn() const { return bSiren; }
	UFUNCTION(BlueprintPure, Category="Cruiser") bool HasDriver() const { return Driver != nullptr; }
	AFTOCharacter* GetDriver() const { return Driver; }

	/** Server: switch the lights and siren. */
	void SetSiren(bool bOn) { bSiren = bOn; }

	/** Server: kick the driver out (also what E does from inside). */
	void RequestExit() { OnExit(); }

	/** Dev/testing: drive without a human on the controls. */
	void SetAutopilot(bool bEnable, float Throttle = 0.f, float Steer = 0.f)
	{
		bAutopilot = bEnable;
		ThrottleInput = Throttle;
		SteerInput = Steer;
	}

	// ---- Handling (cm, seconds, degrees) ----
	UPROPERTY(EditDefaultsOnly, Category="Handling") float MaxSpeed = 2600.f;
	UPROPERTY(EditDefaultsOnly, Category="Handling") float MaxReverseSpeed = 900.f;
	UPROPERTY(EditDefaultsOnly, Category="Handling") float Acceleration = 1500.f;
	UPROPERTY(EditDefaultsOnly, Category="Handling") float BrakeDeceleration = 3200.f;
	UPROPERTY(EditDefaultsOnly, Category="Handling") float CoastDeceleration = 400.f;
	UPROPERTY(EditDefaultsOnly, Category="Handling") float MaxYawRate = 120.f;
	/** How quickly sideways sliding dies out; lower with the handbrake = drifting. */
	UPROPERTY(EditDefaultsOnly, Category="Handling") float Grip = 9.f;
	UPROPERTY(EditDefaultsOnly, Category="Handling") float HandbrakeGrip = 1.2f;

	/** Siren pulls over offending cars within this distance ahead. */
	UPROPERTY(EditDefaultsOnly, Category="Siren") float SirenReach = 1800.f;

	static constexpr float RideHeight = 95.f;

protected:
	virtual void BeginPlay() override;

	// Input
	void OnMove(const FInputActionValue& Value);
	void OnMoveReleased(const FInputActionValue& Value);
	void OnHandbrake(const FInputActionValue& Value);
	void OnHandbrakeReleased(const FInputActionValue& Value);
	void OnExit();
	void OnSiren();

	/** Arcade car step; used by whoever owns the simulation. */
	void Simulate(float DeltaSeconds);
	void FollowGround();
	void UpdateCosmetics(float DeltaSeconds);
	void ServerSirenTick();

	UFUNCTION(Server, Unreliable)
	void ServerMove(FVector_NetQuantize10 Location, float Yaw, float Speed, float Lateral, float Steer);

	UFUNCTION(Server, Reliable)
	void ServerExit();

	UFUNCTION(Server, Reliable)
	void ServerSetSiren(bool bOn);

	void OnHorn();

	UFUNCTION(Server, Unreliable)
	void ServerHorn();
	float NextHornTime = 0.f;

	UFUNCTION() void OnRep_Siren();
	UFUNCTION() void OnRep_StripeColor();

	void ExitDriver();
	bool IsSimulatingLocally() const;

	// ---- Components ----
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UBoxComponent> Collision;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category="Components") TArray<TObjectPtr<UStaticMeshComponent>> Wheels;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> LightRed;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> LightBlue;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UAudioComponent> EngineAudio;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UAudioComponent> SirenAudio;

	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PaintMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RedMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BlueMaterial;

	// ---- Replicated ----
	UPROPERTY(Replicated) FFTOCruiserState NetState;
	UPROPERTY(ReplicatedUsing=OnRep_Siren) bool bSiren = false;
	UPROPERTY(ReplicatedUsing=OnRep_StripeColor) FLinearColor StripeColor = FLinearColor(0.1f, 0.35f, 1.f);
	/** The officer inside (server-owned; replicated so HUDs can tell). */
	UPROPERTY(Replicated) TObjectPtr<AFTOCharacter> Driver;

	// ---- Simulation ----
	float ThrottleInput = 0.f;
	float SteerInput = 0.f;
	bool bHandbrake = false;
	bool bAutopilot = false;
	float ForwardSpeed = 0.f;
	float LateralSpeed = 0.f;
	float SteerVisual = 0.f;
	float WheelSpin = 0.f;
	float SendAccumulator = 0.f;
	float SirenScanAccumulator = 0.f;
	float LastNetUpdateTime = 0.f;
	int32 RemoteMoveCount = 0;
};
