using UnrealBuildTool;

public class Peregon : ModuleRules
{
	public Peregon(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"PhysicsCore",
			"VehicleSimCore",
			"RoadGenCore",
			"ProceduralMeshComponent",
			"Slate",
			"SlateCore",
			"Json",
			"AudioMixer"
		});
	}
}
