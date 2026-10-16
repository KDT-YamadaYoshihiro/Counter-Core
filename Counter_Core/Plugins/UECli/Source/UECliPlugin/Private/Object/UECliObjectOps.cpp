// Copyright UE CLI. All rights reserved.

#include "Object/UECliObjectOps.h"

#include "Algo/AnyOf.h"
#include "Components/ActorComponent.h"
#include "Internationalization/Regex.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Level/UECliLevelOps.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UECliObjectOps"

namespace UECli::ObjectOps
{
	namespace
	{
		constexpr int32 MaxValueLength = 4000;

		/** The object to edit, and the Blueprint that owns it when it is a class default object. */
		UObject* Resolve(const FString& Ref, UBlueprint*& OutBlueprint)
		{
			OutBlueprint = nullptr;
			// (A Blueprint without a generated class comes back as null with OutBlueprint set.)
			if (Ref.StartsWith(TEXT("/")))
			{
				FString Path = Ref;
				if (!Path.Contains(TEXT(".")))
				{
					Path = Path + TEXT(".") + FPackageName::GetShortName(Path); // package path -> asset
				}
				UObject* Object = StaticFindObject(UObject::StaticClass(), nullptr, *Path);
				if (Object == nullptr)
				{
					Object = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
				}
				if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
				{
					OutBlueprint = Blueprint;
					return Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
				}
				return Object;
			}

			if (AActor* Actor = LevelOps::FindActor(Ref))
			{
				return Actor;
			}
			FString ActorRef, ComponentName;
			if (Ref.Split(TEXT("."), &ActorRef, &ComponentName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
			{
				if (AActor* Actor = LevelOps::FindActor(ActorRef))
				{
					for (UActorComponent* Component : Actor->GetComponents())
					{
						if (Component && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
						{
							return Component;
						}
					}
				}
			}
			return nullptr;
		}

		bool IsEditable(const FProperty& Prop)
		{
			return Prop.HasAnyPropertyFlags(CPF_Edit) && !Prop.HasAnyPropertyFlags(CPF_EditConst);
		}

		TSharedRef<FJsonObject> PropertyToJson(const FProperty& Prop, const void* ValuePtr, UObject* Owner)
		{
			FString Value;
			Prop.ExportTextItem_Direct(Value, ValuePtr, nullptr, Owner, PPF_None);
			if (Value.Len() > MaxValueLength)
			{
				Value = Value.Left(MaxValueLength) + TEXT("...");
			}

			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Prop.GetName());
			Json->SetStringField(TEXT("type"), Prop.GetCPPType());
			const FString Category = Prop.GetMetaData(TEXT("Category"));
			if (!Category.IsEmpty())
			{
				Json->SetStringField(TEXT("category"), Category);
			}
			Json->SetStringField(TEXT("value"), Value);
			Json->SetBoolField(TEXT("editable"), IsEditable(Prop));
			return Json;
		}

		/** ImportText that also rejects what UE silently coerces ("abc" -> 0.0) and trailing garbage. */
		bool ParseValue(const FProperty& Prop, const FString& Value, void* Dest, UObject* Owner)
		{
			const FString Trimmed = Value.TrimStartAndEnd();
			const FNumericProperty* Numeric = CastField<FNumericProperty>(&Prop);
			if (Numeric != nullptr && !Numeric->IsEnum())
			{
				const FRegexPattern Pattern(TEXT("^[+-]?(\\d+\\.?\\d*|\\.\\d+)([eE][+-]?\\d+)?$"));
				FRegexMatcher Matcher(Pattern, Trimmed);
				if (!Matcher.FindNext())
				{
					return false;
				}
			}
			if (CastField<FBoolProperty>(&Prop) != nullptr)
			{
				static const TCHAR* Accepted[] = { TEXT("true"), TEXT("false"), TEXT("1"), TEXT("0") };
				if (!Algo::AnyOf(Accepted, [&Trimmed](const TCHAR* A) { return Trimmed.Equals(A, ESearchCase::IgnoreCase); }))
				{
					return false;
				}
			}

			// Plain strings verbatim: ImportText would stop an unquoted value at the first space.
			if (const FStrProperty* Str = CastField<FStrProperty>(&Prop))
			{
				Str->SetPropertyValue(Dest, Value);
				return true;
			}
			if (const FNameProperty* Name = CastField<FNameProperty>(&Prop))
			{
				Name->SetPropertyValue(Dest, FName(*Value));
				return true;
			}
			if (const FTextProperty* Text = CastField<FTextProperty>(&Prop))
			{
				Text->SetPropertyValue(Dest, FText::FromString(Value));
				return true;
			}

			const TCHAR* End = Prop.ImportText_Direct(*Trimmed, Dest, Owner, PPF_None);
			if (End == nullptr)
			{
				return false;
			}
			while (FChar::IsWhitespace(*End))
			{
				++End;
			}
			return *End == TEXT('\0');
		}

		/** Why Resolve returned null: not found, or a Blueprint that has never been compiled. */
		FString ResolveError(const FString& Ref, const UBlueprint* Blueprint, bool& bOutNotFound)
		{
			bOutNotFound = Blueprint == nullptr;
			return Blueprint != nullptr
				? FString::Printf(TEXT("Blueprint '%s' has no generated class yet; compile it first."), *Ref)
				: FString::Printf(TEXT("No object '%s' (asset path, object path, or level actor label[.Component])."), *Ref);
		}
	}

	namespace
	{
		/**
		 * Walk "Member.Inner.Leaf" through struct properties; "Name[i]" picks an existing array
		 * element. For a write, the top property must be editable and no member EditConst.
		 * OutContainer is what OutLeaf->ContainerPtrToValuePtr() takes.
		 */
		bool WalkPath(UObject& Object, const FString& Property, bool bForWrite,
			FProperty*& OutTop, FProperty*& OutLeaf, void*& OutContainer, FString& OutError, bool* bOutMissing = nullptr)
		{
			TArray<FString> Segments;
			Property.ParseIntoArray(Segments, TEXT("."));
			if (Segments.Num() == 0)
			{
				OutError = TEXT("Property name is empty.");
				return false;
			}

			const UStruct* Struct = Object.GetClass();
			void* Container = &Object;
			FProperty* Top = nullptr;
			FProperty* Leaf = nullptr;
			for (int32 Index = 0; Index < Segments.Num(); ++Index)
			{
				FString Name = Segments[Index];
				int32 Element = INDEX_NONE;
				FString IndexText;
				if (Name.EndsWith(TEXT("]")) && Name.Split(TEXT("["), &Name, &IndexText))
				{
					IndexText.LeftChopInline(1);
					if (!IndexText.IsNumeric() || IndexText.Contains(TEXT("-")) || IndexText.Contains(TEXT(".")))
					{
						OutError = FString::Printf(TEXT("'%s': the array index must be a non-negative integer."), *Segments[Index]);
						return false;
					}
					Element = FCString::Atoi(*IndexText);
				}

				Leaf = FindFProperty<FProperty>(Struct, FName(*Name));
				if (Leaf == nullptr)
				{
					OutError = FString::Printf(TEXT("'%s' has no property '%s'."), *Struct->GetName(), *Name);
					if (bOutMissing)
					{
						*bOutMissing = true;
					}
					return false;
				}
				if (Top == nullptr)
				{
					Top = Leaf;
					if (bForWrite && !IsEditable(*Top))
					{
						OutError = FString::Printf(TEXT("Property '%s' is not editable (no EditAnywhere/EditDefaultsOnly, or EditConst)."), *Top->GetName());
						return false;
					}
				}
				else if (bForWrite && Leaf->HasAnyPropertyFlags(CPF_EditConst))
				{
					OutError = FString::Printf(TEXT("Struct member '%s' is read-only (EditConst)."), *Leaf->GetName());
					return false;
				}
				if (Element != INDEX_NONE)
				{
					const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Leaf);
					if (ArrayProp == nullptr)
					{
						OutError = FString::Printf(TEXT("'%s' is not an array (%s)."), *Name, *Leaf->GetCPPType());
						return false;
					}
					FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Container));
					if (Element >= Helper.Num())
					{
						OutError = FString::Printf(TEXT("'%s' has %d element(s); index %d is out of range (set the whole array to grow it)."), *Name, Helper.Num(), Element);
						return false;
					}
					// The element is a container whose only value (the inner property, offset 0) is itself.
					Container = Helper.GetRawPtr(Element);
					Leaf = ArrayProp->Inner;
				}
				if (Index < Segments.Num() - 1)
				{
					const FStructProperty* StructProp = CastField<FStructProperty>(Leaf);
					if (StructProp == nullptr)
					{
						OutError = CastField<FObjectPropertyBase>(Leaf) != nullptr
							? FString::Printf(TEXT("'%s' is an object reference, not a struct; address that object directly (its object path, or 'Label.ComponentName' for a component)."), *Segments[Index])
							: FString::Printf(TEXT("'%s' is not a struct; cannot address '%s' inside it."), *Segments[Index], *Segments[Index + 1]);
						return false;
					}
					Container = StructProp->ContainerPtrToValuePtr<void>(Container);
					Struct = StructProp->Struct;
				}
			}

			OutTop = Top;
			OutLeaf = Leaf;
			OutContainer = Container;
			return true;
		}
	}

	bool GetProperties(const FString& Ref, const FString& Filter, bool bIncludeAll, const FString& PropertyPath,
		TSharedRef<FJsonObject>& Out, FString& OutError, FString& OutErrorCode)
	{
		UBlueprint* Blueprint = nullptr;
		UObject* Object = Resolve(Ref, Blueprint);
		if (Object == nullptr)
		{
			bool bNotFound = false;
			OutError = ResolveError(Ref, Blueprint, bNotFound);
			OutErrorCode = bNotFound ? TEXT("object.not_found") : TEXT("object.not_compiled");
			return false;
		}

		TArray<TSharedPtr<FJsonValue>> Properties;
		if (!PropertyPath.IsEmpty())
		{
			// One exact (possibly nested / indexed) property instead of the listing.
			FProperty* Top = nullptr;
			FProperty* Leaf = nullptr;
			void* Container = nullptr;
			bool bMissing = false;
			if (!WalkPath(*Object, PropertyPath, /*bForWrite*/ false, Top, Leaf, Container, OutError, &bMissing))
			{
				// A property that does not exist is not found; a malformed path or index is a bad request.
				OutErrorCode = bMissing ? TEXT("object.not_found") : TEXT("request.bad_request");
				return false;
			}
			const TSharedRef<FJsonObject> One = PropertyToJson(*Leaf, Leaf->ContainerPtrToValuePtr<void>(Container), Object);
			One->SetStringField(TEXT("name"), PropertyPath);
			One->SetBoolField(TEXT("editable"), IsEditable(*Top) && !Leaf->HasAnyPropertyFlags(CPF_EditConst));
			Properties.Add(MakeShared<FJsonValueObject>(One));
			Out->SetStringField(TEXT("object"), Object->GetPathName());
			Out->SetStringField(TEXT("class"), Object->GetClass()->GetName());
			Out->SetArrayField(TEXT("properties"), Properties);
			return true;
		}

		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FProperty* Prop = *It;
			if ((!bIncludeAll && !Prop->HasAnyPropertyFlags(CPF_Edit)) || Prop->HasAnyPropertyFlags(CPF_Deprecated))
			{
				continue;
			}
			if (!Filter.IsEmpty() && !Prop->GetName().Contains(Filter))
			{
				continue;
			}
			Properties.Add(MakeShared<FJsonValueObject>(PropertyToJson(*Prop, Prop->ContainerPtrToValuePtr<void>(Object), Object)));
		}

		Out->SetStringField(TEXT("object"), Object->GetPathName());
		Out->SetStringField(TEXT("class"), Object->GetClass()->GetName());
		Out->SetArrayField(TEXT("properties"), Properties);
		return true;
	}

	bool SetProperty(const FString& Ref, const FString& Property, const FString& Value,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		UBlueprint* Blueprint = nullptr;
		UObject* Object = Resolve(Ref, Blueprint);
		bOutNotFound = false;
		if (Object == nullptr)
		{
			OutError = ResolveError(Ref, Blueprint, bOutNotFound);
			return false;
		}

		FProperty* Top = nullptr;
		FProperty* Leaf = nullptr;
		void* Container = nullptr;
		if (!WalkPath(*Object, Property, /*bForWrite*/ true, Top, Leaf, Container, OutError))
		{
			return false;
		}

		// Parse into a scratch value first: a rejected value must not touch the object.
		void* Scratch = FMemory::Malloc(Leaf->GetSize(), Leaf->GetMinAlignment());
		Leaf->InitializeValue(Scratch);
		if (!ParseValue(*Leaf, Value, Scratch, Object))
		{
			Leaf->DestroyValue(Scratch);
			FMemory::Free(Scratch);
			OutError = FString::Printf(TEXT("Could not parse '%s' as %s for '%s'."), *Value, *Leaf->GetCPPType(), *Property);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("SetProperty", "UE CLI: Set Property"));
		Object->Modify();
		Object->PreEditChange(Top);
		void* ValuePtr = Leaf->ContainerPtrToValuePtr<void>(Container);
		Leaf->CopyCompleteValue(ValuePtr, Scratch);
		Leaf->DestroyValue(Scratch);
		FMemory::Free(Scratch);

		FPropertyChangedEvent Event(Leaf, EPropertyChangeType::ValueSet);
		Event.SetActiveMemberProperty(Top);
		Object->PostEditChangeProperty(Event);
		Object->MarkPackageDirty();
		if (Blueprint != nullptr)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}

		Out = PropertyToJson(*Leaf, ValuePtr, Object);
		Out->SetStringField(TEXT("name"), Property);
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
