// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

/**
 * Converts Blueprints to the UE CLI JSON representation the AI works with:
 * stable-per-read logical node ids (N1, N2, ...) alongside real UE GUIDs, pins
 * with categories and defaults, connections in `nodeId.pinId` shorthand.
 *
 * Read-only. The node/pin JSON helpers are reused by the Phase 2 edit handlers
 * to describe the nodes they create.
 */
namespace UECli::BlueprintReader
{
	/** Resolve a Blueprint by package path (`/Game/BP_Player`) or object path. */
	UBlueprint* LoadBlueprintByPath(const FString& Path, FString& OutError);

	/** All Blueprint assets, optionally filtered to a package-path prefix. Does not load them. */
	TArray<TSharedPtr<FJsonValue>> ListBlueprints(const FString& PackagePathPrefix);

	/** Variables + graph list for a Blueprint (no graph contents). */
	TSharedRef<FJsonObject> InspectBlueprint(UBlueprint& Blueprint);

	/**
	 * One graph as { blueprint, name, type, nodes[], connections[] }.
	 * Returns false and fills OutError if the graph name does not resolve.
	 */
	bool ReadGraph(UBlueprint& Blueprint, const FString& GraphName, TSharedRef<FJsonObject>& OutGraph, FString& OutError);

	/** Find a graph on a Blueprint by name across ubergraph/function/macro/delegate pages. */
	UEdGraph* FindGraph(UBlueprint& Blueprint, const FString& GraphName, FString& OutType);

	/** Logical id (N1, N2, ...) for every non-null node, in graph order. */
	TMap<const UEdGraphNode*, FString> ComputeNodeIds(const UEdGraph& Graph);

	/** One node as JSON. Pass the graph's id map so `linkedTo` can be resolved; an empty map is fine. */
	TSharedRef<FJsonObject> NodeToJson(const UEdGraphNode& Node, const TMap<const UEdGraphNode*, FString>& NodeIds);
}
