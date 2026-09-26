#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FTOSmokeTest.generated.h"

class ACameraActor;

/**
 * Development-only scripted tour for quick visual checks without a human at the keyboard.
 *
 * Launch the game with -FTOSmokeTest to take a set of screenshots (roll call, on duty,
 * aerial city, street level, an incident) into Saved/Screenshots/SmokeTest.
 * Add -FTOSmokeTestQuit to exit when it's done.
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
	bool AreShadersReady() const;
	void RunStep(int32 Step);
	void Shot(const TCHAR* Name, bool bShowUI = true);
	void ViewFrom(const FVector& Location, const FVector& LookAt);

	UPROPERTY(Transient) TObjectPtr<ACameraActor> Camera;

	float ReadyTime = -1.f;
	float StableSince = -1.f;
	int32 FramesSinceReady = 0;
	int32 MenuShotPhase = 0;
	float MenuShotTime = 0.f;
	int32 NextStep = 0;
	float NextStepTime = 0.f;
	FVector IncidentSpot = FVector::ZeroVector;
	UPROPERTY(Transient) TObjectPtr<class AFTOCruiser> TestCruiser;
	FVector WalkDirection = FVector::ForwardVector;
	bool bWalkOfficer = false;
};
