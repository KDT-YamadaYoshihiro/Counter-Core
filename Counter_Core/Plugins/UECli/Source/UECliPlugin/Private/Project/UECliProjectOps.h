// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

/** Project-level queries and asset operations. */
namespace UECli::ProjectOps
{
	/** { name, projectFile, projectDir, contentDir, engineVersion, plugins:[...] }. */
	TSharedRef<FJsonObject> ProjectInfo();

	/**
	 * Assets matching a name substring, optionally filtered by class name and
	 * package path prefix. Each: { path, name, class }.
	 */
	TArray<TSharedPtr<FJsonValue>> SearchAssets(const FString& Query, const FString& ClassFilter, const FString& PathPrefix, int32 Limit);

	/** Save one asset's package to disk. */
	bool SaveAsset(const FString& Path, FString& OutError);

	/** Absolute path of an importable source file: it must exist and lie under the project or UECLI_IMPORT_DIRS. */
	bool ResolveImportSource(const FString& SourceFile, FString& OutFullPath, FString& OutError);

	/**
	 * Import a source file into a content path. OutResult = { imported:[paths] }.
	 */
	bool ImportAsset(const FString& SourceFile, const FString& DestinationPath, bool bReplaceExisting,
		TSharedRef<FJsonObject>& OutResult, FString& OutError);
}
