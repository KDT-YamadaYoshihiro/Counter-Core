// Copyright UE CLI. All rights reserved.

#include "Handlers/UECliJobHandlers.h"
#include "UECliServices.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Http/UECliHttpTypes.h"
#include "Jobs/UECliJob.h"
#include "Jobs/UECliJobManager.h"

namespace UECli::JobHandlers
{
	using namespace UECli::Http;

	namespace
	{
		bool ResolveId(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, FGuid& OutId)
		{
			const FString IdText = QueryParam(Request, TEXT("id"));
			if (IdText.IsEmpty() || !FGuid::Parse(IdText, OutId))
			{
				SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'id' must be a job guid."));
				return false;
			}
			return true;
		}
	}

	bool Start(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}

		FString Kind;
		if (!Body->TryGetStringField(TEXT("kind"), Kind) || Kind.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'kind' is required."));
		}

		TSharedPtr<FJsonObject> Params;
		const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
		if (Body->TryGetObjectField(TEXT("params"), ParamsPtr))
		{
			Params = *ParamsPtr;
		}

		TSharedPtr<FUECliJob> Job = UECli::Services::Jobs().Enqueue(Kind, Params, Error);
		if (!Job.IsValid() && UECli::Services::Jobs().KnownKinds().Contains(Kind))
		{
			// Known kind, rejected parameters.
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}
		if (!Job.IsValid())
		{
			const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Kinds;
			for (const FString& Known : UECli::Services::Jobs().KnownKinds())
			{
				Kinds.Add(MakeShared<FJsonValueString>(Known));
			}
			Details->SetArrayField(TEXT("knownKinds"), Kinds);
			return SendError(OnComplete, 400, TEXT("job.bad_kind"), Error, Details);
		}

		return SendJson(OnComplete, 202, Job->ToJson());
	}

	bool List(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const int32 Limit = FMath::Clamp(FCString::Atoi(*QueryParam(Request, TEXT("limit"), TEXT("25"))), 1, 100);
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const TSharedPtr<FUECliJob>& Job : UECli::Services::Jobs().List(Limit))
		{
			Items.Add(MakeShared<FJsonValueObject>(Job->ToJson()));
		}
		return SendJsonArray(OnComplete, 200, Items);
	}

	bool Status(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FGuid Id;
		if (!ResolveId(Request, OnComplete, Id))
		{
			return true;
		}

		const TSharedPtr<FUECliJob> Job = UECli::Services::Jobs().Find(Id);
		if (!Job.IsValid())
		{
			return SendError(OnComplete, 404, TEXT("job.not_found"), TEXT("No job with that id (it may have been trimmed)."));
		}
		return SendJson(OnComplete, 200, Job->ToJson());
	}

	bool Cancel(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FGuid Id;
		if (!ResolveId(Request, OnComplete, Id))
		{
			return true;
		}

		if (!UECli::Services::Jobs().Cancel(Id))
		{
			return SendError(OnComplete, 409, TEXT("job.not_cancellable"), TEXT("Job is unknown or already finished."));
		}

		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetBoolField(TEXT("ok"), true);
		return SendJson(OnComplete, 200, Body);
	}
}
