using UnrealBuildTool;

// Presentation layer only: shows the UISim state, no game logic (docs/DESIGN.md 4.3).
public class UnderInvestigation : ModuleRules
{
	public UnderInvestigation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "UMG", "Slate", "SlateCore", "UISimBridge"
		});
	}
}
