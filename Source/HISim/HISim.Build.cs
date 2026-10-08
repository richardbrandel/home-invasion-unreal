using UnrealBuildTool;

public class HISim : ModuleRules
{
	public HISim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		// HISim.h sits at the module root rather than in a Public/ folder, so UnrealBuildTool
		// does not publish it to dependents and HIUnreal gets "'HISim.h' file not found".
		// The module's own sources compile either way, which is why this only surfaces once
		// a second module includes the header.
		PublicIncludePaths.Add(ModuleDirectory);
	}
}
