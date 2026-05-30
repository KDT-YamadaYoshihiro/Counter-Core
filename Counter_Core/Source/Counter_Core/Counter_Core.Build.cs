// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Counter_Core : ModuleRules
{
	public Counter_Core(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Counter_Core",
			"Counter_Core/Variant_Platforming",
			"Counter_Core/Variant_Platforming/Animation",
			"Counter_Core/Variant_Combat",
			"Counter_Core/Variant_Combat/AI",
			"Counter_Core/Variant_Combat/Animation",
			"Counter_Core/Variant_Combat/Gameplay",
			"Counter_Core/Variant_Combat/Interfaces",
			"Counter_Core/Variant_Combat/UI",
			"Counter_Core/Variant_SideScrolling",
			"Counter_Core/Variant_SideScrolling/AI",
			"Counter_Core/Variant_SideScrolling/Gameplay",
			"Counter_Core/Variant_SideScrolling/Interfaces",
			"Counter_Core/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
