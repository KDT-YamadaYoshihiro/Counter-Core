// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"

class FJsonObject;
class FJsonValue;

/** Helpers shared by every UE CLI request handler: query access + JSON replies. */
namespace UECli::Http
{
	/** First value of a query parameter, or empty. */
	FString QueryParam(const FHttpServerRequest& Request, const FString& Key);

	/** First value of a query parameter, or a fallback. */
	FString QueryParam(const FHttpServerRequest& Request, const FString& Key, const FString& Default);

	/** Case-insensitive first value of a request header, or empty. */
	FString HeaderValue(const FHttpServerRequest& Request, const FString& Key);

	/** Constant-time string equality, so a token cannot be probed byte by byte. */
	bool TokenEquals(const FString& A, const FString& B);

	/** Parse the request body as a JSON object. Returns null (and fills OutError) on failure. */
	TSharedPtr<FJsonObject> ParseJsonBody(const FHttpServerRequest& Request, FString& OutError);

	/** Send a JSON object with the given status code. Always returns true. */
	bool SendJson(const FHttpResultCallback& OnComplete, int32 StatusCode, const TSharedRef<FJsonObject>& Body);

	/** Send a JSON array with the given status code. Always returns true. */
	bool SendJsonArray(const FHttpResultCallback& OnComplete, int32 StatusCode, const TArray<TSharedPtr<FJsonValue>>& Items);

	/** Send the { error, message, details? } envelope the Core maps to UECliException. */
	bool SendError(const FHttpResultCallback& OnComplete, int32 StatusCode, const FString& ErrorCode, const FString& Message, const TSharedPtr<FJsonObject>& Details = nullptr);
}
