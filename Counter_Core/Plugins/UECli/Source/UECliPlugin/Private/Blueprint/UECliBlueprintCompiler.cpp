// Copyright UE CLI. All rights reserved.

#include "Blueprint/UECliBlueprintCompiler.h"
#include "UECliServices.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Events/UECliEventHub.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/PackageName.h"
#include "UECliLog.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UECli::BlueprintCompiler
{
	namespace
	{
		const TCHAR* StatusName(EBlueprintStatus Status)
		{
			switch (Status)
			{
			case BS_Dirty:                  return TEXT("Dirty");
			case BS_Error:                  return TEXT("Error");
			case BS_UpToDate:               return TEXT("UpToDate");
			case BS_UpToDateWithWarnings:   return TEXT("UpToDateWithWarnings");
			case BS_Unknown:
			default:                        return TEXT("Unknown");
			}
		}

		const TCHAR* SeverityName(EMessageSeverity::Type Severity)
		{
			switch (Severity)
			{
			case EMessageSeverity::Error:               return TEXT("error");
			case EMessageSeverity::PerformanceWarning:  return TEXT("performance");
			case EMessageSeverity::Warning:             return TEXT("warning");
			case EMessageSeverity::Info:
			default:                                    return TEXT("info");
			}
		}
	}

	bool Compile(UBlueprint& Blueprint, TSharedRef<FJsonObject>& OutResult, FString& OutError)
	{
		const FString BlueprintPath = Blueprint.GetOutermost()->GetName();
		{
			const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("blueprint"), BlueprintPath);
			UECli::Services::Events().Broadcast(TEXT("blueprint.compile.started"), Payload);
		}

		FCompilerResultsLog ResultsLog;
		ResultsLog.SetSourcePath(Blueprint.GetPathName());
		ResultsLog.BeginEvent(TEXT("UECli Compile"));

		FKismetEditorUtilities::CompileBlueprint(&Blueprint, EBlueprintCompileOptions::None, &ResultsLog);

		ResultsLog.EndEvent();

		TArray<TSharedPtr<FJsonValue>> Messages;
		for (const TSharedRef<FTokenizedMessage>& Message : ResultsLog.Messages)
		{
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("severity"), SeverityName(Message->GetSeverity()));
			Entry->SetStringField(TEXT("text"), Message->ToText().ToString());
			Messages.Add(MakeShared<FJsonValueObject>(Entry));
		}

		OutResult->SetStringField(TEXT("status"), StatusName(Blueprint.Status));
		OutResult->SetBoolField(TEXT("succeeded"), ResultsLog.NumErrors == 0);
		OutResult->SetNumberField(TEXT("errors"), ResultsLog.NumErrors);
		OutResult->SetNumberField(TEXT("warnings"), ResultsLog.NumWarnings);
		OutResult->SetArrayField(TEXT("messages"), Messages);

		const TSharedRef<FJsonObject> Finished = MakeShared<FJsonObject>();
		Finished->SetStringField(TEXT("blueprint"), BlueprintPath);
		Finished->SetStringField(TEXT("status"), StatusName(Blueprint.Status));
		Finished->SetBoolField(TEXT("succeeded"), ResultsLog.NumErrors == 0);
		Finished->SetNumberField(TEXT("errors"), ResultsLog.NumErrors);
		Finished->SetNumberField(TEXT("warnings"), ResultsLog.NumWarnings);
		UECli::Services::Events().Broadcast(TEXT("blueprint.compile.finished"), Finished);
		return true;
	}

	bool Save(UBlueprint& Blueprint, FString& OutError)
	{
		UPackage* Package = Blueprint.GetOutermost();
		if (!Package)
		{
			OutError = TEXT("Blueprint has no package.");
			return false;
		}

		Package->MarkPackageDirty();

		FString Filename;
		// A level script Blueprint lives in its map package: keep the .umap extension.
		const FString& Extension = Package->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
		if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename, Extension))
		{
			OutError = FString::Printf(TEXT("Could not resolve a file path for package '%s'."), *Package->GetName());
			return false;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		if (!UPackage::SavePackage(Package, nullptr, *Filename, SaveArgs))
		{
			OutError = FString::Printf(TEXT("SavePackage failed for '%s'."), *Package->GetName());
			return false;
		}

		UE_LOG(LogUECli, Display, TEXT("Saved %s"), *Package->GetName());

		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("blueprint"), Package->GetName());
		UECli::Services::Events().Broadcast(TEXT("blueprint.saved"), Payload);
		return true;
	}
}
