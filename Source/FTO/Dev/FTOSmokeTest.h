#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FTOSmokeTest.generated.h"

class ACameraActor;
class AFTOCharacter;
class AFTOCityGenerator;
class AFTOCruiser;
class AFTOGameMode;
class AFTOPedestrian;
class AFTOPerp;
class AFTOTrafficCar;
class UInstancedStaticMeshComponent;
class APawn;
class APlayerController;

/**
 * Development-only scripted tour for quick visual checks without a human at the keyboard.
 *
 * Launch the game with -FTOSmokeTest to take a set of screenshots (menu, lobby, officer,
 * whistle, on duty, aerial city, street, incident, report card, sidewalk, traffic and its
 * drivers, driving with a suspect in the back, the seat view, riding shotgun)
 * into Saved/Screenshots/SmokeTest and log the average FPS.
 * Add -FTOSmokeTestQuit to exit when done, and -FTOSmokeTag=name to prefix the shots when
 * running several instances (host + client).
 */
UCLASS(NotPlaceable, Transient)
class FTO_API AFTOSmokeTest : public AActor
{
	GENERATED_BODY()

public:
	AFTOSmokeTest();

	virtual void Tick(float DeltaSeconds) override;

	static bool IsRequested();

protected:
	virtual void BeginPlay() override;

	struct FStep
	{
		FString Name;
		/** Seconds to wait after this step before running the next one. */
		float Delay = 1.f;
		TFunction<void()> Action;
		/** If set, the next step runs as soon as this is true (Delay is then the longest it waits). */
		TFunction<bool()> Until;
	};

	void BuildSteps();
	void AddStep(const TCHAR* Name, float Delay, TFunction<void()> Action);
	/** A step that waits until Until() is true (or MaxSeconds pass), e.g. for the other player. */
	void AddWait(const TCHAR* Name, float MaxSeconds, TFunction<bool()> Until);
	void AddShot(const TCHAR* Name, float Delay, bool bShowUI = true);

	bool AreShadersReady() const;
	void Shot(const TCHAR* Name, bool bShowUI = true);
	void ViewFrom(const FVector& Location, const FVector& LookAt);
	void SetHUDVisible(bool bVisible);

	APlayerController* GetPC() const;
	APawn* GetPawn() const;
	AFTOGameMode* GetAuthGameMode() const;
	AFTOCityGenerator* GetCity() const;

	TArray<FStep> Steps;

	UPROPERTY(Transient) TObjectPtr<ACameraActor> Camera;
	UPROPERTY(Transient) TObjectPtr<AFTOCruiser> TestCruiser;
	/** A stand-in second officer riding shotgun (standalone/host only). */
	UPROPERTY(Transient) TObjectPtr<AFTOCharacter> TestPassenger;

	float ReadyTime = -1.f;
	float StableSince = -1.f;
	int32 NextStep = 0;
	float NextStepTime = 0.f;
	/** The last step's early-out, if it had one. */
	TFunction<bool()> WaitUntil;
	int32 FramesSinceReady = 0;

	/** Citizens stood in the road for the cruiser, and the one who gets tackled. */
	TArray<TWeakObjectPtr<AFTOPedestrian>> Pins;
	TWeakObjectPtr<AFTOPedestrian> TackleTarget;
	float ChaosBefore = 0.f;

	/** The suspect in the arrest checks, and the getaway car. */
	TWeakObjectPtr<AFTOPerp> TestPerp;
	TWeakObjectPtr<AFTOTrafficCar> TestCar;
	/** Host: the shoplifter staged for the client to arrest. */
	TWeakObjectPtr<AFTOPerp> RiderPerp;
	/** Stages Crime Ahead cm in front of the local officer (in the street, the perp facing them) and returns its perp. */
	AFTOPerp* StagePerp(FName Crime, float Ahead);
	/** Point the local officer's crosshair at Target (through their over-the-shoulder camera, as a player would). */
	void AimAt(const FVector& Target);
	/** The ground under a spot (a trace down), or the spot's own height if there's nothing. */
	float GroundZ(const FVector& At) const;
	/** The instance of a city Mesh nearest Near (skipping broken ones): its component, index and transform. */
	bool FindCityInstance(const TCHAR* Mesh, const FVector& Near, UInstancedStaticMeshComponent*& OutISM, int32& OutIndex, FTransform& OutTransform) const;
	/** The destruction checks: what's being shot at or driven into, and where from. */
	TWeakObjectPtr<UInstancedStaticMeshComponent> TestISM;
	int32 TestInstance = INDEX_NONE;
	FVector TestTarget = FVector::ZeroVector;
	FVector TestAway = FVector::ZeroVector;
	float CrashHealthBefore = 0.f;
	/** The nearest perp at a Crime incident to the local officer. */
	AFTOPerp* FindNearestPerp(FName Crime) const;
	/** Films the officer and a suspect side on. */
	void ViewArrest(const AActor* Suspect, float Side = 1.f);

	FVector IncidentSpot = FVector::ZeroVector;
	FVector WalkDirection = FVector::ForwardVector;
	bool bWalkOfficer = false;
};
