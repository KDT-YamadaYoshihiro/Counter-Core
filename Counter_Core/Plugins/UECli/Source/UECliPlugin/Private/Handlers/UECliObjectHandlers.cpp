// Copyright UE CLI. All rights reserved.

#include "Handlers/UECliObjectHandlers.h"
#include "UECliServices.h"

#include "Dom/JsonObject.h"
#include "Events/UECliEventHub.h"
#include "Http/UECliHttpTypes.h"
#include "Object/UECliObjectOps.h"

namespace UECli::ObjectHandlers
{
	using namespace UECli::Http;

	bool GetProperties(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const FString Path = QueryParam(Request, TEXT("path"));
		if (Path.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'path' is required."));
		}

		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		FString Error;
		FString Code;
		if (!ObjectOps::GetProperties(Path, QueryParam(Request, TEXT("filter")),
			QueryParam(Request, TEXT("all")).Equals(TEXT("true"), ESearchCase::IgnoreCase),
			QueryParam(Request, TEXT("property")), Body, Error, Code))
		{
			if (Code == TEXT("object.not_found"))
			{
				return SendError(OnComplete, 404, TEXT("object.not_found"), Error);
			}
			if (Code == TEXT("object.not_compiled"))
			{
				return SendError(OnComplete, 409, TEXT("object.not_compiled"), Error);
			}
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}
		return SendJson(OnComplete, 200, Body);
	}

	bool SetProperty(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}

		FString Path, Property, Value;
		Body->TryGetStringField(TEXT("path"), Path);
		Body->TryGetStringField(TEXT("property"), Property);
		Body->TryGetStringField(TEXT("value"), Value);
		if (Path.IsEmpty() || Property.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'path' and 'property' are required."));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		bool bNotFound = false;
		if (!ObjectOps::SetProperty(Path, Property, Value, Result, Error, bNotFound))
		{
			return bNotFound
				? SendError(OnComplete, 404, TEXT("object.not_found"), Error)
				: SendError(OnComplete, 422, TEXT("object.set_property_failed"), Error);
		}

		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("op"), TEXT("set-property"));
		Payload->SetStringField(TEXT("path"), Path);
		UECli::Services::Events().Broadcast(TEXT("asset.changed"), Payload);
		return SendJson(OnComplete, 200, Result);
	}
}
