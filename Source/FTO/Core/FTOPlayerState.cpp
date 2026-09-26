#include "Core/FTOPlayerState.h"
#include "Core/FTOCharacter.h"
#include "Net/UnrealNetwork.h"

void AFTOPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFTOPlayerState, BadgeIndex);
}

void AFTOPlayerState::SetBadgeIndex(int32 NewIndex)
{
	BadgeIndex = NewIndex;
	OnRep_BadgeIndex();
}

FLinearColor AFTOPlayerState::GetOfficerColor() const
{
	return ColorForBadge(BadgeIndex);
}

FLinearColor AFTOPlayerState::ColorForBadge(int32 Index)
{
	static const FLinearColor Colors[] =
	{
		FLinearColor(0.10f, 0.35f, 1.00f), // blue
		FLinearColor(1.00f, 0.25f, 0.25f), // red
		FLinearColor(0.20f, 0.85f, 0.35f), // green
		FLinearColor(1.00f, 0.80f, 0.10f), // yellow
	};
	return Colors[FMath::Abs(Index) % UE_ARRAY_COUNT(Colors)];
}

void AFTOPlayerState::OnRep_BadgeIndex()
{
	if (AFTOCharacter* Officer = GetPawn<AFTOCharacter>())
	{
		Officer->RefreshOfficerColor();
	}
}
