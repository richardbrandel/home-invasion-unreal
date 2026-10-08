using UnrealBuildTool;
using System.Collections.Generic;

public class HIUnrealTarget : TargetRules
{
	public HIUnrealTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "HIUnreal", "HISim" });
	}
}
