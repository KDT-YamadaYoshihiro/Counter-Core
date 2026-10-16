// Copyright UE CLI. All rights reserved.

#include "Handlers/UECliEditorHandlers.h"

#include "Dom/JsonObject.h"
#include "Containers/Ticker.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CoreDelegates.h"
#include "Http/UECliHttpTypes.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "UECliProtocol.h"

namespace UECli::EditorHandlers
{
	using namespace UECli::Http;

	bool Ping(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetBoolField(TEXT("pong"), true);
		Body->SetNumberField(TEXT("protocolVersion"), UECLI_PROTOCOL_VERSION);
		return SendJson(OnComplete, 200, Body);
	}

	bool Version(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const FEngineVersion& EngineVersion = FEngineVersion::Current();

		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("engineVersion"), EngineVersion.ToString());
		Body->SetStringField(TEXT("engineVersionShort"),
			FString::Printf(TEXT("%u.%u.%u"), EngineVersion.GetMajor(), EngineVersion.GetMinor(), EngineVersion.GetPatch()));

		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UECli"));
		Body->SetStringField(TEXT("pluginVersion"), Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : TEXT("unknown"));
		Body->SetNumberField(TEXT("protocolVersion"), UECLI_PROTOCOL_VERSION);

		const FString ProjectName = FApp::GetProjectName();
		if (!ProjectName.IsEmpty())
		{
			Body->SetStringField(TEXT("projectName"), ProjectName);
		}
		if (FPaths::IsProjectFilePathSet())
		{
			Body->SetStringField(TEXT("projectFile"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
		}

		return SendJson(OnComplete, 200, Body);
	}

	bool Quit(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		// Refuse to drop unsaved work unless forced.
		if (QueryParam(Request, TEXT("force")) != TEXT("true"))
		{
			TArray<UPackage*> Dirty;
			FEditorFileUtils::GetDirtyContentPackages(Dirty);
			FEditorFileUtils::GetDirtyWorldPackages(Dirty);
			if (Dirty.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Names;
				for (const UPackage* Package : Dirty)
				{
					Names.Add(MakeShared<FJsonValueString>(Package->GetName()));
				}
				const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
				Details->SetArrayField(TEXT("packages"), Names);
				return SendError(OnComplete, 409, TEXT("editor.unsaved_changes"),
					FString::Printf(TEXT("%d unsaved package(s); save them first or quit with force=true."), Dirty.Num()), Details);
			}
		}

		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetBoolField(TEXT("ok"), true);
		Body->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
		SendJson(OnComplete, 200, Body);

		// Exit on a later tick so the response above is flushed first.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			RequestEngineExit(TEXT("UE CLI: editor/quit"));
			return false;
		}), 0.2f);
		return true;
	}

	bool Undo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		if (!GEditor)
		{
			return SendError(OnComplete, 503, TEXT("editor.unavailable"), TEXT("No editor available for undo."));
		}

		const bool bUndone = GEditor->UndoTransaction();
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetBoolField(TEXT("ok"), bUndone);
		return SendJson(OnComplete, bUndone ? 200 : 409, Body);
	}

	bool Redo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		if (!GEditor)
		{
			return SendError(OnComplete, 503, TEXT("editor.unavailable"), TEXT("No editor available for redo."));
		}

		const bool bRedone = GEditor->RedoTransaction();
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetBoolField(TEXT("ok"), bRedone);
		return SendJson(OnComplete, bRedone ? 200 : 409, Body);
	}
}
