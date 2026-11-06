using UnrealBuildTool;

// Генератор трассы: чистый C++17 без зависимостей от Unreal.
// Те же исходники собираются через CMake для headless-тестов (Tests/RoadGen).
public class RoadGenCore : ModuleRules
{
	public RoadGenCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		PublicDependencyModuleNames.Add("Core");
	}
}
