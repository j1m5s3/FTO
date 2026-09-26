using UnrealBuildTool;

/** Editor-only helpers for the art pipeline (called from Tools/Unreal/*.py). */
public class FTOEditor : ModuleRules
{
	public FTOEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "PhysicsUtilities", "AssetRegistry" });
	}
}
