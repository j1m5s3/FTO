#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "FTOPlayerState.generated.h"

/** Per-officer replicated state. */
UCLASS()
class FTO_API AFTOPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server only. */
	void SetBadgeIndex(int32 NewIndex);

	UFUNCTION(BlueprintPure, Category="FTO")
	int32 GetBadgeIndex() const { return BadgeIndex; }

	/** Uniform colour for this officer's badge slot. */
	UFUNCTION(BlueprintPure, Category="FTO")
	FLinearColor GetOfficerColor() const;

	static FLinearColor ColorForBadge(int32 Index);

protected:
	UPROPERTY(ReplicatedUsing=OnRep_BadgeIndex, BlueprintReadOnly, Category="FTO")
	int32 BadgeIndex = 0;

	UFUNCTION()
	void OnRep_BadgeIndex();
};
