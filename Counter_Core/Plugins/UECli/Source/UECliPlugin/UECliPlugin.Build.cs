// Copyright UE CLI. All rights reserved.

using UnrealBuildTool;

public class UECliPlugin : ModuleRules
{
	public UECliPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Projects",        // IPluginManager, for the plugin's own version
			"Json",            // request/response bodies
			"HTTPServer",           // FHttpServerModule embedded listener (v1 command channel)
			"WebSocketNetworking", "Sockets",  // v2 event channel (same module RemoteControl uses)
			"UnrealEd",        // editor context, asset save, transactions
			"BlueprintGraph",  // UK2Node_* / UEdGraph manipulation
			"KismetCompiler",  // Blueprint compile (Phase 3)
			"AssetRegistry",   // enumerate Blueprint assets
			"AssetTools",      // asset creation (Phase 2)
			"RHI",             // GIsRHIInitialized check before screenshots (Phase 5)
			"AutomationController", // automation.run job — drive the engine's own test runner
			"NavigationSystem",     // level.build job — poll nav build progress
			"PythonScriptPlugin",   // python.exec job — run Python in the editor
			"AnimGraph",            // transition rule graphs addressed as From->To
			"Landscape",            // landscape create / heightmap import / sculpt
			"ImageWrapper",         // 16-bit PNG heightmaps
			"Niagara",              // Niagara system / emitter / module editing
			"NiagaraCore",
			"NiagaraEditor",
			"Foliage",              // LandscapeEdit.h includes InstancedFoliageActor.h
			"UMG", "UMGEditor",     // Widget Blueprint designer-tree edits
		});

		// Header only — ILiveCodingModule is resolved at runtime via GetModulePtr.
		PrivateIncludePathModuleNames.Add("LiveCoding");
	}
}
