// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/** Create and delete Blueprint assets. */
namespace UECli::BlueprintAsset
{
	/**
	 * Create a new Blueprint at a package path (<c>/Game/Enemies/BP_Grunt</c>).
	 * ParentClass is a native class name or a Blueprint path; empty = Actor.
	 * bInterface makes it a Blueprint Interface (ParentClass is then ignored).
	 * OutResult = { path, name, parentClass, kind }.
	 */
	bool CreateBlueprint(const FString& PackagePath, const FString& ParentClassRef, bool bInterface,
		TSharedRef<FJsonObject>& OutResult, FString& OutError);

	/** Delete a Blueprint asset. */
	bool DeleteBlueprint(const FString& PackagePath, bool bForce, TArray<FString>& OutReferencers, FString& OutError);
}
