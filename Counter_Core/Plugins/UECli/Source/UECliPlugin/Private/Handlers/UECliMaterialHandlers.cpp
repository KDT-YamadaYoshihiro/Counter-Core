// Copyright UE CLI. All rights reserved.

#include "Handlers/UECliMaterialHandlers.h"

#include "Algo/StableSort.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Http/UECliHttpTypes.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialFunction.h"
#include "UObject/UObjectGlobals.h"

namespace UECli::MaterialHandlers
{
	using namespace UECli::Http;

	namespace
	{
		/** Wire source of an input: { from: "E<k>", output: <name or index> }, or null when unconnected. */
		TSharedPtr<FJsonObject> LinkJson(const FExpressionInput* Input, const TMap<const UMaterialExpression*, FString>& Ids)
		{
			if (!Input || !Input->Expression)
			{
				return nullptr;
			}
			const FString* Id = Ids.Find(Input->Expression);
			if (!Id)
			{
				return nullptr;
			}
			const TSharedRef<FJsonObject> Link = MakeShared<FJsonObject>();
			Link->SetStringField(TEXT("from"), *Id);
			Link->SetNumberField(TEXT("outputIndex"), Input->OutputIndex);
			TArray<FExpressionOutput>& Outputs = Input->Expression->GetOutputs();
			if (Outputs.IsValidIndex(Input->OutputIndex) && !Outputs[Input->OutputIndex].OutputName.IsNone())
			{
				Link->SetStringField(TEXT("output"), Outputs[Input->OutputIndex].OutputName.ToString());
			}
			return Link;
		}
	}

	bool GetGraph(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const FString Path = QueryParam(Request, TEXT("path"));
		if (Path.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'path' is required."));
		}

		FString ObjectPath = Path;
		if (!ObjectPath.Contains(TEXT(".")))
		{
			ObjectPath += TEXT(".") + FPackageName::GetShortName(Path);
		}
		UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		UMaterial* Material = Cast<UMaterial>(Asset);
		UMaterialFunction* Function = Cast<UMaterialFunction>(Asset);
		if (!Material && !Function)
		{
			return SendError(OnComplete, 404, TEXT("material.not_found"),
				FString::Printf(TEXT("No material or material function at '%s' (instances have no graph; read their parameters with ue_get_properties)."), *Path));
		}

		const TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Material ? Material->GetExpressions() : Function->GetExpressions();
		TMap<const UMaterialExpression*, FString> Ids;
		for (int32 Index = 0; Index < Expressions.Num(); ++Index)
		{
			Ids.Add(Expressions[Index], FString::Printf(TEXT("E%d"), Index + 1));
		}

		TArray<TSharedPtr<FJsonValue>> ExpressionsJson;
		for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
		{
			if (!Expr)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("id"), Ids[Expr]);
			FString ClassName = Expr->GetClass()->GetName();
			ClassName.RemoveFromStart(TEXT("MaterialExpression"));
			Json->SetStringField(TEXT("class"), ClassName);
			Json->SetStringField(TEXT("name"), Expr->GetName());

			TArray<FString> Captions;
			Expr->GetCaption(Captions);
			Json->SetStringField(TEXT("caption"), FString::Join(Captions, TEXT(" ")));
			if (!Expr->Desc.IsEmpty())
			{
				Json->SetStringField(TEXT("desc"), Expr->Desc);
			}
			Json->SetArrayField(TEXT("position"), {
				MakeShared<FJsonValueNumber>(Expr->MaterialExpressionEditorX),
				MakeShared<FJsonValueNumber>(Expr->MaterialExpressionEditorY) });

			TArray<TSharedPtr<FJsonValue>> Inputs;
			for (int32 Index = 0; FExpressionInput* Input = Expr->GetInput(Index); ++Index)
			{
				const TSharedRef<FJsonObject> In = MakeShared<FJsonObject>();
				In->SetStringField(TEXT("name"), Expr->GetInputName(Index).ToString());
				if (const TSharedPtr<FJsonObject> Link = LinkJson(Input, Ids))
				{
					In->SetObjectField(TEXT("link"), Link);
				}
				Inputs.Add(MakeShared<FJsonValueObject>(In));
			}
			Json->SetArrayField(TEXT("inputs"), Inputs);

			TArray<TSharedPtr<FJsonValue>> Outputs;
			for (const FExpressionOutput& Output : Expr->GetOutputs())
			{
				Outputs.Add(MakeShared<FJsonValueString>(Output.OutputName.IsNone() ? FString() : Output.OutputName.ToString()));
			}
			Json->SetArrayField(TEXT("outputs"), Outputs);
			ExpressionsJson.Add(MakeShared<FJsonValueObject>(Json));
		}

		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("path"), Path);
		Body->SetArrayField(TEXT("expressions"), ExpressionsJson);

		if (Function)
		{
			// A function's pins are its FunctionInput / FunctionOutput expressions, in pin order.
			TArray<TPair<int32, TSharedPtr<FJsonValue>>> Ins, Outs;
			for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
			{
				if (const UMaterialExpressionFunctionInput* In = Cast<UMaterialExpressionFunctionInput>(Expr))
				{
					const TSharedRef<FJsonObject> Pin = MakeShared<FJsonObject>();
					Pin->SetStringField(TEXT("id"), Ids[Expr]);
					Pin->SetStringField(TEXT("name"), In->InputName.ToString());
					FString Type = StaticEnum<EFunctionInputType>()->GetNameStringByValue(In->InputType);
					Type.RemoveFromStart(TEXT("FunctionInput_"));
					Pin->SetStringField(TEXT("type"), Type);
					Ins.Emplace(In->SortPriority, MakeShared<FJsonValueObject>(Pin));
				}
				else if (const UMaterialExpressionFunctionOutput* Out = Cast<UMaterialExpressionFunctionOutput>(Expr))
				{
					const TSharedRef<FJsonObject> Pin = MakeShared<FJsonObject>();
					Pin->SetStringField(TEXT("id"), Ids[Expr]);
					Pin->SetStringField(TEXT("name"), Out->OutputName.ToString());
					Outs.Emplace(Out->SortPriority, MakeShared<FJsonValueObject>(Pin));
				}
			}
			auto Sorted = [](TArray<TPair<int32, TSharedPtr<FJsonValue>>>& Pins)
			{
				Algo::StableSortBy(Pins, [](const TPair<int32, TSharedPtr<FJsonValue>>& P) { return P.Key; });
				TArray<TSharedPtr<FJsonValue>> Values;
				for (const TPair<int32, TSharedPtr<FJsonValue>>& P : Pins)
				{
					Values.Add(P.Value);
				}
				return Values;
			};
			Body->SetStringField(TEXT("kind"), TEXT("function"));
			Body->SetArrayField(TEXT("functionInputs"), Sorted(Ins));
			Body->SetArrayField(TEXT("functionOutputs"), Sorted(Outs));
			Body->SetArrayField(TEXT("properties"), {});
			return SendJson(OnComplete, 200, Body);
		}

		// Material attribute pins (BaseColor, Roughness, ...) that are wired.
		TArray<TSharedPtr<FJsonValue>> PropertiesJson;
		const UEnum* PropertyEnum = StaticEnum<EMaterialProperty>();
		for (int32 Property = 0; Property < MP_MAX; ++Property)
		{
			const FExpressionInput* Input = Material->GetExpressionInputForProperty(static_cast<EMaterialProperty>(Property));
			if (const TSharedPtr<FJsonObject> Link = LinkJson(Input, Ids))
			{
				FString Name = PropertyEnum ? PropertyEnum->GetNameStringByValue(Property) : FString::FromInt(Property);
				Name.RemoveFromStart(TEXT("MP_"));
				Link->SetStringField(TEXT("property"), Name);
				PropertiesJson.Add(MakeShared<FJsonValueObject>(Link.ToSharedRef()));
			}
		}

		Body->SetStringField(TEXT("kind"), TEXT("material"));
		Body->SetStringField(TEXT("domain"), StaticEnum<EMaterialDomain>()->GetNameStringByValue(Material->MaterialDomain));
		Body->SetStringField(TEXT("blendMode"), StaticEnum<EBlendMode>()->GetNameStringByValue(Material->BlendMode));
		Body->SetArrayField(TEXT("properties"), PropertiesJson);
		return SendJson(OnComplete, 200, Body);
	}
}
