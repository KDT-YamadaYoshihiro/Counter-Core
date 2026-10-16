// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonValue;

/** Discover things to drop into a graph with `ue_add_node`. */
namespace UECli::NodeSearch
{
	/**
	 * Search for graph-droppable nodes whose name contains Query, optionally
	 * restricted to one class (and its supers). Kind is one of
	 * <c>function</c> / <c>event</c> / <c>macro</c> / <c>variable</c> /
	 * <c>all</c> (empty == all). Each result:
	 *   { name, kind, class?, pure?, static?, type?, tooltip?, params:[{name,type,out,return}] }
	 */
	TArray<TSharedPtr<FJsonValue>> Search(
		const FString& Query, const FString& ClassFilter, const FString& Kind, int32 Limit);
}
