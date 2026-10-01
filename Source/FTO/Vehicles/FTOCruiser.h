#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Interaction/FTOInteractable.h"
#include "Vehicles/FTOVehicleSeats.h"
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
class UFTOVehicleDamage;
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
 * A drivable police cruiser with arcade handling, a real cabin (the officers sit visibly inside,
 * among the MDT, radio, radar, shotgun rack and cage), a chase or seat-view camera, and a
 * passenger seat so a second officer can ride shotgun.
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

	// IFTOInteractable (driving, or riding shotgun when someone's already at the wheel)
	virtual bool CanInteract(const AFTOCharacter* Officer) const override;
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual void Interact(AFTOCharacter* Officer) override;
	virtual FVector GetInteractLocation() const override { return GetActorLocation(); }
	virtual float GetInteractRange() const override { return 380.f; }

	UFUNCTION(BlueprintPure, Category="Cruiser") float GetSpeed() const { return ForwardSpeed; }
	UFUNCTION(BlueprintPure, Category="Cruiser") bool IsSirenOn() const { return bSiren; }
	UFUNCTION(BlueprintPure, Category="Cruiser") bool HasDriver() const { return Driver != nullptr; }
	AFTOCharacter* GetDriver() const { return Driver; }
	AFTOCharacter* GetPassenger() const { return Passenger; }

	/** The body mesh; seats are sockets on it. */
	USceneComponent* GetSeatParent() const;

	/** Server: switch the lights and siren. */
	void SetSiren(bool bOn) { bSiren = bOn; }

	/** Server: kick the driver out (also what E does from inside). */
	void RequestExit() { OnExit(); }

	/** Server: let an officer out of whichever seat they're in. */
	void LetOut(AFTOCharacter* Officer);

	/** Local: chase camera or the view from the driver's seat. */
	void SetInteriorView(bool bInterior);
	bool IsInteriorView() const;

	/** Dev/testing: drive without a human on the controls. */
	void SetAutopilot(bool bEnable, float Throttle = 0.f, float Steer = 0.f)
	{
		bAutopilot = bEnable;
		ThrottleInput = Throttle;
		SteerInput = Steer;
	}

	UFTOVehicleDamage* GetDamage() const { return Damage; }

	/** Dev/testing: already doing Speed (cm/s) straight ahead. */
	void Launch(float Speed)
	{
		ForwardSpeed = Speed;
		LateralSpeed = 0.f;
	}

	/** Dev/testing: stop dead on the spot (for photos). */
	void StopDead()
	{
		ForwardSpeed = LateralSpeed = 0.f;
		ThrottleInput = SteerInput = 0.f;
	}

	/** Crashes below this speed (cm/s, into whatever it hit) just bump; above, the car takes damage by the speed. */
	UPROPERTY(EditDefaultsOnly, Category="Damage") float CrashSpeed = 450.f;
	/** Damage per cm/s over CrashSpeed (cruisers are built to take a beating). */
	UPROPERTY(EditDefaultsOnly, Category="Damage") float CrashDamagePerSpeed = 0.022f;
	/** Seconds a written-off cruiser sits empty before the motor pool fetches it back to the lot. */
	UPROPERTY(EditDefaultsOnly, Category="Damage") float MotorPoolSeconds = 20.f;

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

	/** Degrees of look per unit of mouse/stick input (matches the on-foot camera). */
	UPROPERTY(EditDefaultsOnly, Category="Camera") float LookRate = 2.5f;

	static constexpr float RideHeight = 95.f;

protected:
	virtual void BeginPlay() override;

	// Input
	void OnMove(const FInputActionValue& Value);
	void OnMoveReleased(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnHandbrake(const FInputActionValue& Value);
	void OnHandbrakeReleased(const FInputActionValue& Value);
	void OnExit();
	void OnSiren();
	void OnToggleCamera();

	/** Arcade car step; used by whoever owns the simulation. */
	void Simulate(float DeltaSeconds);
	void FollowGround();
	void UpdateCosmetics(float DeltaSeconds);
	void UpdateViews(float DeltaSeconds);
	void ServerSirenTick();

	UFUNCTION(Server, Unreliable)
	void ServerMove(FVector_NetQuantize10 Location, float Yaw, float Speed, float Lateral, float Steer);

	/**
	 * Whoever simulates the car: it's hit Victim at Velocity. Anyone who can be knocked over goes flying (the
	 * server decides, see FTOImpact) and the car ploughs on through them; returns false for walls and cars.
	 */
	bool BowlOver(AActor* Victim, const FVector& Velocity);
	UFUNCTION(Server, Reliable)
	void ServerBowlOver(AActor* Victim, FVector_NetQuantize10 Velocity);
	/** People we've just hit, passed through until then (they're busy flying). */
	TArray<TPair<TWeakObjectPtr<AActor>, float>> BowledOver;

	/**
	 * Whoever simulates the car: it's hit a piece of the city (Hit). Going fast enough to break it, it's knocked
	 * flying (the driver's machine tucks it away at once, the server breaks it for everyone) and the car ploughs on,
	 * a little slower; returns false otherwise.
	 */
	bool BreakThrough(const FHitResult& Hit, FVector& Velocity);
	UFUNCTION(Server, Reliable)
	void ServerBreakThrough(FName Component, int32 Instance, FVector_NetQuantize Hit, FVector_NetQuantize10 Push);
	/** Whoever simulates the car: it's run into something solid at Into cm/s. The car (and a car it hit) takes the knock. */
	void Crash(const FHitResult& Hit, float Into);
	UFUNCTION(Server, Reliable)
	void ServerCrash(AActor* Other, float Into, FVector_NetQuantize At);
	/** Things we've just broken through, passed through until then (they're being tucked away). */
	TArray<TPair<TWeakObjectPtr<UPrimitiveComponent>, float>> BrokenThrough;
	float NextCrashTime = 0.f;

	/** Server: a written-off cruiser left empty goes back to the lot, repaired. */
	void TickMotorPool(float DeltaSeconds);
	FTransform HomeTransform;
	float AbandonedFor = 0.f;

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
	void ExitPassenger();
	/** A clear spot beside the car on one side (+1 right, -1 left), or the other side if blocked. */
	FVector FindExitSpot(float Side) const;
	bool IsSimulatingLocally() const;

	// ---- Components ----
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UBoxComponent> Collision;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category="Components") TArray<TObjectPtr<UStaticMeshComponent>> Wheels;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> LightRed;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> LightBlue;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UCameraComponent> Camera;
	/** Eye level in the driver's seat, looking out over the dash. */
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UCameraComponent> InteriorCamera;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UAudioComponent> EngineAudio;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UAudioComponent> SirenAudio;
	/** The dentable paint (M_FTOVehicle). */
	UPROPERTY() TObjectPtr<UMaterialInterface> VehicleMaterial;
	/** Grinding along a wall: scraped paint (throttled; the driver's machine tells the server). */
	void Scrape(const FHitResult& Hit);
	UFUNCTION(Server, Unreliable) void ServerScrape(FVector_NetQuantize At);
	float NextScrapeTime = 0.f;
	/** Tyres squealing in a slide (UpdateSkid). */
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UAudioComponent> SkidAudio;
	void UpdateSkid(float DeltaSeconds);
	FVector SkidLastLocation = FVector::ZeroVector;
	FVector SkidVelocity = FVector::ZeroVector;
	float SkidLevel = 0.f;
	/** Dents, smoke, fire, write-offs (the beaten-up body from build_vehicles.py --dented). */
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UFTOVehicleDamage> Damage;
	UPROPERTY() TObjectPtr<UStaticMesh> DentedMesh;

	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PaintMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RedMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BlueMaterial;

	// ---- Replicated ----
	UPROPERTY(Replicated) FFTOCruiserState NetState;
	UPROPERTY(ReplicatedUsing=OnRep_Siren) bool bSiren = false;
	UPROPERTY(ReplicatedUsing=OnRep_StripeColor) FLinearColor StripeColor = FLinearColor(0.1f, 0.35f, 1.f);
	/** The officer at the wheel (server-owned; replicated so HUDs can tell). */
	UPROPERTY(Replicated) TObjectPtr<AFTOCharacter> Driver;
	/** The officer riding shotgun, if any. */
	UPROPERTY(Replicated) TObjectPtr<AFTOCharacter> Passenger;

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

	// ---- Local view ----
	float LookYaw = 0.f;
	float LookPitch = 0.f;
	float LastLookTime = -10.f;
	/** Whose head we've hidden for the seat view (only ever on this machine). */
	TWeakObjectPtr<AFTOCharacter> HiddenHead;
};
