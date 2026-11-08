using UnrealBuildTool;

public class UnderInvestigationTarget : TargetRules
{
	public UnderInvestigationTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("UnderInvestigation");
	}
}
