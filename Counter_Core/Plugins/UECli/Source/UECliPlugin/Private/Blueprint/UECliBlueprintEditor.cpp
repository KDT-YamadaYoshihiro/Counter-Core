// Copyright UE CLI. All rights reserved.

#include "Blueprint/UECliBlueprintEditor.h"

#include "Blueprint/UECliBlueprintReader.h"
#include "Blueprint/UECliPinType.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UECliLog.h"
#include "UObject/Class.h"

#define LOCTEXT_NAMESPACE "UECliBlueprintEditor"

namespace UECli::BlueprintEditor
{
	namespace
	{
		const UEdGraphSchema_K2* K2Schema(const UEdGraph& Graph)
		{
			return Cast<UEdGraphSchema_K2>(Graph.GetSchema());
		}

		/** Blueprint-callable library classes searched when a CallFunction has no explicit owner. */
		const TCHAR* const CommonFunctionLibraries[] =
		{
			TEXT("KismetSystemLibrary"),
			TEXT("KismetMathLibrary"),
			TEXT("GameplayStatics"),
			TEXT("KismetStringLibrary"),
			TEXT("KismetTextLibrary"),
			TEXT("KismetArrayLibrary"),
		};

		UClass* FindClassByName(const FString& Name)
		{
			if (Name.IsEmpty())
			{
				return nullptr;
			}
			return FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst);
		}

		UFunction* ResolveFunction(const UBlueprint& Blueprint, const FString& FunctionName, const FString& OwnerName, UClass*& OutOwner)
		{
			OutOwner = nullptr;
			const FName FuncFName(*FunctionName);

			if (!OwnerName.IsEmpty())
			{
				if (UClass* Owner = FindClassByName(OwnerName))
				{
					if (UFunction* Function = Owner->FindFunctionByName(FuncFName))
					{
						OutOwner = Owner;
						return Function;
					}
				}
				return nullptr;
			}

			// Self scope first (functions/events defined on this Blueprint or its parents).
			for (UClass* SelfClass : { Blueprint.SkeletonGeneratedClass.Get(), Blueprint.GeneratedClass.Get(), Blueprint.ParentClass.Get() })
			{
				if (SelfClass)
				{
					if (UFunction* Function = SelfClass->FindFunctionByName(FuncFName))
					{
						OutOwner = SelfClass;
						return Function;
					}
				}
			}

			for (const TCHAR* LibraryName : CommonFunctionLibraries)
			{
				if (UClass* Library = FindClassByName(LibraryName))
				{
					if (UFunction* Function = Library->FindFunctionByName(FuncFName))
					{
						OutOwner = Library;
						return Function;
					}
				}
			}

			return nullptr;
		}

		void PlaceNode(UEdGraphNode& Node, const FAddNodeRequest& Request, const UEdGraph& Graph)
		{
			if (Request.PosX.IsSet() && Request.PosY.IsSet())
			{
				Node.NodePosX = Request.PosX.GetValue();
				Node.NodePosY = Request.PosY.GetValue();
				return;
			}

			// Stagger below the lowest existing node so a fresh node is visible.
			int32 MaxY = 0;
			for (const UEdGraphNode* Existing : Graph.Nodes)
			{
				if (Existing && Existing != &Node)
				{
					MaxY = FMath::Max(MaxY, Existing->NodePosY);
				}
			}
			Node.NodePosX = 320;
			Node.NodePosY = MaxY + 160;
		}

		void Activate(UEdGraphNode& Node)
		{
			if (Node.IsAutomaticallyPlacedGhostNode())
			{
				Node.SetEnabledState(ENodeEnabledState::Enabled, /*bUserAction*/ false);
				Node.NodeComment.Empty();
			}
		}

		UK2Node_Event* FindExistingEvent(const UEdGraph& Graph, const FName EventName)
		{
			for (UEdGraphNode* Node : Graph.Nodes)
			{
				if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
				{
					if (Event->EventReference.GetMemberName() == EventName || Event->CustomFunctionName == EventName)
					{
						return Event;
					}
				}
			}
			return nullptr;
		}

		template <typename TNode>
		TNode* SpawnSimpleNode(UEdGraph& Graph)
		{
			TNode* Node = NewObject<TNode>(&Graph);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();
			Graph.AddNode(Node, /*bFromUI*/ false, /*bSelectNewNode*/ false);
			return Node;
		}

		/**
		 * Resolve a macro graph. LibraryPath empty -> the engine StandardMacros
		 * library (ForEachLoop / ForLoop / WhileLoop / DoOnce / Gate / FlipFlop /
		 * IsValid / …); otherwise a /Game/... macro-library Blueprint path.
		 */
		UEdGraph* ResolveMacroGraph(const FString& LibraryPath, const FString& MacroName, FString& OutError)
		{
			FString Path = LibraryPath.IsEmpty()
				? TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros")
				: LibraryPath;
			if (!Path.Contains(TEXT(".")))
			{
				Path += TEXT(".") + FPaths::GetCleanFilename(Path);
			}

			UBlueprint* Library = LoadObject<UBlueprint>(nullptr, *Path);
			if (!Library)
			{
				OutError = FString::Printf(TEXT("Macro library '%s' not found."), *Path);
				return nullptr;
			}

			for (UEdGraph* MacroGraph : Library->MacroGraphs)
			{
				if (MacroGraph && MacroGraph->GetName() == MacroName)
				{
					return MacroGraph;
				}
			}

			OutError = FString::Printf(TEXT("Macro '%s' not found in '%s'."), *MacroName, *Library->GetName());
			return nullptr;
		}
	}

	UEdGraphNode* ResolveNode(UEdGraph& Graph, const FString& NodeRef, FString& OutError)
	{
		if (NodeRef.StartsWith(TEXT("N")) && NodeRef.Len() > 1 && FChar::IsDigit(NodeRef[1]))
		{
			const int32 OneBasedIndex = FCString::Atoi(*NodeRef.RightChop(1));
			int32 Seen = 0;
			for (UEdGraphNode* Node : Graph.Nodes)
			{
				if (Node && ++Seen == OneBasedIndex)
				{
					return Node;
				}
			}
			OutError = FString::Printf(TEXT("Graph has no node '%s'."), *NodeRef);
			return nullptr;
		}

		FGuid Guid;
		if (FGuid::Parse(NodeRef, Guid))
		{
			for (UEdGraphNode* Node : Graph.Nodes)
			{
				if (Node && Node->NodeGuid == Guid)
				{
					return Node;
				}
			}
			OutError = FString::Printf(TEXT("Graph has no node with guid '%s'."), *NodeRef);
			return nullptr;
		}

		OutError = FString::Printf(TEXT("'%s' is neither a node guid nor an N<k> id."), *NodeRef);
		return nullptr;
	}

	UEdGraphPin* ResolvePin(UEdGraph& Graph, const FString& PinRef, FString& OutError)
	{
		FString NodeRef, PinName;
		if (!PinRef.Split(TEXT("."), &NodeRef, &PinName) || NodeRef.IsEmpty() || PinName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Pin reference '%s' must be '<node>.<pin>'."), *PinRef);
			return nullptr;
		}

		UEdGraphNode* Node = ResolveNode(Graph, NodeRef, OutError);
		if (!Node)
		{
			return nullptr;
		}

		if (UEdGraphPin* Pin = Node->FindPin(FName(*PinName)))
		{
			return Pin;
		}

		// Fall back to a case-insensitive match on the display name.
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && (Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase) ||
				Pin->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase)))
			{
				return Pin;
			}
		}

		OutError = FString::Printf(TEXT("Node '%s' has no pin '%s'."), *NodeRef, *PinName);
		return nullptr;
	}

	bool AddNode(UBlueprint& Blueprint, UEdGraph& Graph, const FAddNodeRequest& Request, TSharedRef<FJsonObject>& OutNode, FString& OutError)
	{
		// "Node": any UEdGraphNode subclass by name (memberName), in any graph — e.g.
		// AnimGraphNode_StateMachine / AnimGraphNode_SequencePlayer in an AnimGraph,
		// AnimStateNode in a state machine graph.
		const bool bGenericNode = Request.Type.Equals(TEXT("Node"), ESearchCase::IgnoreCase);
		const UEdGraphSchema_K2* Schema = K2Schema(Graph);
		if (!Schema && !bGenericNode)
		{
			OutError = TEXT("Graph is not a K2 (Blueprint) graph; use type 'Node' with a node class name.");
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("AddNode", "UE CLI: Add Node"));
		Graph.Modify();
		Blueprint.Modify();

		UEdGraphNode* NewNode = nullptr;

		if (bGenericNode)
		{
			UClass* NodeClass = FindClassByName(Request.MemberName);
			if (!NodeClass || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()) || NodeClass->HasAnyClassFlags(CLASS_Abstract))
			{
				OutError = FString::Printf(TEXT("'%s' is not a concrete graph node class."), *Request.MemberName);
				return false;
			}
			UEdGraphNode* Node = NewObject<UEdGraphNode>(&Graph, NodeClass, NAME_None, RF_Transactional);
			Graph.AddNode(Node, /*bFromUI*/ false, /*bSelectNewNode*/ false);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();
			NewNode = Node;
		}
		else if (Request.Type.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
		{
			const FName EventName(*Request.MemberName);
			if (UK2Node_Event* Existing = FindExistingEvent(Graph, EventName))
			{
				Activate(*Existing);
				NewNode = Existing;
			}
			else
			{
				UClass* Owner = FindClassByName(Request.MemberParent);
				if (!Owner)
				{
					Owner = Blueprint.ParentClass;
				}
				UFunction* Function = Owner ? Owner->FindFunctionByName(EventName) : nullptr;
				if (!Function)
				{
					OutError = FString::Printf(TEXT("No overridable event '%s' on %s."), *Request.MemberName,
						Owner ? *Owner->GetName() : TEXT("(unknown class)"));
					return false;
				}

				UK2Node_Event* Event = NewObject<UK2Node_Event>(&Graph);
				Event->EventReference.SetExternalMember(EventName, Owner);
				Event->bOverrideFunction = true;
				Event->CreateNewGuid();
				Event->PostPlacedNewNode();
				Event->AllocateDefaultPins();
				Graph.AddNode(Event, /*bFromUI*/ false, /*bSelectNewNode*/ false);
				NewNode = Event;
			}
		}
		else if (Request.Type.Equals(TEXT("CustomEvent"), ESearchCase::IgnoreCase))
		{
			UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(&Graph);
			Event->CustomFunctionName = FName(*Request.MemberName);
			Event->CreateNewGuid();
			Event->PostPlacedNewNode();
			Event->AllocateDefaultPins();
			Graph.AddNode(Event, false, false);
			NewNode = Event;
		}
		else if (Request.Type.Equals(TEXT("CallFunction"), ESearchCase::IgnoreCase))
		{
			UClass* Owner = nullptr;
			UFunction* Function = ResolveFunction(Blueprint, Request.MemberName, Request.MemberParent, Owner);
			if (!Function)
			{
				OutError = Request.MemberParent.IsEmpty()
					? FString::Printf(TEXT("Could not find a Blueprint-callable function named '%s'. Pass memberParent to disambiguate."), *Request.MemberName)
					: FString::Printf(TEXT("Class '%s' has no function '%s'."), *Request.MemberParent, *Request.MemberName);
				return false;
			}

			UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(&Graph);
			Call->SetFromFunction(Function);
			Call->CreateNewGuid();
			Call->PostPlacedNewNode();
			Call->AllocateDefaultPins();
			Graph.AddNode(Call, false, false);
			NewNode = Call;
		}
		else if (Request.Type.Equals(TEXT("VariableGet"), ESearchCase::IgnoreCase) ||
			Request.Type.Equals(TEXT("VariableSet"), ESearchCase::IgnoreCase))
		{
			const bool bSet = Request.Type.Equals(TEXT("VariableSet"), ESearchCase::IgnoreCase);
			UK2Node_Variable* Var = bSet
				? static_cast<UK2Node_Variable*>(NewObject<UK2Node_VariableSet>(&Graph))
				: static_cast<UK2Node_Variable*>(NewObject<UK2Node_VariableGet>(&Graph));
			Var->VariableReference.SetSelfMember(FName(*Request.MemberName));
			Var->CreateNewGuid();
			Var->PostPlacedNewNode();
			Var->AllocateDefaultPins();
			Graph.AddNode(Var, false, false);
			NewNode = Var;
		}
		else if (Request.Type.Equals(TEXT("Branch"), ESearchCase::IgnoreCase) ||
			Request.Type.Equals(TEXT("IfThenElse"), ESearchCase::IgnoreCase))
		{
			NewNode = SpawnSimpleNode<UK2Node_IfThenElse>(Graph);
		}
		else if (Request.Type.Equals(TEXT("Sequence"), ESearchCase::IgnoreCase))
		{
			NewNode = SpawnSimpleNode<UK2Node_ExecutionSequence>(Graph);
		}
		else if (Request.Type.Equals(TEXT("Self"), ESearchCase::IgnoreCase))
		{
			NewNode = SpawnSimpleNode<UK2Node_Self>(Graph);
		}
		else if (Request.Type.Equals(TEXT("MakeArray"), ESearchCase::IgnoreCase))
		{
			NewNode = SpawnSimpleNode<UK2Node_MakeArray>(Graph);
		}
		else if (Request.Type.Equals(TEXT("Select"), ESearchCase::IgnoreCase))
		{
			NewNode = SpawnSimpleNode<UK2Node_Select>(Graph);
		}
		else if (Request.Type.Equals(TEXT("Cast"), ESearchCase::IgnoreCase) ||
			Request.Type.Equals(TEXT("DynamicCast"), ESearchCase::IgnoreCase))
		{
			UClass* TargetClass = FindClassByName(Request.MemberName);
			if (!TargetClass && Request.MemberName.StartsWith(TEXT("/")))
			{
				FString ObjectPath = Request.MemberName;
				if (!ObjectPath.Contains(TEXT("."))) { ObjectPath += TEXT(".") + FPaths::GetCleanFilename(Request.MemberName); }
				TargetClass = LoadObject<UClass>(nullptr, *(ObjectPath + TEXT("_C")));
			}
			if (!TargetClass)
			{
				OutError = FString::Printf(TEXT("Cast needs a target class in 'memberName'; '%s' did not resolve."), *Request.MemberName);
				return false;
			}
			UK2Node_DynamicCast* Cast = NewObject<UK2Node_DynamicCast>(&Graph);
			Cast->TargetType = TargetClass;
			Cast->CreateNewGuid();
			Cast->PostPlacedNewNode();
			Cast->AllocateDefaultPins();
			Graph.AddNode(Cast, false, false);
			NewNode = Cast;
		}
		else if (Request.Type.Equals(TEXT("Macro"), ESearchCase::IgnoreCase) ||
			Request.Type.Equals(TEXT("MacroInstance"), ESearchCase::IgnoreCase))
		{
			UEdGraph* MacroGraph = ResolveMacroGraph(Request.MemberParent, Request.MemberName, OutError);
			if (!MacroGraph)
			{
				return false;
			}
			UK2Node_MacroInstance* Macro = NewObject<UK2Node_MacroInstance>(&Graph);
			Macro->SetMacroGraph(MacroGraph);
			Macro->CreateNewGuid();
			Macro->PostPlacedNewNode();
			Macro->AllocateDefaultPins();
			Graph.AddNode(Macro, false, false);
			NewNode = Macro;
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported node type '%s'."), *Request.Type);
			return false;
		}

		if (!NewNode)
		{
			OutError = TEXT("Node creation failed.");
			return false;
		}

		PlaceNode(*NewNode, Request, Graph);
		NewNode->Modify();

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);

		const TMap<const UEdGraphNode*, FString> NodeIds = BlueprintReader::ComputeNodeIds(Graph);
		OutNode = BlueprintReader::NodeToJson(*NewNode, NodeIds);
		return true;
	}

	bool DeleteNode(UBlueprint& Blueprint, UEdGraph& Graph, const FString& NodeRef, FString& OutError)
	{
		UEdGraphNode* Node = ResolveNode(Graph, NodeRef, OutError);
		if (!Node)
		{
			return false;
		}
		if (!Node->CanUserDeleteNode())
		{
			OutError = FString::Printf(TEXT("Node '%s' cannot be deleted."), *NodeRef);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("DeleteNode", "UE CLI: Delete Node"));
		FBlueprintEditorUtils::RemoveNode(&Blueprint, Node, /*bDontRecompile*/ true);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		return true;
	}

	bool SetPinValue(UBlueprint& Blueprint, UEdGraph& Graph, const FString& NodeRef, const FString& PinName, const FString& Value, FString& OutError)
	{
		const UEdGraphSchema_K2* Schema = K2Schema(Graph);
		if (!Schema)
		{
			OutError = TEXT("Graph is not a K2 (Blueprint) graph.");
			return false;
		}

		UEdGraphPin* Pin = ResolvePin(Graph, NodeRef + TEXT(".") + PinName, OutError);
		if (!Pin)
		{
			return false;
		}
		if (Pin->Direction != EGPD_Input)
		{
			OutError = FString::Printf(TEXT("Pin '%s' is an output; only input pins carry default values."), *PinName);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("SetPinValue", "UE CLI: Set Pin Value"));
		Pin->Modify();
		Schema->TrySetDefaultValue(*Pin, Value);
		FBlueprintEditorUtils::MarkBlueprintAsModified(&Blueprint);
		return true;
	}

	bool ConnectPins(UBlueprint& Blueprint, UEdGraph& Graph, const FString& FromRef, const FString& ToRef, FString& OutError)
	{
		// Any schema: a state machine graph's schema turns a state-to-state wire into a transition.
		const UEdGraphSchema* Schema = Graph.GetSchema();
		if (!Schema)
		{
			OutError = TEXT("Graph has no schema.");
			return false;
		}

		UEdGraphPin* From = ResolvePin(Graph, FromRef, OutError);
		if (!From)
		{
			return false;
		}
		UEdGraphPin* To = ResolvePin(Graph, ToRef, OutError);
		if (!To)
		{
			return false;
		}

		const FPinConnectionResponse Response = Schema->CanCreateConnection(From, To);
		if (Response.Response == CONNECT_RESPONSE_DISALLOW)
		{
			OutError = FString::Printf(TEXT("Cannot connect %s -> %s: %s"), *FromRef, *ToRef, *Response.Message.ToString());
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("ConnectPins", "UE CLI: Connect Pins"));
		From->GetOwningNode()->Modify();
		To->GetOwningNode()->Modify();
		if (!Schema->TryCreateConnection(From, To))
		{
			OutError = FString::Printf(TEXT("Failed to connect %s -> %s."), *FromRef, *ToRef);
			return false;
		}

		Activate(*From->GetOwningNode());
		Activate(*To->GetOwningNode());
		FBlueprintEditorUtils::MarkBlueprintAsModified(&Blueprint);
		return true;
	}

	bool DisconnectPins(UBlueprint& Blueprint, UEdGraph& Graph, const FString& FromRef, const FString& ToRef, FString& OutError)
	{
		// Any schema: AnimGraph / state machine graphs are not K2.
		const UEdGraphSchema* Schema = Graph.GetSchema();
		if (!Schema)
		{
			OutError = TEXT("Graph has no schema.");
			return false;
		}
		UEdGraphPin* From = ResolvePin(Graph, FromRef, OutError);
		if (!From)
		{
			return false;
		}
		UEdGraphPin* To = ResolvePin(Graph, ToRef, OutError);
		if (!To)
		{
			return false;
		}
		if (!From->LinkedTo.Contains(To))
		{
			OutError = FString::Printf(TEXT("%s and %s are not connected."), *FromRef, *ToRef);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("DisconnectPins", "UE CLI: Disconnect Pins"));
		From->GetOwningNode()->Modify();
		To->GetOwningNode()->Modify();
		Schema->BreakSinglePinLink(From, To);
		FBlueprintEditorUtils::MarkBlueprintAsModified(&Blueprint);
		return true;
	}

	bool MoveNode(UBlueprint& Blueprint, UEdGraph& Graph, const FString& NodeRef, int32 PosX, int32 PosY, FString& OutError)
	{
		UEdGraphNode* Node = ResolveNode(Graph, NodeRef, OutError);
		if (!Node)
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("MoveNode", "UE CLI: Move Node"));
		Node->Modify();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		FBlueprintEditorUtils::MarkBlueprintAsModified(&Blueprint);
		return true;
	}

	// ---------------------------------------------------------------- member variables

	namespace
	{
		FBPVariableDescription* FindVariable(UBlueprint& Blueprint, const FName Name)
		{
			return Blueprint.NewVariables.FindByPredicate(
				[Name](const FBPVariableDescription& Var) { return Var.VarName == Name; });
		}
	}

	bool AddVariable(UBlueprint& Blueprint, const FString& Name, const FString& TypeSpec, const FString& DefaultValue,
		bool bInstanceEditable, FString& OutError)
	{
		const FName VarName(*Name);
		if (FindVariable(Blueprint, VarName))
		{
			OutError = FString::Printf(TEXT("Blueprint '%s' already has a variable '%s'."), *Blueprint.GetName(), *Name);
			return false;
		}

		FEdGraphPinType PinType;
		if (!UECli::PinType::FromSpec(TypeSpec, PinType, OutError))
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("AddVariable", "UE CLI: Add Variable"));
		if (!FBlueprintEditorUtils::AddMemberVariable(&Blueprint, VarName, PinType, DefaultValue))
		{
			OutError = FString::Printf(TEXT("The editor rejected adding variable '%s'."), *Name);
			return false;
		}

		if (bInstanceEditable)
		{
			FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(&Blueprint, VarName, /*bNewBlueprintOnly*/ false);
		}
		return true;
	}

	bool RenameVariable(UBlueprint& Blueprint, const FString& From, const FString& To, FString& OutError)
	{
		if (!FindVariable(Blueprint, FName(*From)))
		{
			OutError = FString::Printf(TEXT("No variable '%s' on '%s'."), *From, *Blueprint.GetName());
			return false;
		}
		if (FindVariable(Blueprint, FName(*To)))
		{
			OutError = FString::Printf(TEXT("A variable '%s' already exists."), *To);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("RenameVariable", "UE CLI: Rename Variable"));
		FBlueprintEditorUtils::RenameMemberVariable(&Blueprint, FName(*From), FName(*To));
		return true;
	}

	bool RemoveVariable(UBlueprint& Blueprint, const FString& Name, FString& OutError)
	{
		if (!FindVariable(Blueprint, FName(*Name)))
		{
			OutError = FString::Printf(TEXT("No variable '%s' on '%s'."), *Name, *Blueprint.GetName());
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("RemoveVariable", "UE CLI: Remove Variable"));
		FBlueprintEditorUtils::RemoveMemberVariable(&Blueprint, FName(*Name));
		return true;
	}

	bool SetVariableDefault(UBlueprint& Blueprint, const FString& Name, const FString& Value, FString& OutError)
	{
		FBPVariableDescription* Var = FindVariable(Blueprint, FName(*Name));
		if (!Var)
		{
			OutError = FString::Printf(TEXT("No variable '%s' on '%s'."), *Name, *Blueprint.GetName());
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("SetVariableDefault", "UE CLI: Set Variable Default"));
		Blueprint.Modify();
		Var->DefaultValue = Value;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		return true;
	}

	// ---------------------------------------------------------------- function graphs

	bool AddFunction(UBlueprint& Blueprint, const FString& Name,
		const TArray<FParamSpec>& Inputs, const TArray<FParamSpec>& Outputs,
		TSharedRef<FJsonObject>& OutFunction, FString& OutError)
	{
		const FName FunctionName(*Name);
		for (const UEdGraph* Existing : Blueprint.FunctionGraphs)
		{
			if (Existing && Existing->GetFName() == FunctionName)
			{
				OutError = FString::Printf(TEXT("Blueprint '%s' already has a function '%s'."), *Blueprint.GetName(), *Name);
				return false;
			}
		}

		// Validate every parameter type before creating anything, so a bad type leaves no half-built graph.
		TArray<FEdGraphPinType> InputTypes, OutputTypes;
		for (const FParamSpec& Param : Inputs)
		{
			if (!UECli::PinType::FromSpec(Param.Type, InputTypes.AddDefaulted_GetRef(), OutError))
			{
				return false;
			}
		}
		for (const FParamSpec& Param : Outputs)
		{
			if (!UECli::PinType::FromSpec(Param.Type, OutputTypes.AddDefaulted_GetRef(), OutError))
			{
				return false;
			}
		}

		const FScopedTransaction Transaction(LOCTEXT("AddFunction", "UE CLI: Add Function"));
		Blueprint.Modify();

		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
			&Blueprint, FunctionName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(&Blueprint, Graph, /*bIsUserCreated*/ true, static_cast<UClass*>(nullptr));

		UK2Node_FunctionEntry* Entry = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if ((Entry = Cast<UK2Node_FunctionEntry>(Node)) != nullptr)
			{
				break;
			}
		}
		if (!Entry)
		{
			OutError = TEXT("Function graph was created without an entry node.");
			return false;
		}

		for (int32 Index = 0; Index < Inputs.Num(); ++Index)
		{
			Entry->CreateUserDefinedPin(FName(*Inputs[Index].Name), InputTypes[Index], EGPD_Output, /*bUseUniqueName*/ true);
		}

		if (Outputs.Num() > 0)
		{
			if (UK2Node_FunctionResult* Result = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry))
			{
				for (int32 Index = 0; Index < Outputs.Num(); ++Index)
				{
					Result->CreateUserDefinedPin(FName(*Outputs[Index].Name), OutputTypes[Index], EGPD_Input, /*bUseUniqueName*/ true);
				}
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);

		OutFunction->SetStringField(TEXT("name"), Name);
		OutFunction->SetStringField(TEXT("graph"), Graph->GetName());
		OutFunction->SetNumberField(TEXT("inputs"), Inputs.Num());
		OutFunction->SetNumberField(TEXT("outputs"), Outputs.Num());
		return true;
	}

	bool RemoveFunction(UBlueprint& Blueprint, const FString& Name, FString& OutError)
	{
		const FName FunctionName(*Name);
		UEdGraph* Target = nullptr;
		for (UEdGraph* Graph : Blueprint.FunctionGraphs)
		{
			if (Graph && Graph->GetFName() == FunctionName)
			{
				Target = Graph;
				break;
			}
		}
		if (!Target)
		{
			OutError = FString::Printf(TEXT("Blueprint '%s' has no function '%s'."), *Blueprint.GetName(), *Name);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("RemoveFunction", "UE CLI: Remove Function"));
		FBlueprintEditorUtils::RemoveGraph(&Blueprint, Target);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		return true;
	}

	// ---------------------------------------------------------------- interfaces

	namespace
	{
		UClass* ResolveInterfaceClass(const FString& Ref, FString& OutError)
		{
			UClass* InterfaceClass = nullptr;
			if (Ref.StartsWith(TEXT("/")))
			{
				FString ObjectPath = Ref;
				if (!ObjectPath.Contains(TEXT(".")))
				{
					FString AssetName;
					Ref.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
					ObjectPath = Ref + TEXT(".") + AssetName;
				}
				InterfaceClass = LoadObject<UClass>(nullptr, *(ObjectPath + TEXT("_C")));
			}
			else
			{
				InterfaceClass = FindFirstObject<UClass>(*Ref, EFindFirstObjectOptions::NativeFirst);
				if (!InterfaceClass && !Ref.StartsWith(TEXT("U")))
				{
					InterfaceClass = FindFirstObject<UClass>(*(TEXT("U") + Ref), EFindFirstObjectOptions::NativeFirst);
				}
			}

			if (!InterfaceClass || !InterfaceClass->HasAnyClassFlags(CLASS_Interface))
			{
				OutError = FString::Printf(TEXT("'%s' is not an interface."), *Ref);
				return nullptr;
			}
			return InterfaceClass;
		}
	}

	TArray<FString> ListInterfaces(UBlueprint& Blueprint)
	{
		TArray<UClass*> Implemented;
		FBlueprintEditorUtils::FindImplementedInterfaces(&Blueprint, /*bGetAllInterfaces*/ false, Implemented);

		TArray<FString> Names;
		for (const UClass* Interface : Implemented)
		{
			if (Interface)
			{
				Names.Add(Interface->GetName());
			}
		}
		return Names;
	}

	bool AddInterface(UBlueprint& Blueprint, const FString& InterfaceRef, FString& OutError)
	{
		UClass* InterfaceClass = ResolveInterfaceClass(InterfaceRef, OutError);
		if (!InterfaceClass)
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("AddInterface", "UE CLI: Add Interface"));
		if (!FBlueprintEditorUtils::ImplementNewInterface(&Blueprint, InterfaceClass->GetClassPathName()))
		{
			OutError = FString::Printf(TEXT("The editor rejected implementing '%s' (already implemented?)."), *InterfaceClass->GetName());
			return false;
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		return true;
	}

	bool RemoveInterface(UBlueprint& Blueprint, const FString& InterfaceRef, bool bPreserveFunctions, FString& OutError)
	{
		UClass* InterfaceClass = ResolveInterfaceClass(InterfaceRef, OutError);
		if (!InterfaceClass)
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("RemoveInterface", "UE CLI: Remove Interface"));
		FBlueprintEditorUtils::RemoveInterface(&Blueprint, InterfaceClass->GetClassPathName(), bPreserveFunctions);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
