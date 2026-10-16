// Copyright UE CLI. All rights reserved.

#include "Handlers/UECliRuntimeHandlers.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "Http/UECliHttpTypes.h"
#include "Misc/Paths.h"
#include "PlayInEditorDataTypes.h"
#include "Runtime/UECliLogCapture.h"
#include "UECliLog.h"
#include "UnrealClient.h"

namespace UECli::RuntimeHandlers
{
	using namespace UECli::Http;

	namespace
	{
		TUniquePtr<FUECliLogCapture> GLogCapture;

		ELogVerbosity::Type ParseSeverity(const FString& Text)
		{
			if (Text.Equals(TEXT("error"), ESearchCase::IgnoreCase))   return ELogVerbosity::Error;
			if (Text.Equals(TEXT("warning"), ESearchCase::IgnoreCase)) return ELogVerbosity::Warning;
			if (Text.Equals(TEXT("display"), ESearchCase::IgnoreCase)) return ELogVerbosity::Display;
			if (Text.Equals(TEXT("verbose"), ESearchCase::IgnoreCase)) return ELogVerbosity::Verbose;
			return ELogVerbosity::Log; // default: everything down to Log
		}

		void WritePlayStatus(const TSharedRef<FJsonObject>& Body)
		{
			const bool bPlaying = GEditor && GEditor->IsPlaySessionInProgress();
			Body->SetBoolField(TEXT("playing"), bPlaying);
			Body->SetBoolField(TEXT("requested"), GEditor && GEditor->IsPlaySessionRequestQueued());
			if (GEditor)
			{
				Body->SetBoolField(TEXT("simulating"), GEditor->IsSimulatingInEditor());
			}
		}
	}

	void Init()
	{
		if (!GLogCapture.IsValid())
		{
			GLogCapture = MakeUnique<FUECliLogCapture>();
		}
	}

	void Shutdown()
	{
		GLogCapture.Reset();
	}

	bool Logs(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		if (!GLogCapture.IsValid())
		{
			return SendError(OnComplete, 503, TEXT("screenshot.no_capture"), TEXT("Log capture is not running."));
		}

		const uint64 Since = static_cast<uint64>(FCString::Atoi64(*QueryParam(Request, TEXT("since"), TEXT("0"))));
		const int32 Limit = FCString::Atoi(*QueryParam(Request, TEXT("limit"), TEXT("200")));
		const ELogVerbosity::Type MinVerbosity = ParseSeverity(QueryParam(Request, TEXT("severity")));
		const FString Category = QueryParam(Request, TEXT("category"));

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		GLogCapture->Read(Since, Limit, MinVerbosity, Category, Result);
		return SendJson(OnComplete, 200, Result);
	}

	bool Play(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		if (!GEditor)
		{
			return SendError(OnComplete, 503, TEXT("editor.unavailable"), TEXT("No editor available."));
		}
		if (GEditor->IsPlaySessionInProgress())
		{
			return SendError(OnComplete, 409, TEXT("pie.already_playing"), TEXT("A play session is already in progress."));
		}

		FRequestPlaySessionParams Params;
		Params.WorldType = EPlaySessionWorldType::PlayInEditor;
		GEditor->RequestPlaySession(Params);

		// The session starts on the next editor tick. Poll GET /editor/play for status.
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetBoolField(TEXT("ok"), true);
		WritePlayStatus(Body);
		return SendJson(OnComplete, 202, Body);
	}

	bool StopPlay(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		if (!GEditor)
		{
			return SendError(OnComplete, 503, TEXT("editor.unavailable"), TEXT("No editor available."));
		}
		if (!GEditor->IsPlaySessionInProgress())
		{
			return SendError(OnComplete, 409, TEXT("pie.not_playing"), TEXT("No play session is in progress."));
		}

		GEditor->RequestEndPlayMap();
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetBoolField(TEXT("ok"), true);
		return SendJson(OnComplete, 202, Body);
	}

	bool PlayStatus(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		WritePlayStatus(Body);
		return SendJson(OnComplete, 200, Body);
	}

	bool Screenshot(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> RequestBody = ParseJsonBody(Request, Error);

		FString Name = TEXT("uecli");
		if (RequestBody.IsValid())
		{
			RequestBody->TryGetStringField(TEXT("name"), Name);
		}
		// A bare file name only: no directories, no ".." escaping Saved/Screenshots/UECli.
		Name = FPaths::MakeValidFileName(FPaths::GetCleanFilename(Name));
		if (Name.IsEmpty() || Name.StartsWith(TEXT(".")))
		{
			Name = TEXT("uecli");
		}

		const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("UECli"));
		IFileManager::Get().MakeDirectory(*Directory, /*Tree*/ true);
		const FString FilePath = Directory / FString::Printf(TEXT("%s_%s.png"), *Name, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));

		if (!GIsRHIInitialized)
		{
			return SendError(OnComplete, 409, TEXT("screenshot.no_rhi"),
				TEXT("The editor is running with -nullrhi; screenshots are not available. Launch it with rendering to use this."));
		}

		FScreenshotRequest::RequestScreenshot(FilePath, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);

		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetBoolField(TEXT("ok"), true);
		Body->SetStringField(TEXT("path"), FilePath);
		Body->SetStringField(TEXT("note"), TEXT("The file is written on the next rendered frame."));
		return SendJson(OnComplete, 202, Body);
	}
}
