#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FTOEditorLibrary.generated.h"

class USkeletalMesh;

/** Asset-pipeline operations Python can't do on its own. Editor only. */
UCLASS()
class UFTOEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * (Re)builds the mesh's physics asset with a capsule body per bone and limited joints,
	 * so ragdolls flop limb by limb. Creates <Mesh>_PhysicsAsset next to the mesh if needed.
	 * Returns the number of bodies (0 on failure).
	 */
	UFUNCTION(BlueprintCallable, Category="FTO|Editor")
	static int32 RebuildPhysicsAsset(USkeletalMesh* Mesh, float MinBoneSize = 2.f);
};
