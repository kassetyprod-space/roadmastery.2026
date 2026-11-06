using UnrealBuildTool;

public class PeregonTarget : TargetRules
{
	public PeregonTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "Peregon", "VehicleSimCore", "RoadGenCore" });
	}
}
