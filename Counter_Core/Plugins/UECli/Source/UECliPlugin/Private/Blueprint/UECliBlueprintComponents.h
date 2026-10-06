// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UBlueprint;

/** Components on a Blueprint's construction script (the "Components" panel). */
namespace UECli::BlueprintComponents
{
	/** { name, class, parent, inherited } for every component (SCS + inherited). */
	TArray<TSharedPtr<FJsonValue>> ListComponents(UBlueprint& Blueprint);

	/**
	 * Add a component. ClassRef is a native class name (StaticMeshComponent) or a
	 * Blueprint path. ParentName attaches it under an existing scene component;
	 * empty attaches under the root. OutResult = { name, class, parent }.
	 */
	bool AddComponent(UBlueprint& Blueprint, const FString& ClassRef, const FString& Name, const FString& ParentName,
		TSharedRef<FJsonObject>& OutResult, FString& OutError);

	bool RemoveComponent(UBlueprint& Blueprint, const FString& Name, FString& OutError);

	/** Set a property on a construction-script component's template (e.g. a StaticMeshComponent's mesh). */
	bool SetComponentProperty(UBlueprint& Blueprint, const FString& ComponentName, const FString& Property, const FString& Value, FString& OutError);
}
