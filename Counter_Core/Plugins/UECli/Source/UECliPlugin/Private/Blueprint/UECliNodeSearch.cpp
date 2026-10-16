// Copyright UE CLI. All rights reserved.

#include "Blueprint/UECliNodeSearch.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace UECli::NodeSearch
{
	namespace
	{
		FString PropertyTypeName(const FProperty* Property)
		{
			if (!Property)
			{
				return TEXT("?");
			}
			FString Extended;
			const FString Base = Property->GetCPPType(&Extended);
			return Base + Extended;
		}

		bool NameMatches(const FString& Query, const FString& Name)
		{
			return Query.IsEmpty() || Name.Contains(Query);
		}

		bool IsCallable(const UFunction* Function)
		{
			return Function
				&& Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure)
				&& !Function->HasAnyFunctionFlags(FUNC_EditorOnly);
		}

		bool IsBlueprintEvent(const UFunction* Function)
		{
			return Function
				&& Function->HasAnyFunctionFlags(FUNC_BlueprintEvent)
				&& !Function->HasAnyFunctionFlags(FUNC_Delegate | FUNC_EditorOnly);
		}

		void AddParams(const UFunction* Function, const TSharedRef<FJsonObject>& Json)
		{
			TArray<TSharedPtr<FJsonValue>> Params;
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				const FProperty* Param = *It;
				const TSharedRef<FJsonObject> ParamJson = MakeShared<FJsonObject>();
				ParamJson->SetStringField(TEXT("name"), Param->GetName());
				ParamJson->SetStringField(TEXT("type"), PropertyTypeName(Param));
				if (Param->HasAnyPropertyFlags(CPF_OutParm) && !Param->HasAnyPropertyFlags(CPF_ReferenceParm))
				{
					ParamJson->SetBoolField(TEXT("out"), true);
				}
				if (Param->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					ParamJson->SetBoolField(TEXT("return"), true);
				}
				Params.Add(MakeShared<FJsonValueObject>(ParamJson));
			}
			Json->SetArrayField(TEXT("params"), Params);
		}

		TSharedRef<FJsonObject> FunctionToJson(const UFunction* Function, const TCHAR* Kind)
		{
			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Function->GetName());
			Json->SetStringField(TEXT("kind"), Kind);
			if (const UClass* Owner = Function->GetOwnerClass())
			{
				Json->SetStringField(TEXT("class"), Owner->GetName());
			}
			if (Function->HasAnyFunctionFlags(FUNC_BlueprintPure))
			{
				Json->SetBoolField(TEXT("pure"), true);
			}
			if (Function->HasAnyFunctionFlags(FUNC_Static))
			{
				Json->SetBoolField(TEXT("static"), true);
			}

			const FString Tooltip = Function->GetToolTipText().ToString();
			if (!Tooltip.IsEmpty() && Tooltip != Function->GetName())
			{
				Json->SetStringField(TEXT("tooltip"), Tooltip.Left(240));
			}

			AddParams(Function, Json);
			return Json;
		}

		using FFunctionPredicate = bool (*)(const UFunction*);

		/** Iterate functions on one class (and supers) or, with no filter, every native class. */
		void CollectFunctions(
			const FString& Query, const FString& ClassFilter, FFunctionPredicate Predicate,
			const TCHAR* Kind, int32 Limit, TArray<TSharedPtr<FJsonValue>>& Out)
		{
			auto Consider = [&](const UFunction* Function)
			{
				if (Out.Num() < Limit && Predicate(Function) && NameMatches(Query, Function->GetName()))
				{
					Out.Add(MakeShared<FJsonValueObject>(FunctionToJson(Function, Kind)));
				}
			};

			if (!ClassFilter.IsEmpty())
			{
				if (const UClass* Class = FindFirstObject<UClass>(*ClassFilter, EFindFirstObjectOptions::NativeFirst))
				{
					for (TFieldIterator<UFunction> It(Class, EFieldIterationFlags::IncludeSuper); It && Out.Num() < Limit; ++It)
					{
						Consider(*It);
					}
				}
				return;
			}

			for (TObjectIterator<UClass> ClassIt; ClassIt && Out.Num() < Limit; ++ClassIt)
			{
				if (!ClassIt->HasAnyClassFlags(CLASS_Native))
				{
					continue;
				}
				for (TFieldIterator<UFunction> It(*ClassIt, EFieldIterationFlags::None); It && Out.Num() < Limit; ++It)
				{
					Consider(*It);
				}
			}
		}

		void CollectVariables(
			const FString& Query, const FString& ClassFilter, int32 Limit, TArray<TSharedPtr<FJsonValue>>& Out)
		{
			// A class is required — every property in the engine is far too broad.
			const UClass* Class = ClassFilter.IsEmpty() ? nullptr : FindFirstObject<UClass>(*ClassFilter, EFindFirstObjectOptions::NativeFirst);
			if (!Class)
			{
				return;
			}

			for (TFieldIterator<FProperty> It(Class, EFieldIterationFlags::IncludeSuper); It && Out.Num() < Limit; ++It)
			{
				const FProperty* Property = *It;
				if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible) || !NameMatches(Query, Property->GetName()))
				{
					continue;
				}

				const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
				Json->SetStringField(TEXT("name"), Property->GetName());
				Json->SetStringField(TEXT("kind"), TEXT("variable"));
				Json->SetStringField(TEXT("type"), PropertyTypeName(Property));
				if (const UStruct* Owner = Property->GetOwnerStruct())
				{
					Json->SetStringField(TEXT("class"), Owner->GetName());
				}
				if (Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
				{
					Json->SetBoolField(TEXT("pure"), true);   // get-only
				}
				Json->SetArrayField(TEXT("params"), TArray<TSharedPtr<FJsonValue>>());
				Out.Add(MakeShared<FJsonValueObject>(Json));
			}
		}

		void CollectMacrosFrom(
			const UBlueprint* Library, const FString& ClassRef, const FString& Query,
			int32 Limit, TArray<TSharedPtr<FJsonValue>>& Out)
		{
			if (!Library)
			{
				return;
			}
			for (const UEdGraph* MacroGraph : Library->MacroGraphs)
			{
				if (Out.Num() >= Limit || !MacroGraph || !NameMatches(Query, MacroGraph->GetName()))
				{
					continue;
				}
				const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
				Json->SetStringField(TEXT("name"), MacroGraph->GetName());
				Json->SetStringField(TEXT("kind"), TEXT("macro"));
				Json->SetStringField(TEXT("class"), ClassRef);   // pass as --parent to add-node Macro
				Json->SetArrayField(TEXT("params"), TArray<TSharedPtr<FJsonValue>>());
				Out.Add(MakeShared<FJsonValueObject>(Json));
			}
		}

		void CollectMacros(const FString& Query, int32 Limit, TArray<TSharedPtr<FJsonValue>>& Out)
		{
			// The engine StandardMacros (ForEachLoop / DoOnce / Gate / IsValid / …).
			// class "" round-trips: `add-node Macro --member X` with no --parent.
			if (const UBlueprint* Standard = LoadObject<UBlueprint>(
				nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros")))
			{
				CollectMacrosFrom(Standard, FString(), Query, Limit, Out);
			}

			// Any project macro library that is already loaded.
			for (TObjectIterator<UBlueprint> It; It && Out.Num() < Limit; ++It)
			{
				if (It->BlueprintType != BPTYPE_MacroLibrary || It->GetName() == TEXT("StandardMacros"))
				{
					continue;
				}
				CollectMacrosFrom(*It, It->GetPathName(), Query, Limit, Out);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> Search(
		const FString& Query, const FString& ClassFilter, const FString& Kind, int32 Limit)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		if (Limit <= 0)
		{
			Limit = 50;
		}

		const bool bAll = Kind.IsEmpty() || Kind.Equals(TEXT("all"), ESearchCase::IgnoreCase);
		const bool bFunctions = bAll || Kind.Equals(TEXT("function"), ESearchCase::IgnoreCase);
		// Without a class filter, only run events when explicitly asked — every
		// BlueprintEvent in the engine is noise next to the callable functions.
		const bool bEvents = (bAll && !ClassFilter.IsEmpty()) || Kind.Equals(TEXT("event"), ESearchCase::IgnoreCase);
		const bool bMacros = bAll || Kind.Equals(TEXT("macro"), ESearchCase::IgnoreCase);
		const bool bVariables = bAll || Kind.Equals(TEXT("variable"), ESearchCase::IgnoreCase);

		// Collect each kind up to Limit on its own, then take from them in turn: many
		// matching functions cannot crowd macros and variables out of an "all" search,
		// and a kind with few matches leaves its share to the others.
		TArray<TArray<TSharedPtr<FJsonValue>>> Parts;
		if (bFunctions)
		{
			CollectFunctions(Query, ClassFilter, &IsCallable, TEXT("function"), Limit, Parts.AddDefaulted_GetRef());
		}
		if (bEvents)
		{
			CollectFunctions(Query, ClassFilter, &IsBlueprintEvent, TEXT("event"), Limit, Parts.AddDefaulted_GetRef());
		}
		if (bMacros)
		{
			CollectMacros(Query, Limit, Parts.AddDefaulted_GetRef());
		}
		if (bVariables)
		{
			CollectVariables(Query, ClassFilter, Limit, Parts.AddDefaulted_GetRef());
		}
		for (int32 Index = 0; Result.Num() < Limit; ++Index)
		{
			bool bAny = false;
			for (const TArray<TSharedPtr<FJsonValue>>& Part : Parts)
			{
				if (Index < Part.Num() && Result.Num() < Limit)
				{
					Result.Add(Part[Index]);
					bAny = true;
				}
			}
			if (!bAny)
			{
				break;
			}
		}

		Result.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
		{
			const TSharedPtr<FJsonObject> Oa = A->AsObject();
			const TSharedPtr<FJsonObject> Ob = B->AsObject();
			const FString Ka = Oa->GetStringField(TEXT("kind"));
			const FString Kb = Ob->GetStringField(TEXT("kind"));
			return Ka == Kb
				? Oa->GetStringField(TEXT("name")) < Ob->GetStringField(TEXT("name"))
				: Ka < Kb;
		});
		return Result;
	}
}
