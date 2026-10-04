// Copyright UE CLI. All rights reserved.

#include "Handlers/UECliProjectHandlers.h"

#include "Dom/JsonObject.h"
#include "Http/UECliHttpTypes.h"
#include "Project/UECliProjectOps.h"

namespace UECli::ProjectHandlers
{
	using namespace UECli::Http;

	bool ProjectInfo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		return SendJson(OnComplete, 200, ProjectOps::ProjectInfo());
	}

	bool SearchAssets(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const int32 Limit = FMath::Clamp(FCString::Atoi(*QueryParam(Request, TEXT("limit"), TEXT("100"))), 1, 500);
		return SendJsonArray(OnComplete, 200, ProjectOps::SearchAssets(
			QueryParam(Request, TEXT("query")),
			QueryParam(Request, TEXT("type")),
			QueryParam(Request, TEXT("path")),
			Limit));
	}

	bool SaveAsset(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const FString Path = QueryParam(Request, TEXT("path"));
		if (Path.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'path' is required."));
		}

		FString Error;
		if (!ProjectOps::SaveAsset(Path, Error))
		{
			return SendError(OnComplete, 422, TEXT("asset.save_failed"), Error);
		}

		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetBoolField(TEXT("ok"), true);
		return SendJson(OnComplete, 200, Body);
	}
}
