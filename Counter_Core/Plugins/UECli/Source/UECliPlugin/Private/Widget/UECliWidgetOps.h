// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Widget Blueprint (UMG) designer-tree edits: the parts Python cannot reach.
 * Widget / slot properties go through the generic object routes using the
 * objectPath / slotPath each node reports. Every edit is one transaction and
 * marks the Blueprint structurally modified; compile and save afterwards.
 * Functions return false with bOutNotFound set when the WBP or a widget is missing.
 */
namespace UECli::WidgetOps
{
	/** { blueprint, root, widgets: [node...], bindings: [{ widget, property, function }] }. */
	bool GetTree(const FString& Path, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** Add a widget of Class under Parent (a panel; empty = root, or the root panel). Out is the new node. */
	bool AddWidget(const FString& Path, const FString& ClassRef, const FString& Name, const FString& Parent, int32 Index,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** Remove a widget and its subtree. */
	bool RemoveWidget(const FString& Path, const FString& Name, FString& OutError, bool& bOutNotFound);

	/** Move a widget under another panel (Index < 0 = append). Out is the moved node. */
	bool Reparent(const FString& Path, const FString& Name, const FString& Parent, int32 Index,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** Bind a widget property (Text, Visibility, ...) to a function of the Widget Blueprint; empty Function removes it. */
	bool SetBinding(const FString& Path, const FString& Widget, const FString& Property, const FString& Function,
		FString& OutError, bool& bOutNotFound);
}
