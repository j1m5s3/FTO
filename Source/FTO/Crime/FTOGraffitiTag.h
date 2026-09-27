#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FTOGraffitiTag.generated.h"

class UTextRenderComponent;

/**
 * A tagger's handiwork on a wall, sprayed a letter at a time while they're at it (server sets the progress, every
 * machine draws it). It stays up after they've gone: the city gets scruffier as the shift wears on, up to a limit.
 */
UCLASS(NotPlaceable)
class FTO_API AFTOGraffitiTag : public AActor
{
	GENERATED_BODY()

public:
	AFTOGraffitiTag();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: a fresh tag on the wall this actor faces out of (its X axis points away from the wall). */
	static AFTOGraffitiTag* Spray(UWorld* World, const FVector& OnWall, const FVector& WallNormal, int32 Seed);

	/** Server: 0-1, how much of it's been sprayed. */
	void SetProgress(float InProgress);
	float GetProgress() const { return Progress; }

	/** Tags left on walls at once; the oldest is scrubbed off to make room. */
	static constexpr int32 MaxTags = 16;

protected:
	UFUNCTION() void OnRep_Tag();

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UTextRenderComponent> Letters;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UTextRenderComponent> Outline;

	UPROPERTY(ReplicatedUsing=OnRep_Tag) FString Word;
	UPROPERTY(ReplicatedUsing=OnRep_Tag) FColor Paint = FColor::White;
	UPROPERTY(ReplicatedUsing=OnRep_Tag) float Progress = 0.f;
};
