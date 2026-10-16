// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

struct FHttpServerRequest;

/** Request handlers for the Blueprint routes. Bound by FUECliHttpServer. */
namespace UECli::BlueprintHandlers
{
	// --- Phase 1: read ---
	bool List(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);     // GET  /blueprints?path=
	bool Inspect(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);   // GET  /blueprint?path=
	bool Snapshot(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);  // GET  /blueprint/snapshot?path=
	bool SearchNodes(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete); // GET /nodes/search?query=&class=&limit=

	// --- assets ---
	bool Create(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);   // POST   /blueprints { path, parentClass? }
	bool DeleteAsset(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete); // DELETE /blueprint?path=
	bool GetGraph(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);  // GET  /blueprint/graph?path=&graph=

	// --- Phase 2: edit (query: path, graph) ---
	bool AddNode(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);      // POST   /blueprint/graph/nodes
	bool DeleteNode(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);   // DELETE /blueprint/graph/node?node=
	bool SetPinValue(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);  // POST   /blueprint/graph/node/pin
	bool Connect(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);      // POST   /blueprint/graph/connections
	bool Disconnect(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);   // DELETE /blueprint/graph/connection?from=&to=
	bool MoveNode(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);     // POST   /blueprint/graph/node/position

	// --- Phase 3: compile / save (query: path) ---
	bool Compile(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);      // POST /blueprint/compile
	bool Save(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);         // POST /blueprint/save

	// --- member variables (query: path) ---
	bool AddVariable(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);       // POST   /blueprint/variables { name, type, default?, instanceEditable? }
	bool RenameVariable(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);    // POST   /blueprint/variable/rename { from, to }
	bool RemoveVariable(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);    // DELETE /blueprint/variable?name=
	bool SetVariableDefault(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);// POST   /blueprint/variable/default { name, value }

	// --- function graphs (query: path) ---
	bool AddFunction(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);    // POST   /blueprint/functions { name, inputs?, outputs? }
	bool RemoveFunction(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete); // DELETE /blueprint/function?name=

	// --- components (query: path) ---
	bool ListComponents(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);   // GET    /blueprint/components
	bool AddComponent(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);     // POST   /blueprint/components { class, name?, parent? }
	bool RemoveComponent(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);  // DELETE /blueprint/component?name=
	bool SetComponentProperty(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete); // POST /blueprint/component/property { component, property, value }

	// --- interfaces (query: path) ---
	bool ListInterfaces(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);   // GET    /blueprint/interfaces
	bool AddInterface(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);     // POST   /blueprint/interfaces { interface }
	bool RemoveInterface(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);  // DELETE /blueprint/interface?interface=&preserveFunctions=
}
