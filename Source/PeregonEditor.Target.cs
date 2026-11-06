using UnrealBuildTool;

public class PeregonEditorTarget : TargetRules
{
	public PeregonEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "Peregon", "VehicleSimCore", "RoadGenCore", "PeregonEditor" });
	}
}
