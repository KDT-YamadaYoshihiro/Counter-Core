// Copyright UE CLI. All rights reserved.

#include "Blueprint/UECliBlueprintComponents.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "UECliLog.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "UECliBlueprintComponents"

namespace UECli::BlueprintComponents
{
	namespace
	{
		UClass* ResolveComponentClass(const FString& Ref, FString& OutError)
		{
			UClass* ComponentClass = nullptr;
			if (Ref.StartsWith(TEXT("/")))
			{
				FString ObjectPath = Ref;
				if (!ObjectPath.Contains(TEXT(".")))
				{
					FString AssetName;
					Ref.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
					ObjectPath = Ref + TEXT(".") + AssetName;
				}
				ComponentClass = LoadObject<UClass>(nullptr, *(ObjectPath + TEXT("_C")));
			}
			else
			{
				ComponentClass = FindFirstObject<UClass>(*Ref, EFindFirstObjectOptions::NativeFirst);
			}

			if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
			{
				OutError = FString::Printf(TEXT("'%s' is not an ActorComponent class."), *Ref);
				return nullptr;
			}
			return ComponentClass;
		}
	}

	TArray<TSharedPtr<FJsonValue>> ListComponents(UBlueprint& Blueprint)
	{
		TArray<TSharedPtr<FJsonValue>> Result;

		// Inherited (native / parent-BP) components from the CDO.
		if (Blueprint.ParentClass)
		{
			if (const AActor* DefaultActor = Cast<AActor>(Blueprint.ParentClass->GetDefaultObject()))
			{
				for (const UActorComponent* Component : DefaultActor->GetComponents())
				{
					if (!Component)
					{
						continue;
					}
					const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
					Json->SetStringField(TEXT("name"), Component->GetName());
					Json->SetStringField(TEXT("class"), Component->GetClass()->GetName());
					Json->SetBoolField(TEXT("inherited"), true);
					Result.Add(MakeShared<FJsonValueObject>(Json));
				}
			}
		}

		if (USimpleConstructionScript* SCS = Blueprint.SimpleConstructionScript)
		{
			for (USCS_Node* Node : SCS->GetAllNodes())
			{
				if (!Node)
				{
					continue;
				}
				const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
				Json->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
				Json->SetStringField(TEXT("class"), Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("(none)"));
				if (const USCS_Node* Parent = SCS->FindParentNode(Node))
				{
					Json->SetStringField(TEXT("parent"), Parent->GetVariableName().ToString());
				}
				else if (!Node->ParentComponentOrVariableName.IsNone())
				{
					Json->SetStringField(TEXT("parent"), Node->ParentComponentOrVariableName.ToString());
				}
				Result.Add(MakeShared<FJsonValueObject>(Json));
			}
		}

		return Result;
	}

	bool AddComponent(UBlueprint& Blueprint, const FString& ClassRef, const FString& Name, const FString& ParentName,
		TSharedRef<FJsonObject>& OutResult, FString& OutError)
	{
		USimpleConstructionScript* SCS = Blueprint.SimpleConstructionScript;
		if (!SCS)
		{
			OutError = TEXT("Blueprint has no construction script (is it an Actor Blueprint?).");
			return false;
		}

		UClass* ComponentClass = ResolveComponentClass(ClassRef, OutError);
		if (!ComponentClass)
		{
			return false;
		}

		const FName VariableName = Name.IsEmpty() ? NAME_None : FName(*Name);
		if (VariableName != NAME_None && SCS->FindSCSNode(VariableName))
		{
			OutError = FString::Printf(TEXT("A component named '%s' already exists."), *Name);
			return false;
		}

		USCS_Node* ParentNode = ParentName.IsEmpty() ? nullptr : SCS->FindSCSNode(FName(*ParentName));
		if (!ParentName.IsEmpty() && !ParentNode)
		{
			OutError = FString::Printf(TEXT("No component '%s' to attach under."), *ParentName);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("AddComponent", "UE CLI: Add Component"));
		Blueprint.Modify();
		SCS->Modify();

		USCS_Node* NewNode = SCS->CreateNode(ComponentClass, VariableName);

		if (ParentNode)
		{
			ParentNode->Modify();
			ParentNode->AddChildNode(NewNode);
		}
		else
		{
			SCS->AddNode(NewNode);
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);

		OutResult->SetStringField(TEXT("name"), NewNode->GetVariableName().ToString());
		OutResult->SetStringField(TEXT("class"), ComponentClass->GetName());
		if (ParentNode)
		{
			OutResult->SetStringField(TEXT("parent"), ParentNode->GetVariableName().ToString());
		}
		return true;
	}

	bool RemoveComponent(UBlueprint& Blueprint, const FString& Name, FString& OutError)
	{
		USimpleConstructionScript* SCS = Blueprint.SimpleConstructionScript;
		USCS_Node* Node = SCS ? SCS->FindSCSNode(FName(*Name)) : nullptr;
		if (!Node)
		{
			OutError = FString::Printf(TEXT("No component '%s' on '%s' (inherited components can't be removed here)."), *Name, *Blueprint.GetName());
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("RemoveComponent", "UE CLI: Remove Component"));
		Blueprint.Modify();
		SCS->Modify();
		SCS->RemoveNodeAndPromoteChildren(Node);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		return true;
	}

	bool SetComponentProperty(UBlueprint& Blueprint, const FString& ComponentName, const FString& Property, const FString& Value, FString& OutError)
	{
		USimpleConstructionScript* SCS = Blueprint.SimpleConstructionScript;
		USCS_Node* Node = SCS ? SCS->FindSCSNode(FName(*ComponentName)) : nullptr;
		if (!Node || !Node->ComponentTemplate)
		{
			OutError = FString::Printf(TEXT("No editable component '%s' on '%s' (inherited components can't be edited here)."), *ComponentName, *Blueprint.GetName());
			return false;
		}

		UActorComponent* Template = Node->ComponentTemplate;
		FProperty* Prop = Template->GetClass()->FindPropertyByName(FName(*Property));
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("Component class '%s' has no property '%s'."), *Template->GetClass()->GetName(), *Property);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("SetComponentProperty", "UE CLI: Set Component Property"));
		Blueprint.Modify();
		Template->Modify();

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Template);
		if (!Prop->ImportText_Direct(*Value, ValuePtr, Template, PPF_None))
		{
			OutError = FString::Printf(TEXT("Could not parse '%s' as a value for '%s'."), *Value, *Property);
			return false;
		}

		FPropertyChangedEvent ChangedEvent(Prop);
		Template->PostEditChangeProperty(ChangedEvent);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
