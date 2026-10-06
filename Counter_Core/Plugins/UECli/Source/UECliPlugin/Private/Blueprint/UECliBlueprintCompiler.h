// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UBlueprint;

/**
 * Phase 3: compile a Blueprint and report the result, and save its package.
 * Compile never triggers a save; on a failed compile the caller decides whether
 * to save the broken state (the plugin does not do it implicitly).
 */
namespace UECli::BlueprintCompiler
{
	/**
	 * Compile the Blueprint. OutResult = { status, errors, warnings, messages[] }
	 * where messages are { severity, text }. Returns true if the compile ran
	 * (even with errors); false only if compilation could not be attempted.
	 */
	bool Compile(UBlueprint& Blueprint, TSharedRef<FJsonObject>& OutResult, FString& OutError);

	/** Save the Blueprint's package to disk. */
	bool Save(UBlueprint& Blueprint, FString& OutError);
}
