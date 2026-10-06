// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

struct FEdGraphPinType;

/**
 * Parse an AI-friendly type spec into an FEdGraphPinType.
 *
 *   bool | int | int64 | byte | float | double | string | name | text
 *   vector | rotator | transform | color            (common structs)
 *   Actor | Pawn | StaticMeshComponent | ...         (native object refs)
 *   /Game/Characters/BP_Player                       (Blueprint object ref)
 *
 * A trailing "[]" makes it an array (e.g. "int[]", "/Game/BP_Item[]").
 */
namespace UECli::PinType
{
	bool FromSpec(const FString& Spec, FEdGraphPinType& OutType, FString& OutError);
}
