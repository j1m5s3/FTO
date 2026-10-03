#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "FTOJuice.generated.h"

class USoundBase;

/**
 * Screen shake: a camera modifier on every local player's camera that jolts the view by its "trauma" (0-1), which
 * big moments add to and which dies away by itself. Shake goes as trauma squared, so small knocks barely register
 * and big ones really rattle.
 */
UCLASS()
class FTO_API UFTOShakeModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	void AddTrauma(float Amount) { Trauma = FMath::Clamp(Trauma + Amount, 0.f, 1.f); }
	float GetTrauma() const { return Trauma; }

protected:
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

	float Trauma = 0.f;
	float Time = 0.f;
};

/** Making the big moments feel big: shake, slow motion. */
namespace FTOJuice
{
	/** Every machine: rattle the local cameras by Amount (0-1), less the further they are from At (nothing past Radius). */
	FTO_API void ShakeAt(const UWorld* World, const FVector& At, float Amount, float Radius);
	/** How much a sound in the world should shake things (and how far): explosions, crashes, walls coming down... */
	FTO_API bool ShakeFor(const USoundBase* Sound, float& OutAmount, float& OutRadius);
	/** The local player's current trauma (tests). */
	FTO_API float GetTrauma(const UWorld* World);
	/** Server: the whole game in slow motion (Scale) for RealSeconds, unless there's been one in the last 15 seconds. */
	FTO_API void SlowMo(UWorld* World, float Scale, float RealSeconds);
	/** When the last slow-mo started (server, world time; tests). */
	FTO_API float LastSlowMo(const UWorld* World);
}
