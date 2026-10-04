// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

/**
 * Phase 2 Blueprint mutations, performed through the editor's own object model
 * and schema (never by touching serialized bytes). Every entry point wraps the
 * change in an Unreal transaction so it is undoable, and marks the Blueprint
 * dirty; none of them save or compile — that is Phase 3.
 *
 * Node references accept either a real GUID or a `N<k>` logical id from the most
 * recent graph read.
 */
namespace UECli::BlueprintEditor
{
	/** Resolve a node by GUID or `N<k>` id within a graph. */
	UEdGraphNode* ResolveNode(UEdGraph& Graph, const FString& NodeRef, FString& OutError);

	/** Resolve `<nodeRef>.<pinName>` to a pin. */
	UEdGraphPin* ResolvePin(UEdGraph& Graph, const FString& PinRef, FString& OutError);

	struct FAddNodeRequest
	{
		FString Type;          // Event | CustomEvent | CallFunction | VariableGet | VariableSet | Branch
		FString MemberName;    // function / event / variable name
		FString MemberParent;  // owning class (optional; resolved for CallFunction/Event)
		TOptional<int32> PosX;
		TOptional<int32> PosY;
	};

	/** Add a node. On success OutNode carries its JSON (guid, pins, ...). */
	bool AddNode(UBlueprint& Blueprint, UEdGraph& Graph, const FAddNodeRequest& Request, TSharedRef<FJsonObject>& OutNode, FString& OutError);

	bool DeleteNode(UBlueprint& Blueprint, UEdGraph& Graph, const FString& NodeRef, FString& OutError);

	bool SetPinValue(UBlueprint& Blueprint, UEdGraph& Graph, const FString& NodeRef, const FString& PinName, const FString& Value, FString& OutError);

	bool ConnectPins(UBlueprint& Blueprint, UEdGraph& Graph, const FString& FromRef, const FString& ToRef, FString& OutError);

	bool DisconnectPins(UBlueprint& Blueprint, UEdGraph& Graph, const FString& FromRef, const FString& ToRef, FString& OutError);

	bool MoveNode(UBlueprint& Blueprint, UEdGraph& Graph, const FString& NodeRef, int32 PosX, int32 PosY, FString& OutError);

	// --- member variables ---

	bool AddVariable(UBlueprint& Blueprint, const FString& Name, const FString& TypeSpec, const FString& DefaultValue,
		bool bInstanceEditable, FString& OutError);

	bool RenameVariable(UBlueprint& Blueprint, const FString& From, const FString& To, FString& OutError);

	bool RemoveVariable(UBlueprint& Blueprint, const FString& Name, FString& OutError);

	bool SetVariableDefault(UBlueprint& Blueprint, const FString& Name, const FString& Value, FString& OutError);

	// --- function graphs ---

	struct FParamSpec
	{
		FString Name;
		FString Type;
	};

	/** Create a new function graph with the given inputs/outputs. */
	bool AddFunction(UBlueprint& Blueprint, const FString& Name,
		const TArray<FParamSpec>& Inputs, const TArray<FParamSpec>& Outputs,
		TSharedRef<FJsonObject>& OutFunction, FString& OutError);

	bool RemoveFunction(UBlueprint& Blueprint, const FString& Name, FString& OutError);

	// --- interfaces ---

	TArray<FString> ListInterfaces(UBlueprint& Blueprint);

	bool AddInterface(UBlueprint& Blueprint, const FString& InterfaceRef, FString& OutError);

	bool RemoveInterface(UBlueprint& Blueprint, const FString& InterfaceRef, bool bPreserveFunctions, FString& OutError);
}
