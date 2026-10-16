// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Type-agnostic property access by reflection. A target is an asset path
 * (/Game/DA_Item — a Blueprint resolves to its class defaults), a full object
 * path, or a level actor label (optionally "Label.ComponentName").
 */
namespace UECli::ObjectOps
{
	/** { object, class, properties: [{ name, type, category, value, editable }] }; PropertyPath (dotted / indexed) returns just that one. */
	bool GetProperties(const FString& Ref, const FString& Filter, bool bIncludeAll, const FString& PropertyPath,
		TSharedRef<FJsonObject>& Out, FString& OutError, FString& OutErrorCode);

	/** Set one property from its exported-text form; Property may be a dotted struct path. Out is the new entry. */
	bool SetProperty(const FString& Ref, const FString& Property, const FString& Value,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);
}
