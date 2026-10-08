using UnrealBuildTool;
using System.Collections.Generic;

public class HIUnrealEditorTarget : TargetRules
{
	public HIUnrealEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "HIUnreal", "HISim" });
	}
}
