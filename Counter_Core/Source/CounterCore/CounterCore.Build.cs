using UnrealBuildTool;

public class CounterCore : ModuleRules
{
	public CounterCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayTasks",
			"AIModule"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
