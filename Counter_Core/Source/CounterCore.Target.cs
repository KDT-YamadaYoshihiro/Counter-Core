using UnrealBuildTool;

public class CounterCoreTarget : TargetRules
{
	public CounterCoreTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("CounterCore");
	}
}
