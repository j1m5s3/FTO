#include "Core/FTOMutators.h"
#include "Core/FTOGameState.h"
#include "Engine/World.h"

const TArray<FName>& FTOMutators::All()
{
	static const TArray<FName> Mutators = { TEXT("LowGravity"), TEXT("BouncyCars"), TEXT("HotDogs"), TEXT("BigHeads") };
	return Mutators;
}

FName FTOMutators::Active(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const AFTOGameState* GS = World ? World->GetGameState<AFTOGameState>() : nullptr;
	return GS ? GS->GetMutator() : NAME_None;
}

bool FTOMutators::Is(const UObject* WorldContext, FName Which)
{
	return Which != NAME_None && Active(WorldContext) == Which;
}

FString FTOMutators::Describe(FName Which)
{
	if (Which == TEXT("LowGravity")) return TEXT("TODAY: LOW GRAVITY. Everything's a bit floaty. Mind your jumps.");
	if (Which == TEXT("BouncyCars")) return TEXT("TODAY: BOUNCY CARS. The motor pool fitted hydraulics. Nobody knows why.");
	if (Which == TEXT("HotDogs"))    return TEXT("TODAY: HOT DOG DAY. Every crook in town is dressed as a hot dog. Every one.");
	if (Which == TEXT("BigHeads"))   return TEXT("TODAY: BIG HEADS. Something in the water. Hats won't fit.");
	return FString();
}
