using UnrealBuildTool;

public class CounterCoreEditorTarget : TargetRules
{
	public CounterCoreEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("CounterCore");
	}
}
