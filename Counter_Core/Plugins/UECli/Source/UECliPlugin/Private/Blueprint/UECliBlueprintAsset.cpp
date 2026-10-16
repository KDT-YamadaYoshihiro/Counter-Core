// Copyright UE CLI. All rights reserved.

#include "Blueprint/UECliBlueprintAsset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "UECliLog.h"
#include "UObject/Interface.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace UECli::BlueprintAsset
{
	namespace
	{
		UClass* ResolveParentClass(const FString& Ref, FString& OutError)
		{
			if (Ref.IsEmpty())
			{
				return AActor::StaticClass();
			}

			if (Ref.StartsWith(TEXT("/")))
			{
				FString ObjectPath = Ref;
				if (!ObjectPath.Contains(TEXT(".")))
				{
					FString AssetName;
					Ref.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
					ObjectPath = Ref + TEXT(".") + AssetName;
				}
				if (UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath))
				{
					return Blueprint->GeneratedClass;
				}
				if (UClass* Loaded = LoadObject<UClass>(nullptr, *(ObjectPath + TEXT("_C"))))
				{
					return Loaded;
				}
			}
			else if (UClass* Found = FindFirstObject<UClass>(*Ref, EFindFirstObjectOptions::NativeFirst))
			{
				return Found;
			}

			OutError = FString::Printf(TEXT("Could not resolve parent class '%s'."), *Ref);
			return nullptr;
		}
	}

	bool CreateBlueprint(const FString& PackagePath, const FString& ParentClassRef, bool bInterface,
		TSharedRef<FJsonObject>& OutResult, FString& OutError)
	{
		FString PackageName = PackagePath;
		int32 DotIndex;
		if (PackageName.FindChar(TEXT('.'), DotIndex))
		{
			PackageName = PackageName.Left(DotIndex);
		}

		if (!FPackageName::IsValidLongPackageName(PackageName))
		{
			OutError = FString::Printf(TEXT("'%s' is not a valid package path (expected e.g. /Game/Enemies/BP_Grunt)."), *PackageName);
			return false;
		}
		if (FPackageName::DoesPackageExist(PackageName))
		{
			OutError = FString::Printf(TEXT("An asset already exists at '%s'."), *PackageName);
			return false;
		}

		UClass* ParentClass = bInterface ? UInterface::StaticClass() : ResolveParentClass(ParentClassRef, OutError);
		if (!ParentClass)
		{
			return false;
		}
		if (!bInterface && !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
		{
			OutError = FString::Printf(TEXT("Cannot create a Blueprint from '%s'."), *ParentClass->GetName());
			return false;
		}

		const FString AssetName = FPackageName::GetShortName(PackageName);
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			OutError = FString::Printf(TEXT("Could not create package '%s'."), *PackageName);
			return false;
		}

		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			ParentClass, Package, FName(*AssetName), bInterface ? BPTYPE_Interface : BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (!Blueprint)
		{
			OutError = TEXT("CreateBlueprint returned null.");
			return false;
		}

		FAssetRegistryModule::AssetCreated(Blueprint);
		Package->MarkPackageDirty();

		FString Filename;
		if (FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename, FPackageName::GetAssetPackageExtension()))
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			if (!UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs))
			{
				// Created in memory (usable, undoable) but not on disk: say so instead of reporting a clean create.
				UE_LOG(LogUECli, Warning, TEXT("Created Blueprint %s but could not save it to %s"), *PackageName, *Filename);
				OutResult->SetStringField(TEXT("warning"), FString::Printf(TEXT("Created but not saved to disk (%s); save it with ue_save_blueprint."), *Filename));
			}
		}

		UE_LOG(LogUECli, Display, TEXT("Created Blueprint %s (parent %s)"), *PackageName, *ParentClass->GetName());

		OutResult->SetStringField(TEXT("path"), PackageName);
		OutResult->SetStringField(TEXT("name"), AssetName);
		OutResult->SetStringField(TEXT("parentClass"), ParentClass->GetName());
		OutResult->SetStringField(TEXT("kind"), bInterface ? TEXT("Interface") : TEXT("Normal"));
		return true;
	}

	bool DeleteBlueprint(const FString& PackagePath, bool bForce, TArray<FString>& OutReferencers, FString& OutError)
	{
		FString PackageName = PackagePath;
		int32 DotIndex;
		if (PackageName.FindChar(TEXT('.'), DotIndex))
		{
			PackageName = PackageName.Left(DotIndex);
		}

		const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetShortName(PackageName);
		UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPath);
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("No asset at '%s'."), *PackageName);
			return false;
		}

		// Check other packages that reference this one before touching anything,
		// so a refused delete leaves the project exactly as it was.
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FName> Referencers;
		Registry.GetReferencers(FName(*PackageName), Referencers);
		for (const FName& Referencer : Referencers)
		{
			if (Referencer.ToString() != PackageName)
			{
				OutReferencers.Add(Referencer.ToString());
			}
		}
		if (OutReferencers.Num() > 0 && !bForce)
		{
			OutError = FString::Printf(TEXT("'%s' is referenced by %d package(s); delete with force=true to remove it anyway."),
				*PackageName, OutReferencers.Num());
			return false;
		}

		auto TryDelete = [&]()
		{
			return bForce
				? ObjectTools::ForceDeleteObjects({ Asset }, /*ShowConfirmation*/ false)
				: ObjectTools::DeleteObjects({ Asset }, /*bShowConfirmation*/ false);
		};
		int32 Deleted = TryDelete();
		if (Deleted == 0 && OutReferencers.Num() == 0 && GEditor && GEditor->Trans)
		{
			// No other package references it, so the holder is in memory: edits made through
			// UE CLI leave the asset in the undo buffer. The editor's own delete dialog clears
			// the buffer in that case; do the same and retry once.
			GEditor->Trans->Reset(NSLOCTEXT("UECli", "DeleteAsset", "Delete asset"));
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			Deleted = TryDelete();
		}
		OutReferencers.Reset();
		if (Deleted == 0)
		{
			OutError = FString::Printf(TEXT("The editor refused to delete '%s' (it may still be referenced)."), *PackageName);
			return false;
		}
		return true;
	}
}
