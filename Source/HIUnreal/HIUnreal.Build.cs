using UnrealBuildTool;

public class HIUnreal : ModuleRules
{
	public HIUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HISim" });
		// RenderCore for `GWhiteTexture`, which the minimap's filled sight fan needs: there is
		// no FCanvasTriangleItem overload that takes no texture, and the engine's own
		// AHUD::DrawRect reaches for the same global. Found by the LINKER, not by reading --
		// the compile succeeded and `ld.lld: undefined symbol: GWhiteTexture` is what said so.
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore" });
	}
}
