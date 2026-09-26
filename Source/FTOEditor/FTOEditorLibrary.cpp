#include "FTOEditorLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "UObject/Package.h"

int32 UFTOEditorLibrary::RebuildPhysicsAsset(USkeletalMesh* Mesh, float MinBoneSize)
{
	if (!Mesh)
	{
		return 0;
	}

	UPhysicsAsset* PhysicsAsset = Mesh->GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		const FString PackageName = Mesh->GetOutermost()->GetName() + TEXT("_PhysicsAsset");
		UPackage* Package = CreatePackage(*PackageName);
		PhysicsAsset = NewObject<UPhysicsAsset>(Package, FName(*FPackageName::GetShortName(PackageName)), RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(PhysicsAsset);
	}

	// Start clean: generation appends bodies.
	PhysicsAsset->SkeletalBodySetups.Empty();
	PhysicsAsset->ConstraintSetup.Empty();
	PhysicsAsset->UpdateBodySetupIndexMap();
	PhysicsAsset->UpdateBoundsBodiesArray();

	FPhysAssetCreateParams Params;
	Params.MinBoneSize = MinBoneSize;
	Params.GeomType = EFG_Sphyl;
	Params.VertWeight = EVW_DominantWeight;
	Params.bCreateConstraints = true;
	Params.AngularConstraintMode = ACM_Limited;
	Params.bDisableCollisionsByDefault = true;

	FText Error;
	if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(PhysicsAsset, Mesh, Params, Error, /*bSetToMesh*/ true, /*bShowProgress*/ false))
	{
		UE_LOG(LogTemp, Error, TEXT("FTO: physics asset for %s failed: %s"), *Mesh->GetName(), *Error.ToString());
		return 0;
	}

	PhysicsAsset->MarkPackageDirty();
	Mesh->MarkPackageDirty();
	return PhysicsAsset->SkeletalBodySetups.Num();
}
