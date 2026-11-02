using UnrealBuildTool;

// Ядро физики машины: чистый C++17 без зависимостей от Unreal.
// Те же исходники собираются через CMake для headless-тестов (Tests/VehicleSim).
public class VehicleSimCore : ModuleRules
{
	public VehicleSimCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		PublicDependencyModuleNames.Add("Core");
	}
}
