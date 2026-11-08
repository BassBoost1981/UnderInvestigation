using System.IO;
using UnrealBuildTool;

// Links the prebuilt UISim core. Build it first (Release, see core/CMakeLists.txt):
//   cmake -S core -B core/build -G "Visual Studio 17 2022" -A x64
//   cmake --build core/build --config Release
public class UISimBridge : ModuleRules
{
	public UISimBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = true; // the core reports data errors with std exceptions

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		// Repository layout: <repo>/unreal/Plugins/UISimBridge/Source/UISimBridge -> <repo>/core
		string CoreDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "..", "core"));
		string CoreLib = Path.Combine(CoreDir, "build", "Release", "uisim.lib");
		if (!File.Exists(CoreLib))
		{
			throw new BuildException("UISim core library not found at " + CoreLib + ". Build core/ in Release first.");
		}
		PrivateIncludePaths.Add(Path.Combine(CoreDir, "include"));
		PublicAdditionalLibraries.Add(CoreLib);
	}
}
