using UnrealBuildTool;

// Модуль только для редактора: при запуске создаёт контент, который нельзя завести без .uasset
// (мастер-материал M_PSX2). В игру не попадает.
public class PeregonEditor : ModuleRules
{
	public PeregonEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"AssetTools",
			"MaterialEditor",
			"EditorScriptingUtilities"
		});
	}
}
