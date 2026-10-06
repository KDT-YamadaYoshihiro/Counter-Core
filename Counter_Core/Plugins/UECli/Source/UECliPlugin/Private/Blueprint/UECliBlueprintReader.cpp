// Copyright UE CLI. All rights reserved.

#include "Blueprint/UECliBlueprintReader.h"

#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/BlueprintSupport.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Variable.h"
#include "UECliLog.h"
#include "UObject/Class.h"

namespace UECli::BlueprintReader
{
	namespace
	{
		FString CleanClassPath(const FString& InPath)
		{
			FString Path = InPath;
			Path.RemoveFromStart(TEXT("Class'"));
			Path.RemoveFromEnd(TEXT("'"));
			int32 DotIndex;
			if (Path.FindLastChar(TEXT('.'), DotIndex))
			{
				return Path.RightChop(DotIndex + 1);
			}
			return Path;
		}

		FString NormalizeBlueprintTypeTag(const FString& Raw)
		{
			FString Value = Raw;
			return Value.RemoveFromStart(TEXT("BPTYPE_")) ? Value : Raw;
		}

		const TCHAR* BlueprintTypeName(EBlueprintType Type)
		{
			switch (Type)
			{
			case BPTYPE_Const:            return TEXT("Const");
			case BPTYPE_MacroLibrary:     return TEXT("MacroLibrary");
			case BPTYPE_Interface:        return TEXT("Interface");
			case BPTYPE_LevelScript:      return TEXT("LevelScript");
			case BPTYPE_FunctionLibrary:  return TEXT("FunctionLibrary");
			case BPTYPE_Normal:
			default:                      return TEXT("Normal");
			}
		}

		const TCHAR* ContainerTypeName(EPinContainerType Type)
		{
			switch (Type)
			{
			case EPinContainerType::Array: return TEXT("Array");
			case EPinContainerType::Set:   return TEXT("Set");
			case EPinContainerType::Map:   return TEXT("Map");
			default:                       return TEXT("None");
			}
		}

		FString PinTypeToString(const FEdGraphPinType& PinType)
		{
			FString Category = PinType.PinCategory.ToString();
			if (const UObject* SubObject = PinType.PinSubCategoryObject.Get())
			{
				Category += TEXT("<") + SubObject->GetName() + TEXT(">");
			}
			else if (!PinType.PinSubCategory.IsNone())
			{
				Category += TEXT("<") + PinType.PinSubCategory.ToString() + TEXT(">");
			}

			switch (PinType.ContainerType)
			{
			case EPinContainerType::Array: return TEXT("TArray<") + Category + TEXT(">");
			case EPinContainerType::Set:   return TEXT("TSet<") + Category + TEXT(">");
			case EPinContainerType::Map:   return TEXT("TMap<") + Category + TEXT(">");
			default:                       return Category;
			}
		}

		void DescribeNode(const UEdGraphNode& Node, FString& OutType, FString& OutMember, FString& OutMemberParent, bool& OutPure)
		{
			OutType = Node.GetClass()->GetName();
			OutType.RemoveFromStart(TEXT("K2Node_"));
			OutMember.Empty();
			OutMemberParent.Empty();
			OutPure = false;

			if (const UK2Node* K2Node = Cast<UK2Node>(&Node))
			{
				OutPure = K2Node->IsNodePure();
			}

			if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(&Node))
			{
				OutType = TEXT("CallFunction");
				OutMember = Call->FunctionReference.GetMemberName().ToString();
				if (const UClass* Parent = Call->FunctionReference.GetMemberParentClass())
				{
					OutMemberParent = Parent->GetName();
				}
			}
			else if (const UK2Node_Event* Event = Cast<UK2Node_Event>(&Node))
			{
				OutType = TEXT("Event");
				OutMember = Event->EventReference.GetMemberName() != NAME_None
					? Event->EventReference.GetMemberName().ToString()
					: Event->CustomFunctionName.ToString();
				if (const UClass* Parent = Event->EventReference.GetMemberParentClass())
				{
					OutMemberParent = Parent->GetName();
				}
			}
			else if (const UK2Node_Variable* Var = Cast<UK2Node_Variable>(&Node))
			{
				OutMember = Var->GetVarName().ToString();
				if (const UClass* Parent = Var->VariableReference.GetMemberParentClass())
				{
					OutMemberParent = Parent->GetName();
				}
			}
			else if (const UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(&Node))
			{
				OutType = TEXT("MacroInstance");
				if (const UEdGraph* MacroGraph = Macro->GetMacroGraph())
				{
					OutMember = MacroGraph->GetName();
				}
			}
			else if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(&Node))
			{
				OutType = TEXT("Cast");
				if (const UClass* Target = CastNode->TargetType)
				{
					OutMember = Target->GetName();
				}
			}
		}

		TSharedRef<FJsonObject> PinToJson(const UEdGraphPin& Pin, const TMap<const UEdGraphNode*, FString>& NodeIds)
		{
			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("id"), Pin.PinName.ToString());
			if (!Pin.PinFriendlyName.IsEmpty())
			{
				Json->SetStringField(TEXT("name"), Pin.PinFriendlyName.ToString());
			}
			Json->SetStringField(TEXT("direction"), Pin.Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
			Json->SetStringField(TEXT("category"), Pin.PinType.PinCategory.ToString());

			if (const UObject* SubObject = Pin.PinType.PinSubCategoryObject.Get())
			{
				Json->SetStringField(TEXT("subCategory"), SubObject->GetName());
			}
			else if (!Pin.PinType.PinSubCategory.IsNone())
			{
				Json->SetStringField(TEXT("subCategory"), Pin.PinType.PinSubCategory.ToString());
			}

			if (Pin.PinType.ContainerType != EPinContainerType::None)
			{
				Json->SetStringField(TEXT("containerType"), ContainerTypeName(Pin.PinType.ContainerType));
			}

			if (!Pin.DefaultValue.IsEmpty())
			{
				Json->SetStringField(TEXT("defaultValue"), Pin.DefaultValue);
			}
			else if (Pin.DefaultObject)
			{
				Json->SetStringField(TEXT("defaultValue"), Pin.DefaultObject->GetPathName());
			}
			else if (!Pin.DefaultTextValue.IsEmpty())
			{
				Json->SetStringField(TEXT("defaultValue"), Pin.DefaultTextValue.ToString());
			}

			TArray<TSharedPtr<FJsonValue>> Linked;
			for (const UEdGraphPin* Other : Pin.LinkedTo)
			{
				if (!Other || !Other->GetOwningNodeUnchecked())
				{
					continue;
				}
				if (const FString* OtherId = NodeIds.Find(Other->GetOwningNode()))
				{
					Linked.Add(MakeShared<FJsonValueString>(*OtherId + TEXT(".") + Other->PinName.ToString()));
				}
			}
			if (Linked.Num() > 0)
			{
				Json->SetArrayField(TEXT("linkedTo"), Linked);
			}

			return Json;
		}

		/** "FromState->ToState" for a transition rule graph, empty otherwise. */
		FString TransitionName(const UEdGraph& Graph)
		{
			if (const UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Graph.GetOuter()))
			{
				const UAnimStateNodeBase* From = Transition->GetPreviousState();
				const UAnimStateNodeBase* To = Transition->GetNextState();
				if (From && To)
				{
					return From->GetStateName() + TEXT("->") + To->GetStateName();
				}
			}
			return FString();
		}

		void CollectGraphs(UBlueprint& Blueprint, TArray<TPair<UEdGraph*, FString>>& OutGraphs)
		{
			for (UEdGraph* Graph : Blueprint.UbergraphPages)
			{
				OutGraphs.Emplace(Graph, TEXT("EventGraph"));
			}
			for (UEdGraph* Graph : Blueprint.FunctionGraphs)
			{
				const bool bConstruction = Graph && Graph->GetFName() == UEdGraphSchema_K2::FN_UserConstructionScript;
				OutGraphs.Emplace(Graph, bConstruction ? TEXT("Construction") : TEXT("Function"));
			}
			for (UEdGraph* Graph : Blueprint.MacroGraphs)
			{
				OutGraphs.Emplace(Graph, TEXT("Macro"));
			}
			for (UEdGraph* Graph : Blueprint.DelegateSignatureGraphs)
			{
				OutGraphs.Emplace(Graph, TEXT("Delegate"));
			}

			// Nested graphs (state machines, states, transition rules, collapsed graphs),
			// typed by graph class, e.g. AnimationStateMachineGraph.
			for (int32 Index = 0; Index < OutGraphs.Num(); ++Index)
			{
				if (const UEdGraph* Parent = OutGraphs[Index].Key)
				{
					for (UEdGraph* Sub : Parent->SubGraphs)
					{
						if (Sub)
						{
							OutGraphs.Emplace(Sub, Sub->GetClass()->GetName());
						}
					}
				}
			}
		}
	}

	TMap<const UEdGraphNode*, FString> ComputeNodeIds(const UEdGraph& Graph)
	{
		TMap<const UEdGraphNode*, FString> NodeIds;
		NodeIds.Reserve(Graph.Nodes.Num());
		for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
		{
			if (const UEdGraphNode* Node = Graph.Nodes[Index])
			{
				NodeIds.Add(Node, FString::Printf(TEXT("N%d"), Index + 1));
			}
		}
		return NodeIds;
	}

	TSharedRef<FJsonObject> NodeToJson(const UEdGraphNode& Node, const TMap<const UEdGraphNode*, FString>& NodeIds)
	{
		FString Type, Member, MemberParent;
		bool bPure = false;
		DescribeNode(Node, Type, Member, MemberParent, bPure);

		const TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		if (const FString* Id = NodeIds.Find(&Node))
		{
			NodeJson->SetStringField(TEXT("id"), *Id);
		}
		NodeJson->SetStringField(TEXT("guid"), Node.NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		NodeJson->SetStringField(TEXT("type"), Type);
		if (!Member.IsEmpty())
		{
			NodeJson->SetStringField(TEXT("memberName"), Member);
		}
		if (!MemberParent.IsEmpty())
		{
			NodeJson->SetStringField(TEXT("memberParent"), MemberParent);
		}
		NodeJson->SetStringField(TEXT("title"), Node.GetNodeTitle(ENodeTitleType::ListView).ToString());

		TArray<TSharedPtr<FJsonValue>> Position;
		Position.Add(MakeShared<FJsonValueNumber>(Node.NodePosX));
		Position.Add(MakeShared<FJsonValueNumber>(Node.NodePosY));
		NodeJson->SetArrayField(TEXT("position"), Position);

		if (!Node.IsNodeEnabled())
		{
			NodeJson->SetBoolField(TEXT("enabled"), false);
		}
		const bool bGhost = Node.IsAutomaticallyPlacedGhostNode();
		if (bGhost)
		{
			NodeJson->SetBoolField(TEXT("isGhost"), true);
		}
		if (!Node.NodeComment.IsEmpty() && !bGhost)
		{
			NodeJson->SetStringField(TEXT("comment"), Node.NodeComment);
		}
		if (bPure)
		{
			NodeJson->SetBoolField(TEXT("isPure"), true);
		}

		TArray<TSharedPtr<FJsonValue>> PinsJson;
		for (const UEdGraphPin* Pin : Node.Pins)
		{
			if (Pin && !Pin->bHidden)
			{
				PinsJson.Add(MakeShared<FJsonValueObject>(PinToJson(*Pin, NodeIds)));
			}
		}
		NodeJson->SetArrayField(TEXT("pins"), PinsJson);

		return NodeJson;
	}

	UBlueprint* LoadBlueprintByPath(const FString& Path, FString& OutError)
	{
		FString ObjectPath = Path;
		if (!ObjectPath.Contains(TEXT(".")))
		{
			FString AssetName;
			if (Path.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd) && !AssetName.IsEmpty())
			{
				ObjectPath = Path + TEXT(".") + AssetName;
			}
		}

		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		if (!Blueprint)
		{
			OutError = FString::Printf(TEXT("No Blueprint found at '%s'."), *Path);
		}
		return Blueprint;
	}

	TArray<TSharedPtr<FJsonValue>> ListBlueprints(const FString& PackagePathPrefix)
	{
		TArray<TSharedPtr<FJsonValue>> Result;

		const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		FString Prefix = PackagePathPrefix.IsEmpty() ? TEXT("/Game") : PackagePathPrefix;
		if (Prefix.Len() > 1)
		{
			Prefix.RemoveFromEnd(TEXT("/")); // "/Game/Foo/" is a valid way to say "/Game/Foo"
		}

		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		if (Prefix != TEXT("/"))
		{
			Filter.PackagePaths.Add(FName(*Prefix));
			Filter.bRecursivePaths = true;
		}

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });

		for (const FAssetData& Asset : Assets)
		{
			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("path"), Asset.PackageName.ToString());
			Json->SetStringField(TEXT("name"), Asset.AssetName.ToString());

			FString ParentClass;
			if (Asset.GetTagValue(FBlueprintTags::NativeParentClassPath, ParentClass) ||
				Asset.GetTagValue(FBlueprintTags::ParentClassPath, ParentClass))
			{
				Json->SetStringField(TEXT("parentClass"), CleanClassPath(ParentClass));
			}

			FString BlueprintType;
			if (Asset.GetTagValue(FBlueprintTags::BlueprintType, BlueprintType))
			{
				Json->SetStringField(TEXT("blueprintType"), NormalizeBlueprintTypeTag(BlueprintType));
			}

			Result.Add(MakeShared<FJsonValueObject>(Json));
		}

		return Result;
	}

	TSharedRef<FJsonObject> InspectBlueprint(UBlueprint& Blueprint)
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("path"), Blueprint.GetOutermost()->GetName());
		Json->SetStringField(TEXT("name"), Blueprint.GetName());
		if (Blueprint.ParentClass)
		{
			Json->SetStringField(TEXT("parentClass"), Blueprint.ParentClass->GetName());
		}
		Json->SetStringField(TEXT("blueprintType"), BlueprintTypeName(Blueprint.BlueprintType));
		Json->SetBoolField(TEXT("isCompiled"), Blueprint.Status == BS_UpToDate);

		TArray<TSharedPtr<FJsonValue>> Variables;
		for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
		{
			const TSharedRef<FJsonObject> VarJson = MakeShared<FJsonObject>();
			VarJson->SetStringField(TEXT("name"), Variable.VarName.ToString());
			VarJson->SetStringField(TEXT("type"), PinTypeToString(Variable.VarType));
			if (Variable.VarType.ContainerType != EPinContainerType::None)
			{
				VarJson->SetStringField(TEXT("containerType"), ContainerTypeName(Variable.VarType.ContainerType));
			}
			if (!Variable.DefaultValue.IsEmpty())
			{
				VarJson->SetStringField(TEXT("defaultValue"), Variable.DefaultValue);
			}
			VarJson->SetBoolField(TEXT("instanceEditable"),
				(Variable.PropertyFlags & CPF_DisableEditOnInstance) == 0 && (Variable.PropertyFlags & CPF_Edit) != 0);
			if (!Variable.Category.IsEmpty() && !Variable.Category.EqualTo(FText::FromString(TEXT("Default"))))
			{
				VarJson->SetStringField(TEXT("category"), Variable.Category.ToString());
			}
			Variables.Add(MakeShared<FJsonValueObject>(VarJson));
		}
		Json->SetArrayField(TEXT("variables"), Variables);

		TArray<TPair<UEdGraph*, FString>> Graphs;
		CollectGraphs(Blueprint, Graphs);

		TArray<TSharedPtr<FJsonValue>> GraphsJson;
		for (const TPair<UEdGraph*, FString>& Entry : Graphs)
		{
			if (!Entry.Key)
			{
				continue;
			}
			const TSharedRef<FJsonObject> GraphJson = MakeShared<FJsonObject>();
			GraphJson->SetStringField(TEXT("name"), Entry.Key->GetName());
			GraphJson->SetStringField(TEXT("type"), Entry.Value);
			GraphJson->SetNumberField(TEXT("nodeCount"), Entry.Key->Nodes.Num());
			const FString Transition = TransitionName(*Entry.Key);
			if (!Transition.IsEmpty())
			{
				GraphJson->SetStringField(TEXT("transition"), Transition);
			}
			GraphsJson.Add(MakeShared<FJsonValueObject>(GraphJson));
		}
		Json->SetArrayField(TEXT("graphs"), GraphsJson);

		return Json;
	}

	UEdGraph* FindGraph(UBlueprint& Blueprint, const FString& GraphName, FString& OutType)
	{
		TArray<TPair<UEdGraph*, FString>> Graphs;
		CollectGraphs(Blueprint, Graphs);

		// "From->To" picks a transition rule graph by its states.
		FString From, To;
		if (GraphName.Split(TEXT("->"), &From, &To))
		{
			const FString Wanted = From.TrimStartAndEnd() + TEXT("->") + To.TrimStartAndEnd();
			for (const TPair<UEdGraph*, FString>& Entry : Graphs)
			{
				if (Entry.Key && TransitionName(*Entry.Key) == Wanted)
				{
					OutType = Entry.Value;
					return Entry.Key;
				}
			}
			return nullptr;
		}

		// "Name#k" picks the k-th (1-based) graph called Name, in inspect order —
		// every transition rule graph is called "Transition".
		FString Name = GraphName;
		int32 Wanted = 1;
		FString Left, Right;
		if (GraphName.Split(TEXT("#"), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd) &&
			Right.IsNumeric() && FCString::Atoi(*Right) > 0)
		{
			Name = Left;
			Wanted = FCString::Atoi(*Right);
		}

		int32 Seen = 0;
		for (const TPair<UEdGraph*, FString>& Entry : Graphs)
		{
			if (Entry.Key && Entry.Key->GetName() == Name && ++Seen == Wanted)
			{
				OutType = Entry.Value;
				return Entry.Key;
			}
		}
		return nullptr;
	}

	bool ReadGraph(UBlueprint& Blueprint, const FString& GraphName, TSharedRef<FJsonObject>& OutGraph, FString& OutError)
	{
		FString GraphType;
		UEdGraph* Graph = FindGraph(Blueprint, GraphName, GraphType);
		if (!Graph)
		{
			OutError = FString::Printf(TEXT("Blueprint '%s' has no graph named '%s'."), *Blueprint.GetName(), *GraphName);
			return false;
		}

		const TMap<const UEdGraphNode*, FString> NodeIds = ComputeNodeIds(*Graph);

		TArray<TSharedPtr<FJsonValue>> NodesJson;
		TArray<TSharedPtr<FJsonValue>> ConnectionsJson;

		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			NodesJson.Add(MakeShared<FJsonValueObject>(NodeToJson(*Node, NodeIds)));

			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output)
				{
					continue;
				}
				for (const UEdGraphPin* Target : Pin->LinkedTo)
				{
					if (!Target || !Target->GetOwningNodeUnchecked())
					{
						continue;
					}
					if (const FString* TargetId = NodeIds.Find(Target->GetOwningNode()))
					{
						const TSharedRef<FJsonObject> Conn = MakeShared<FJsonObject>();
						Conn->SetStringField(TEXT("from"), NodeIds[Node] + TEXT(".") + Pin->PinName.ToString());
						Conn->SetStringField(TEXT("to"), *TargetId + TEXT(".") + Target->PinName.ToString());
						ConnectionsJson.Add(MakeShared<FJsonValueObject>(Conn));
					}
				}
			}
		}

		OutGraph->SetStringField(TEXT("blueprint"), Blueprint.GetOutermost()->GetName());
		OutGraph->SetStringField(TEXT("name"), Graph->GetName());
		OutGraph->SetStringField(TEXT("type"), GraphType);
		OutGraph->SetArrayField(TEXT("nodes"), NodesJson);
		OutGraph->SetArrayField(TEXT("connections"), ConnectionsJson);
		return true;
	}
}
