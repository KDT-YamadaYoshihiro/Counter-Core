// Copyright UE CLI. All rights reserved.

#include "Widget/UECliWidgetOps.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "UECliWidgetOps"

namespace UECli::WidgetOps
{
	namespace
	{
		UWidgetBlueprint* LoadWidgetBlueprint(const FString& Path, FString& OutError)
		{
			FString ObjectPath = Path;
			if (!ObjectPath.Contains(TEXT(".")))
			{
				ObjectPath = ObjectPath + TEXT(".") + FPackageName::GetShortName(ObjectPath);
			}
			UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
			{
				OutError = FString::Printf(TEXT("No Widget Blueprint at '%s'."), *Path);
				return nullptr;
			}
			return Blueprint;
		}

		UWidget* FindWidget(UWidgetBlueprint& Blueprint, const FString& Name, FString& OutError)
		{
			UWidget* Widget = Blueprint.WidgetTree->FindWidget(FName(*Name));
			if (Widget == nullptr)
			{
				OutError = FString::Printf(TEXT("'%s' has no widget '%s'."), *Blueprint.GetName(), *Name);
			}
			return Widget;
		}

		/** Native class name (Button, UButton), class path, or a Widget Blueprint path. */
		UClass* ResolveWidgetClass(const FString& Ref, FString& OutError)
		{
			UClass* Class = nullptr;
			if (Ref.StartsWith(TEXT("/Script/")))
			{
				Class = FindObject<UClass>(nullptr, *Ref);
			}
			else if (Ref.StartsWith(TEXT("/")))
			{
				FString Unused;
				if (UWidgetBlueprint* Child = LoadWidgetBlueprint(Ref, Unused))
				{
					Class = Child->GeneratedClass;
				}
			}
			else
			{
				const FString Bare = Ref.StartsWith(TEXT("U")) && Ref.Len() > 1 && FChar::IsUpper(Ref[1]) ? Ref.Mid(1) : Ref;
				for (TObjectIterator<UClass> It; It; ++It)
				{
					if (It->IsChildOf(UWidget::StaticClass()) && It->GetName().Equals(Bare, ESearchCase::IgnoreCase)
						&& !It->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
					{
						Class = *It;
						break;
					}
				}
			}

			if (Class == nullptr || !Class->IsChildOf(UWidget::StaticClass()))
			{
				OutError = FString::Printf(TEXT("'%s' is not a widget class (use a UMG class name like Button / TextBlock / CanvasPanel, or a Widget Blueprint path)."), *Ref);
				return nullptr;
			}
			if (Class->HasAnyClassFlags(CLASS_Abstract))
			{
				OutError = FString::Printf(TEXT("'%s' is abstract."), *Class->GetName());
				return nullptr;
			}
			return Class;
		}

		TSharedRef<FJsonObject> NodeToJson(UWidgetBlueprint& Blueprint, UWidget& Widget)
		{
			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Widget.GetName());
			Json->SetStringField(TEXT("class"), Widget.GetClass()->GetName());
			if (UPanelWidget* Parent = Widget.GetParent())
			{
				Json->SetStringField(TEXT("parent"), Parent->GetName());
				Json->SetNumberField(TEXT("index"), Parent->GetChildIndex(&Widget));
			}
			else
			{
				Json->SetNumberField(TEXT("index"), 0);
			}
			Json->SetBoolField(TEXT("isPanel"), Widget.IsA<UPanelWidget>());
			Json->SetBoolField(TEXT("isVariable"), Widget.bIsVariable);
			Json->SetStringField(TEXT("objectPath"), Widget.GetPathName());
			if (Widget.Slot != nullptr)
			{
				Json->SetStringField(TEXT("slotPath"), Widget.Slot->GetPathName());
				Json->SetStringField(TEXT("slotClass"), Widget.Slot->GetClass()->GetName());
			}
			return Json;
		}

		/** True if a widget of Candidate class is Self (or a subclass), or nests one anywhere in its own designer tree. */
		bool WouldContain(const UClass* Candidate, const UClass* Self, TSet<const UClass*>& Visited)
		{
			if (Candidate == nullptr || Visited.Contains(Candidate))
			{
				return false;
			}
			Visited.Add(Candidate);
			if (Candidate->IsChildOf(Self))
			{
				return true;
			}
			const UWidgetBlueprint* Nested = Cast<UWidgetBlueprint>(UBlueprint::GetBlueprintFromClass(Candidate));
			if (Nested == nullptr || Nested->WidgetTree == nullptr)
			{
				return false;
			}
			TArray<UWidget*> Widgets;
			Nested->WidgetTree->GetAllWidgets(Widgets);
			return Widgets.ContainsByPredicate([&](const UWidget* W) { return W && WouldContain(W->GetClass(), Self, Visited); });
		}

		bool WouldContain(const UClass* Candidate, const UClass* Self)
		{
			TSet<const UClass*> Visited;
			return WouldContain(Candidate, Self, Visited);
		}

		bool IsSelfOrDescendant(UWidget& Candidate, UWidget& Ancestor)
		{
			for (UWidget* Current = &Candidate; Current != nullptr; Current = Current->GetParent())
			{
				if (Current == &Ancestor)
				{
					return true;
				}
			}
			return false;
		}

		/** Insert under Parent, or become the root / go under the root panel when Parent is empty. */
		bool Attach(UWidgetBlueprint& Blueprint, UWidget& Widget, const FString& ParentName, int32 Index, FString& OutError, bool& bOutNotFound)
		{
			UWidgetTree& Tree = *Blueprint.WidgetTree;
			UPanelWidget* Panel = nullptr;
			if (ParentName.IsEmpty())
			{
				if (Tree.RootWidget == nullptr)
				{
					Tree.RootWidget = &Widget;
					return true;
				}
				Panel = Cast<UPanelWidget>(Tree.RootWidget);
				if (Panel == nullptr)
				{
					OutError = FString::Printf(TEXT("The root '%s' is not a panel; pass a parent."), *Tree.RootWidget->GetName());
					return false;
				}
			}
			else
			{
				UWidget* ParentWidget = FindWidget(Blueprint, ParentName, OutError);
				if (ParentWidget == nullptr)
				{
					bOutNotFound = true;
					return false;
				}
				Panel = Cast<UPanelWidget>(ParentWidget);
				if (Panel == nullptr)
				{
					OutError = FString::Printf(TEXT("'%s' (%s) is not a panel and cannot have children."), *ParentName, *ParentWidget->GetClass()->GetName());
					return false;
				}
			}

			if (IsSelfOrDescendant(*Panel, Widget))
			{
				OutError = TEXT("Cannot move a widget under itself or its own descendant.");
				return false;
			}
			if (!Panel->CanAddMoreChildren())
			{
				OutError = FString::Printf(TEXT("'%s' (%s) already has its only child."), *Panel->GetName(), *Panel->GetClass()->GetName());
				return false;
			}

			Panel->Modify();
			if (Index >= 0 && Index < Panel->GetChildrenCount())
			{
				Panel->InsertChildAt(Index, &Widget);
			}
			else
			{
				Panel->AddChild(&Widget);
			}
			return true;
		}

		/** Take a widget out of the tree (parent panel or root). */
		void Detach(UWidgetTree& Tree, UWidget& Widget)
		{
			if (Tree.RootWidget == &Widget)
			{
				Tree.RootWidget = nullptr;
			}
			else if (UPanelWidget* Parent = Widget.GetParent())
			{
				Parent->Modify();
				Parent->RemoveChild(&Widget);
			}
		}

		void Changed(UWidgetBlueprint& Blueprint)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		}
	}

	bool GetTree(const FString& Path, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(Path, OutError);
		bOutNotFound = Blueprint == nullptr;
		if (Blueprint == nullptr)
		{
			return false;
		}

		TArray<UWidget*> Widgets;
		Blueprint->WidgetTree->GetAllWidgets(Widgets);
		TArray<TSharedPtr<FJsonValue>> Nodes;
		for (UWidget* Widget : Widgets)
		{
			Nodes.Add(MakeShared<FJsonValueObject>(NodeToJson(*Blueprint, *Widget)));
		}

		TArray<TSharedPtr<FJsonValue>> Bindings;
		for (const FDelegateEditorBinding& Binding : Blueprint->Bindings)
		{
			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("widget"), Binding.ObjectName);
			Json->SetStringField(TEXT("property"), Binding.PropertyName.ToString());
			Json->SetStringField(TEXT("function"), Binding.FunctionName.ToString());
			Bindings.Add(MakeShared<FJsonValueObject>(Json));
		}

		Out->SetStringField(TEXT("blueprint"), Blueprint->GetPathName());
		if (Blueprint->WidgetTree->RootWidget != nullptr)
		{
			Out->SetStringField(TEXT("root"), Blueprint->WidgetTree->RootWidget->GetName());
		}
		Out->SetArrayField(TEXT("widgets"), Nodes);
		Out->SetArrayField(TEXT("bindings"), Bindings);
		return true;
	}

	bool AddWidget(const FString& Path, const FString& ClassRef, const FString& Name, const FString& Parent, int32 Index,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(Path, OutError);
		bOutNotFound = Blueprint == nullptr;
		if (Blueprint == nullptr)
		{
			return false;
		}
		UClass* Class = ResolveWidgetClass(ClassRef, OutError);
		if (Class == nullptr)
		{
			return false;
		}
		if (!Name.IsEmpty() && Blueprint->WidgetTree->FindWidget(FName(*Name)) != nullptr)
		{
			OutError = FString::Printf(TEXT("A widget named '%s' already exists."), *Name);
			return false;
		}
		if (Blueprint->GeneratedClass && WouldContain(Class, Blueprint->GeneratedClass))
		{
			OutError = FString::Printf(TEXT("'%s' is or contains '%s'; a Widget Blueprint cannot contain itself."), *Class->GetName(), *Blueprint->GetName());
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("AddWidget", "UE CLI: Add Widget"));
		Blueprint->Modify();
		Blueprint->WidgetTree->Modify();
		UWidget* Widget = Blueprint->WidgetTree->ConstructWidget<UWidget>(Class, Name.IsEmpty() ? NAME_None : FName(*Name));
		if (!Attach(*Blueprint, *Widget, Parent, Index, OutError, bOutNotFound))
		{
			Widget->Rename(nullptr, GetTransientPackage());
			return false;
		}

		Changed(*Blueprint);
		Out = NodeToJson(*Blueprint, *Widget);
		return true;
	}

	bool RemoveWidget(const FString& Path, const FString& Name, FString& OutError, bool& bOutNotFound)
	{
		UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(Path, OutError);
		UWidget* Widget = Blueprint ? FindWidget(*Blueprint, Name, OutError) : nullptr;
		bOutNotFound = Widget == nullptr;
		if (Widget == nullptr)
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("RemoveWidget", "UE CLI: Remove Widget"));
		Blueprint->Modify();
		Blueprint->WidgetTree->Modify();

		TArray<UWidget*> Subtree;
		UWidgetTree::GetChildWidgets(Widget, Subtree);
		Subtree.Add(Widget);
		Detach(*Blueprint->WidgetTree, *Widget);
		for (UWidget* Removed : Subtree)
		{
			Removed->Modify();
			// Out of the tree's outer so the names are free again.
			Removed->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
		}
		Blueprint->Bindings.RemoveAll([&Subtree](const FDelegateEditorBinding& Binding)
		{
			return Subtree.ContainsByPredicate([&Binding](const UWidget* W) { return W->GetName() == Binding.ObjectName; });
		});

		Changed(*Blueprint);
		return true;
	}

	bool Reparent(const FString& Path, const FString& Name, const FString& Parent, int32 Index,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(Path, OutError);
		UWidget* Widget = Blueprint ? FindWidget(*Blueprint, Name, OutError) : nullptr;
		bOutNotFound = Widget == nullptr;
		if (Widget == nullptr)
		{
			return false;
		}
		if (Parent.IsEmpty())
		{
			OutError = TEXT("'parent' is required.");
			return false;
		}

		UWidget* Target = FindWidget(*Blueprint, Parent, OutError);
		if (Target == nullptr)
		{
			bOutNotFound = true;
			return false;
		}
		if (IsSelfOrDescendant(*Target, *Widget))
		{
			OutError = TEXT("Cannot move a widget under itself or its own descendant.");
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("ReparentWidget", "UE CLI: Move Widget"));
		Blueprint->Modify();
		Blueprint->WidgetTree->Modify();
		Widget->Modify();
		UPanelWidget* OldParent = Widget->GetParent();
		const int32 OldIndex = OldParent ? OldParent->GetChildIndex(Widget) : -1;
		Detach(*Blueprint->WidgetTree, *Widget);
		if (!Attach(*Blueprint, *Widget, Parent, Index, OutError, bOutNotFound))
		{
			if (OldParent != nullptr)
			{
				OldParent->InsertChildAt(OldIndex, Widget);
			}
			else
			{
				Blueprint->WidgetTree->RootWidget = Widget;
			}
			return false;
		}

		Changed(*Blueprint);
		Out = NodeToJson(*Blueprint, *Widget);
		return true;
	}

	bool SetBinding(const FString& Path, const FString& WidgetName, const FString& Property, const FString& Function,
		FString& OutError, bool& bOutNotFound)
	{
		UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(Path, OutError);
		UWidget* Widget = Blueprint ? FindWidget(*Blueprint, WidgetName, OutError) : nullptr;
		bOutNotFound = Widget == nullptr;
		if (Widget == nullptr)
		{
			return false;
		}

		// Bindable properties are exposed as "<Property>Delegate" delegate members.
		const FString Base = Property.EndsWith(TEXT("Delegate")) ? Property.LeftChop(8) : Property;
		if (FindFProperty<FDelegateProperty>(Widget->GetClass(), FName(*(Base + TEXT("Delegate")))) == nullptr)
		{
			OutError = FString::Printf(TEXT("%s has no bindable property '%s'."), *Widget->GetClass()->GetName(), *Base);
			return false;
		}
		if (!Function.IsEmpty())
		{
			// Only the Blueprint's own function graphs: inherited native functions have the wrong signature.
			const bool bOwnFunction = Blueprint->FunctionGraphs.ContainsByPredicate(
				[&Function](const UEdGraph* Graph) { return Graph && Graph->GetFName() == FName(*Function); });
			if (!bOwnFunction)
			{
				OutError = FString::Printf(TEXT("'%s' has no function graph '%s' (add it with ue_add_function, then bind)."), *Blueprint->GetName(), *Function);
				return false;
			}
		}

		const FScopedTransaction Transaction(LOCTEXT("SetBinding", "UE CLI: Set Widget Binding"));
		Blueprint->Modify();
		Blueprint->Bindings.RemoveAll([&](const FDelegateEditorBinding& Binding)
		{
			return Binding.ObjectName == WidgetName && Binding.PropertyName == FName(*Base);
		});
		if (!Function.IsEmpty())
		{
			// Bindings must call a pure function (the designer's "Create Binding" makes one).
			for (UEdGraph* Graph : Blueprint->FunctionGraphs)
			{
				if (Graph != nullptr && Graph->GetFName() == FName(*Function))
				{
					TArray<UK2Node_FunctionEntry*> Entries;
					Graph->GetNodesOfClass(Entries);
					for (UK2Node_FunctionEntry* Entry : Entries)
					{
						Entry->Modify();
						Entry->AddExtraFlags(FUNC_BlueprintPure);
					}
				}
			}

			FDelegateEditorBinding Binding;
			Binding.ObjectName = WidgetName;
			Binding.PropertyName = FName(*Base);
			Binding.FunctionName = FName(*Function);
			Binding.Kind = EBindingKind::Function;
			Blueprint->Bindings.Add(Binding);
		}

		Changed(*Blueprint);
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
