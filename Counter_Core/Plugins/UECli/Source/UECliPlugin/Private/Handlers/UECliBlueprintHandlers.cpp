// Copyright UE CLI. All rights reserved.

#include "Handlers/UECliBlueprintHandlers.h"
#include "UECliServices.h"

#include "Blueprint/UECliBlueprintAsset.h"
#include "Blueprint/UECliBlueprintCompiler.h"
#include "Blueprint/UECliBlueprintComponents.h"
#include "Blueprint/UECliBlueprintEditor.h"
#include "Blueprint/UECliBlueprintReader.h"
#include "Blueprint/UECliNodeSearch.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Events/UECliEventHub.h"
#include "Http/UECliHttpTypes.h"
#include "Dom/JsonValue.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace UECli::BlueprintHandlers
{
	using namespace UECli::Http;

	namespace
	{
		/** Resolve { path, graph } from the query string to a Blueprint + graph, or send an error. */
		bool ResolveGraph(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete,
			UBlueprint*& OutBlueprint, UEdGraph*& OutGraph)
		{
			OutBlueprint = nullptr;
			OutGraph = nullptr;

			const FString Path = QueryParam(Request, TEXT("path"));
			const FString GraphName = QueryParam(Request, TEXT("graph"));
			if (Path.IsEmpty() || GraphName.IsEmpty())
			{
				SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameters 'path' and 'graph' are required."));
				return false;
			}

			FString Error;
			OutBlueprint = BlueprintReader::LoadBlueprintByPath(Path, Error);
			if (!OutBlueprint)
			{
				SendError(OnComplete, 404, TEXT("blueprint.not_found"), Error);
				return false;
			}

			FString GraphType;
			OutGraph = BlueprintReader::FindGraph(*OutBlueprint, GraphName, GraphType);
			if (!OutGraph)
			{
				SendError(OnComplete, 404, TEXT("graph.not_found"),
					FString::Printf(TEXT("Blueprint '%s' has no graph named '%s'."), *OutBlueprint->GetName(), *GraphName));
				return false;
			}

			return true;
		}

		bool SendOk(const FHttpResultCallback& OnComplete)
		{
			const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetBoolField(TEXT("ok"), true);
			return SendJson(OnComplete, 200, Body);
		}

		bool RequireBody(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, TSharedPtr<FJsonObject>& OutBody)
		{
			FString Error;
			OutBody = ParseJsonBody(Request, Error);
			if (!OutBody.IsValid())
			{
				SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
				return false;
			}
			return true;
		}

		void NotifyChanged(const UBlueprint& Blueprint, const UEdGraph& Graph, const TCHAR* Op)
		{
			const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("blueprint"), Blueprint.GetOutermost()->GetName());
			Payload->SetStringField(TEXT("graph"), Graph.GetName());
			Payload->SetStringField(TEXT("op"), Op);
			UECli::Services::Events().Broadcast(TEXT("blueprint.changed"), Payload);
		}
	}

	// ---------------------------------------------------------------- read

	bool List(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		return SendJsonArray(OnComplete, 200, BlueprintReader::ListBlueprints(QueryParam(Request, TEXT("path"))));
	}

	bool Inspect(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const FString Path = QueryParam(Request, TEXT("path"));
		if (Path.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'path' is required."));
		}

		FString Error;
		UBlueprint* Blueprint = BlueprintReader::LoadBlueprintByPath(Path, Error);
		if (!Blueprint)
		{
			return SendError(OnComplete, 404, TEXT("blueprint.not_found"), Error);
		}
		return SendJson(OnComplete, 200, BlueprintReader::InspectBlueprint(*Blueprint));
	}

	bool SearchNodes(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const FString Query = QueryParam(Request, TEXT("query"));
		const FString ClassFilter = QueryParam(Request, TEXT("class"));
		const FString Kind = QueryParam(Request, TEXT("kind"), TEXT("all"));
		const bool bSpecificKind = !Kind.IsEmpty() && !Kind.Equals(TEXT("all"), ESearchCase::IgnoreCase);
		if (Query.IsEmpty() && ClassFilter.IsEmpty() && !bSpecificKind)
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Pass 'query', 'class' and/or a specific 'kind'."));
		}
		const int32 Limit = FMath::Clamp(FCString::Atoi(*QueryParam(Request, TEXT("limit"), TEXT("50"))), 1, 200);
		return SendJsonArray(OnComplete, 200, NodeSearch::Search(Query, ClassFilter, Kind, Limit));
	}

	bool Create(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}

		FString Path, ParentClass;
		Body->TryGetStringField(TEXT("path"), Path);
		Body->TryGetStringField(TEXT("parentClass"), ParentClass);
		bool bInterface = false;
		Body->TryGetBoolField(TEXT("interface"), bInterface);
		if (Path.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'path' is required."));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!BlueprintAsset::CreateBlueprint(Path, ParentClass, bInterface, Result, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.create_failed"), Error);
		}

		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("op"), TEXT("create-blueprint"));
		Payload->SetStringField(TEXT("blueprint"), Path);
		UECli::Services::Events().Broadcast(TEXT("asset.changed"), Payload);
		return SendJson(OnComplete, 200, Result);
	}

	bool DeleteAsset(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const FString Path = QueryParam(Request, TEXT("path"));
		if (Path.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'path' is required."));
		}

		const bool bForce = QueryParam(Request, TEXT("force")) == TEXT("true");
		FString Error;
		TArray<FString> Referencers;
		if (!BlueprintAsset::DeleteBlueprint(Path, bForce, Referencers, Error))
		{
			if (Referencers.Num() > 0)
			{
				const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
				TArray<TSharedPtr<FJsonValue>> Refs;
				for (const FString& Ref : Referencers)
				{
					Refs.Add(MakeShared<FJsonValueString>(Ref));
				}
				Details->SetArrayField(TEXT("referencers"), Refs);
				return SendError(OnComplete, 409, TEXT("blueprint.referenced"), Error, Details);
			}
			return SendError(OnComplete, 422, TEXT("blueprint.delete_failed"), Error);
		}

		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("op"), TEXT("delete-blueprint"));
		Payload->SetStringField(TEXT("blueprint"), Path);
		UECli::Services::Events().Broadcast(TEXT("asset.changed"), Payload);

		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetBoolField(TEXT("ok"), true);
		return SendJson(OnComplete, 200, Body);
	}

	bool GetGraph(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = nullptr;
		UEdGraph* Graph = nullptr;
		if (!ResolveGraph(Request, OnComplete, Blueprint, Graph))
		{
			return true;
		}

		FString Error;
		TSharedRef<FJsonObject> GraphJson = MakeShared<FJsonObject>();
		if (!BlueprintReader::ReadGraph(*Blueprint, Graph->GetName(), GraphJson, Error))
		{
			return SendError(OnComplete, 404, TEXT("graph.not_found"), Error);
		}
		return SendJson(OnComplete, 200, GraphJson);
	}

	// ---------------------------------------------------------------- edit

	bool AddNode(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = nullptr;
		UEdGraph* Graph = nullptr;
		if (!ResolveGraph(Request, OnComplete, Blueprint, Graph))
		{
			return true;
		}

		TSharedPtr<FJsonObject> Body;
		if (!RequireBody(Request, OnComplete, Body))
		{
			return true;
		}

		BlueprintEditor::FAddNodeRequest AddRequest;
		AddRequest.Type = Body->GetStringField(TEXT("type"));
		Body->TryGetStringField(TEXT("memberName"), AddRequest.MemberName);
		Body->TryGetStringField(TEXT("memberParent"), AddRequest.MemberParent);

		const TArray<TSharedPtr<FJsonValue>>* Position = nullptr;
		if (Body->TryGetArrayField(TEXT("position"), Position) && Position->Num() == 2)
		{
			AddRequest.PosX = static_cast<int32>((*Position)[0]->AsNumber());
			AddRequest.PosY = static_cast<int32>((*Position)[1]->AsNumber());
		}

		if (AddRequest.Type.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'type' is required."));
		}

		FString Error;
		TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		if (!BlueprintEditor::AddNode(*Blueprint, *Graph, AddRequest, NodeJson, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyChanged(*Blueprint, *Graph, TEXT("add-node"));
		return SendJson(OnComplete, 200, NodeJson);
	}

	bool DeleteNode(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = nullptr;
		UEdGraph* Graph = nullptr;
		if (!ResolveGraph(Request, OnComplete, Blueprint, Graph))
		{
			return true;
		}

		const FString NodeRef = QueryParam(Request, TEXT("node"));
		if (NodeRef.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'node' is required."));
		}

		FString Error;
		if (!BlueprintEditor::DeleteNode(*Blueprint, *Graph, NodeRef, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyChanged(*Blueprint, *Graph, TEXT("delete-node"));
		return SendOk(OnComplete);
	}

	bool SetPinValue(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = nullptr;
		UEdGraph* Graph = nullptr;
		if (!ResolveGraph(Request, OnComplete, Blueprint, Graph))
		{
			return true;
		}

		TSharedPtr<FJsonObject> Body;
		if (!RequireBody(Request, OnComplete, Body))
		{
			return true;
		}

		FString NodeRef, PinName, Value;
		Body->TryGetStringField(TEXT("node"), NodeRef);
		Body->TryGetStringField(TEXT("pin"), PinName);
		Body->TryGetStringField(TEXT("value"), Value);
		if (NodeRef.IsEmpty() || PinName.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'node' and 'pin' are required."));
		}

		FString Error;
		if (!BlueprintEditor::SetPinValue(*Blueprint, *Graph, NodeRef, PinName, Value, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyChanged(*Blueprint, *Graph, TEXT("set-pin"));
		return SendOk(OnComplete);
	}

	bool Connect(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = nullptr;
		UEdGraph* Graph = nullptr;
		if (!ResolveGraph(Request, OnComplete, Blueprint, Graph))
		{
			return true;
		}

		TSharedPtr<FJsonObject> Body;
		if (!RequireBody(Request, OnComplete, Body))
		{
			return true;
		}

		FString From, To;
		Body->TryGetStringField(TEXT("from"), From);
		Body->TryGetStringField(TEXT("to"), To);
		if (From.IsEmpty() || To.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'from' and 'to' are required (each '<node>.<pin>')."));
		}

		FString Error;
		if (!BlueprintEditor::ConnectPins(*Blueprint, *Graph, From, To, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyChanged(*Blueprint, *Graph, TEXT("connect"));
		return SendOk(OnComplete);
	}

	bool Disconnect(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = nullptr;
		UEdGraph* Graph = nullptr;
		if (!ResolveGraph(Request, OnComplete, Blueprint, Graph))
		{
			return true;
		}

		const FString From = QueryParam(Request, TEXT("from"));
		const FString To = QueryParam(Request, TEXT("to"));
		if (From.IsEmpty() || To.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameters 'from' and 'to' are required."));
		}

		FString Error;
		if (!BlueprintEditor::DisconnectPins(*Blueprint, *Graph, From, To, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyChanged(*Blueprint, *Graph, TEXT("disconnect"));
		return SendOk(OnComplete);
	}

	bool MoveNode(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = nullptr;
		UEdGraph* Graph = nullptr;
		if (!ResolveGraph(Request, OnComplete, Blueprint, Graph))
		{
			return true;
		}

		TSharedPtr<FJsonObject> Body;
		if (!RequireBody(Request, OnComplete, Body))
		{
			return true;
		}

		FString NodeRef;
		Body->TryGetStringField(TEXT("node"), NodeRef);
		const TArray<TSharedPtr<FJsonValue>>* Position = nullptr;
		if (NodeRef.IsEmpty() || !Body->TryGetArrayField(TEXT("position"), Position) || Position->Num() != 2)
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body needs 'node' and 'position' as [x, y]."));
		}

		FString Error;
		if (!BlueprintEditor::MoveNode(*Blueprint, *Graph, NodeRef,
			static_cast<int32>((*Position)[0]->AsNumber()), static_cast<int32>((*Position)[1]->AsNumber()), Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyChanged(*Blueprint, *Graph, TEXT("move-node"));
		return SendOk(OnComplete);
	}

	// ---------------------------------------------------------------- compile / save

	namespace
	{
		UBlueprint* ResolveBlueprint(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			const FString Path = QueryParam(Request, TEXT("path"));
			if (Path.IsEmpty())
			{
				SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'path' is required."));
				return nullptr;
			}

			FString Error;
			UBlueprint* Blueprint = BlueprintReader::LoadBlueprintByPath(Path, Error);
			if (!Blueprint)
			{
				SendError(OnComplete, 404, TEXT("blueprint.not_found"), Error);
			}
			return Blueprint;
		}
	}

	bool Compile(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		FString Error;
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!BlueprintCompiler::Compile(*Blueprint, Result, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.compile_failed"), Error);
		}
		return SendJson(OnComplete, 200, Result);
	}

	bool Save(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		// Refuse to persist a Blueprint that does not compile (R6.2) unless forced.
		const bool bForce = QueryParam(Request, TEXT("force")) == TEXT("true");
		if (!bForce)
		{
			if (Blueprint->Status == BS_Dirty || Blueprint->Status == BS_Unknown)
			{
				FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
			}
			if (Blueprint->Status == BS_Error)
			{
				return SendError(OnComplete, 409, TEXT("blueprint.compile_errors"),
					TEXT("The Blueprint has compile errors; fix them (see blueprint compile) or save with force=true."));
			}
		}

		FString Error;
		if (!BlueprintCompiler::Save(*Blueprint, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.save_failed"), Error);
		}
		return SendOk(OnComplete);
	}

	// ---------------------------------------------------------------- member variables

	namespace
	{
		void NotifyVarChanged(const UBlueprint& Blueprint, const TCHAR* Op)
		{
			const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("blueprint"), Blueprint.GetOutermost()->GetName());
			Payload->SetStringField(TEXT("op"), Op);
			UECli::Services::Events().Broadcast(TEXT("blueprint.changed"), Payload);
		}
	}

	bool AddVariable(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		TSharedPtr<FJsonObject> Body;
		if (!RequireBody(Request, OnComplete, Body))
		{
			return true;
		}

		FString Name, Type, DefaultValue;
		Body->TryGetStringField(TEXT("name"), Name);
		Body->TryGetStringField(TEXT("type"), Type);
		Body->TryGetStringField(TEXT("default"), DefaultValue);
		bool bInstanceEditable = false;
		Body->TryGetBoolField(TEXT("instanceEditable"), bInstanceEditable);
		if (Name.IsEmpty() || Type.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'name' and 'type' are required."));
		}

		FString Error;
		if (!BlueprintEditor::AddVariable(*Blueprint, Name, Type, DefaultValue, bInstanceEditable, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyVarChanged(*Blueprint, TEXT("add-variable"));
		return SendOk(OnComplete);
	}

	bool RenameVariable(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		TSharedPtr<FJsonObject> Body;
		if (!RequireBody(Request, OnComplete, Body))
		{
			return true;
		}

		FString From, To;
		Body->TryGetStringField(TEXT("from"), From);
		Body->TryGetStringField(TEXT("to"), To);
		if (From.IsEmpty() || To.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'from' and 'to' are required."));
		}

		FString Error;
		if (!BlueprintEditor::RenameVariable(*Blueprint, From, To, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyVarChanged(*Blueprint, TEXT("rename-variable"));
		return SendOk(OnComplete);
	}

	bool RemoveVariable(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		const FString Name = QueryParam(Request, TEXT("name"));
		if (Name.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'name' is required."));
		}

		FString Error;
		if (!BlueprintEditor::RemoveVariable(*Blueprint, Name, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyVarChanged(*Blueprint, TEXT("remove-variable"));
		return SendOk(OnComplete);
	}

	bool SetVariableDefault(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		TSharedPtr<FJsonObject> Body;
		if (!RequireBody(Request, OnComplete, Body))
		{
			return true;
		}

		FString Name, Value;
		Body->TryGetStringField(TEXT("name"), Name);
		Body->TryGetStringField(TEXT("value"), Value);
		if (Name.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'name' is required."));
		}

		FString Error;
		if (!BlueprintEditor::SetVariableDefault(*Blueprint, Name, Value, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyVarChanged(*Blueprint, TEXT("set-variable-default"));
		return SendOk(OnComplete);
	}

	// ---------------------------------------------------------------- function graphs

	namespace
	{
		void ReadParamSpecs(const TSharedPtr<FJsonObject>& Body, const TCHAR* Field, TArray<BlueprintEditor::FParamSpec>& Out)
		{
			const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
			if (!Body->TryGetArrayField(Field, Array))
			{
				return;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Array)
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (Value->TryGetObject(Obj))
				{
					BlueprintEditor::FParamSpec Spec;
					(*Obj)->TryGetStringField(TEXT("name"), Spec.Name);
					(*Obj)->TryGetStringField(TEXT("type"), Spec.Type);
					if (!Spec.Name.IsEmpty() && !Spec.Type.IsEmpty())
					{
						Out.Add(MoveTemp(Spec));
					}
				}
			}
		}
	}

	bool AddFunction(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		TSharedPtr<FJsonObject> Body;
		if (!RequireBody(Request, OnComplete, Body))
		{
			return true;
		}

		FString Name;
		Body->TryGetStringField(TEXT("name"), Name);
		if (Name.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'name' is required."));
		}

		TArray<BlueprintEditor::FParamSpec> Inputs, Outputs;
		ReadParamSpecs(Body, TEXT("inputs"), Inputs);
		ReadParamSpecs(Body, TEXT("outputs"), Outputs);

		FString Error;
		TSharedRef<FJsonObject> Function = MakeShared<FJsonObject>();
		if (!BlueprintEditor::AddFunction(*Blueprint, Name, Inputs, Outputs, Function, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyVarChanged(*Blueprint, TEXT("add-function"));
		return SendJson(OnComplete, 200, Function);
	}

	bool RemoveFunction(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		const FString Name = QueryParam(Request, TEXT("name"));
		if (Name.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'name' is required."));
		}

		FString Error;
		if (!BlueprintEditor::RemoveFunction(*Blueprint, Name, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyVarChanged(*Blueprint, TEXT("remove-function"));
		return SendOk(OnComplete);
	}

	// ---------------------------------------------------------------- components

	bool ListComponents(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}
		return SendJsonArray(OnComplete, 200, BlueprintComponents::ListComponents(*Blueprint));
	}

	bool AddComponent(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		TSharedPtr<FJsonObject> Body;
		if (!RequireBody(Request, OnComplete, Body))
		{
			return true;
		}

		FString ClassRef, Name, Parent;
		Body->TryGetStringField(TEXT("class"), ClassRef);
		Body->TryGetStringField(TEXT("name"), Name);
		Body->TryGetStringField(TEXT("parent"), Parent);
		if (ClassRef.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'class' is required."));
		}

		FString Error;
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!BlueprintComponents::AddComponent(*Blueprint, ClassRef, Name, Parent, Result, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyVarChanged(*Blueprint, TEXT("add-component"));
		return SendJson(OnComplete, 200, Result);
	}

	bool RemoveComponent(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		const FString Name = QueryParam(Request, TEXT("name"));
		if (Name.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'name' is required."));
		}

		FString Error;
		if (!BlueprintComponents::RemoveComponent(*Blueprint, Name, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyVarChanged(*Blueprint, TEXT("remove-component"));
		return SendOk(OnComplete);
	}

	bool SetComponentProperty(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		TSharedPtr<FJsonObject> Body;
		if (!RequireBody(Request, OnComplete, Body))
		{
			return true;
		}

		FString Component, Property, Value;
		Body->TryGetStringField(TEXT("component"), Component);
		Body->TryGetStringField(TEXT("property"), Property);
		Body->TryGetStringField(TEXT("value"), Value);
		if (Component.IsEmpty() || Property.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'component' and 'property' are required."));
		}

		FString Error;
		if (!BlueprintComponents::SetComponentProperty(*Blueprint, Component, Property, Value, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyVarChanged(*Blueprint, TEXT("set-component-property"));
		return SendOk(OnComplete);
	}

	bool Snapshot(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		// One game-thread pass, so no edit can land between the pieces; any
		// piece that cannot be read fails the whole request (no silent gaps).
		const TSharedRef<FJsonObject> Json = BlueprintReader::InspectBlueprint(*Blueprint);

		TArray<TSharedPtr<FJsonValue>> Interfaces;
		for (const FString& Name : BlueprintEditor::ListInterfaces(*Blueprint))
		{
			Interfaces.Add(MakeShared<FJsonValueString>(Name));
		}
		Json->SetArrayField(TEXT("interfaces"), Interfaces);
		Json->SetArrayField(TEXT("components"), BlueprintComponents::ListComponents(*Blueprint));

		TArray<TSharedPtr<FJsonValue>> GraphDetails;
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (Json->TryGetArrayField(TEXT("graphs"), Graphs))
		{
			for (const TSharedPtr<FJsonValue>& Graph : *Graphs)
			{
				const TSharedPtr<FJsonObject>* GraphObj = nullptr;
				FString GraphName;
				if (!Graph->TryGetObject(GraphObj) || !(*GraphObj)->TryGetStringField(TEXT("name"), GraphName))
				{
					continue;
				}
				FString Error;
				TSharedRef<FJsonObject> GraphJson = MakeShared<FJsonObject>();
				if (!BlueprintReader::ReadGraph(*Blueprint, GraphName, GraphJson, Error))
				{
					return SendError(OnComplete, 500, TEXT("blueprint.snapshot_failed"),
						FString::Printf(TEXT("Could not read graph '%s': %s"), *GraphName, *Error));
				}
				GraphDetails.Add(MakeShared<FJsonValueObject>(GraphJson));
			}
		}
		Json->SetArrayField(TEXT("graphDetails"), GraphDetails);
		return SendJson(OnComplete, 200, Json);
	}

	// ---------------------------------------------------------------- interfaces

	bool ListInterfaces(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FString& Name : BlueprintEditor::ListInterfaces(*Blueprint))
		{
			Items.Add(MakeShared<FJsonValueString>(Name));
		}
		return SendJsonArray(OnComplete, 200, Items);
	}

	bool AddInterface(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		TSharedPtr<FJsonObject> Body;
		if (!RequireBody(Request, OnComplete, Body))
		{
			return true;
		}

		FString Interface;
		Body->TryGetStringField(TEXT("interface"), Interface);
		if (Interface.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'interface' is required."));
		}

		FString Error;
		if (!BlueprintEditor::AddInterface(*Blueprint, Interface, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyVarChanged(*Blueprint, TEXT("add-interface"));
		return SendOk(OnComplete);
	}

	bool RemoveInterface(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		UBlueprint* Blueprint = ResolveBlueprint(Request, OnComplete);
		if (!Blueprint)
		{
			return true;
		}

		const FString Interface = QueryParam(Request, TEXT("interface"));
		if (Interface.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'interface' is required."));
		}
		const bool bPreserve = QueryParam(Request, TEXT("preserveFunctions")) == TEXT("true");

		FString Error;
		if (!BlueprintEditor::RemoveInterface(*Blueprint, Interface, bPreserve, Error))
		{
			return SendError(OnComplete, 422, TEXT("blueprint.edit_failed"), Error);
		}
		NotifyVarChanged(*Blueprint, TEXT("remove-interface"));
		return SendOk(OnComplete);
	}
}
