#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FTOSmokeTest.generated.h"

class ACameraActor;
class AFTOCityGenerator;
class AFTOCruiser;
class AFTOGameMode;
class APawn;
class APlayerController;

/**
 * Development-only scripted tour for quick visual checks without a human at the keyboard.
 *
 * Launch the game with -FTOSmokeTest to take a set of screenshots (menu, lobby, officer,
 * whistle, on duty, aerial city, street, incident, report card, sidewalk, traffic, driving)
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
	};

	void BuildSteps();
	void AddStep(const TCHAR* Name, float Delay, TFunction<void()> Action);
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

	float ReadyTime = -1.f;
	float StableSince = -1.f;
	int32 NextStep = 0;
	float NextStepTime = 0.f;
	int32 FramesSinceReady = 0;

	FVector IncidentSpot = FVector::ZeroVector;
	FVector WalkDirection = FVector::ForwardVector;
	bool bWalkOfficer = false;
};
