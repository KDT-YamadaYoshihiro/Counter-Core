// Copyright UE CLI. All rights reserved.

#include "Project/UECliProjectOps.h"

#include "AssetImportTask.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformMisc.h"
#include "IAssetTools.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UECliLog.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UECli::ProjectOps
{
	namespace
	{
		const IAssetRegistry& AssetRegistry()
		{
			return FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		}
	}

	TSharedRef<FJsonObject> ProjectInfo()
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), FApp::GetProjectName());
		if (FPaths::IsProjectFilePathSet())
		{
			Json->SetStringField(TEXT("projectFile"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
		}
		Json->SetStringField(TEXT("projectDir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
		Json->SetStringField(TEXT("contentDir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
		Json->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());

		TArray<TSharedPtr<FJsonValue>> Plugins;
		for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
		{
			if (Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project)
			{
				const TSharedRef<FJsonObject> PluginJson = MakeShared<FJsonObject>();
				PluginJson->SetStringField(TEXT("name"), Plugin->GetName());
				PluginJson->SetStringField(TEXT("version"), Plugin->GetDescriptor().VersionName);
				Plugins.Add(MakeShared<FJsonValueObject>(PluginJson));
			}
		}
		Json->SetArrayField(TEXT("plugins"), Plugins);
		return Json;
	}

	TArray<TSharedPtr<FJsonValue>> SearchAssets(const FString& Query, const FString& ClassFilter, const FString& PathPrefix, int32 Limit)
	{
		TArray<TSharedPtr<FJsonValue>> Result;

		FARFilter Filter;
		Filter.bRecursiveClasses = true;
		if (!ClassFilter.IsEmpty())
		{
			const UClass* Class = FindFirstObject<UClass>(*ClassFilter, EFindFirstObjectOptions::NativeFirst);
			if (!Class)
			{
				return Result; // unknown class: nothing matches (do not silently drop the filter)
			}
			Filter.ClassPaths.Add(Class->GetClassPathName());
		}
		Filter.PackagePaths.Add(FName(*(PathPrefix.IsEmpty() ? TEXT("/Game") : PathPrefix)));
		Filter.bRecursivePaths = true;

		TArray<FAssetData> Assets;
		AssetRegistry().GetAssets(Filter, Assets);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });

		for (const FAssetData& Asset : Assets)
		{
			if (Result.Num() >= Limit)
			{
				break;
			}
			if (!Query.IsEmpty() && !Asset.AssetName.ToString().Contains(Query))
			{
				continue;
			}

			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("path"), Asset.PackageName.ToString());
			Json->SetStringField(TEXT("name"), Asset.AssetName.ToString());
			Json->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
			Result.Add(MakeShared<FJsonValueObject>(Json));
		}
		return Result;
	}

	bool SaveAsset(const FString& Path, FString& OutError)
	{
		FString PackageName = Path;
		int32 DotIndex;
		if (PackageName.FindChar(TEXT('.'), DotIndex))
		{
			PackageName = PackageName.Left(DotIndex);
		}

		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (!Package)
		{
			Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		}
		if (!Package)
		{
			OutError = FString::Printf(TEXT("No package '%s'."), *PackageName);
			return false;
		}

		Package->MarkPackageDirty();

		FString Filename;
		const FString& Extension = Package->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
		if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename, Extension))
		{
			OutError = FString::Printf(TEXT("Could not resolve a file path for '%s'."), *PackageName);
			return false;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, nullptr, *Filename, SaveArgs))
		{
			OutError = FString::Printf(TEXT("SavePackage failed for '%s'."), *PackageName);
			return false;
		}

		UE_LOG(LogUECli, Display, TEXT("Saved %s"), *PackageName);
		return true;
	}

	bool ResolveImportSource(const FString& SourceFile, FString& OutFullPath, FString& OutError)
	{
		// Only files under the project, or under a directory listed in
		// UECLI_IMPORT_DIRS (';'-separated), may be imported.
		FString FullSource = FPaths::ConvertRelativePathToFull(SourceFile);
		FPaths::NormalizeFilename(FullSource);
		FPaths::CollapseRelativeDirectories(FullSource);
		TArray<FString> Allowed;
		FPlatformMisc::GetEnvironmentVariable(TEXT("UECLI_IMPORT_DIRS")).ParseIntoArray(Allowed, TEXT(";"), /*CullEmpty*/ true);
		Allowed.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
		const bool bAllowed = Allowed.ContainsByPredicate([&FullSource](FString Dir)
		{
			Dir = FPaths::ConvertRelativePathToFull(Dir);
			FPaths::NormalizeDirectoryName(Dir);
			return FPaths::IsUnderDirectory(FullSource, Dir);
		});
		if (!bAllowed)
		{
			OutError = FString::Printf(TEXT("'%s' is outside the project and UECLI_IMPORT_DIRS; refusing to import."), *SourceFile);
			return false;
		}

		if (!FPaths::FileExists(FullSource))
		{
			OutError = FString::Printf(TEXT("Source file not found: '%s'."), *SourceFile);
			return false;
		}

		OutFullPath = FullSource;
		return true;
	}

	bool ImportAsset(const FString& SourceFile, const FString& DestinationPath, bool bReplaceExisting,
		TSharedRef<FJsonObject>& OutResult, FString& OutError)
	{
		FString FullSource;
		if (!ResolveImportSource(SourceFile, FullSource, OutError))
		{
			return false;
		}

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = FullSource;
		Task->DestinationPath = DestinationPath.IsEmpty() ? TEXT("/Game") : DestinationPath;
		Task->bAutomated = true;
		Task->bSave = true;
		Task->bReplaceExisting = bReplaceExisting;

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		AssetTools.ImportAssetTasks({ Task });

		TArray<TSharedPtr<FJsonValue>> Imported;
		for (const FString& ObjectPath : Task->ImportedObjectPaths)
		{
			Imported.Add(MakeShared<FJsonValueString>(ObjectPath));
		}

		if (Imported.Num() == 0)
		{
			OutError = FString::Printf(TEXT("Nothing was imported from '%s' (unsupported type, or the importer needs options)."), *SourceFile);
			return false;
		}

		OutResult->SetArrayField(TEXT("imported"), Imported);
		return true;
	}
}
