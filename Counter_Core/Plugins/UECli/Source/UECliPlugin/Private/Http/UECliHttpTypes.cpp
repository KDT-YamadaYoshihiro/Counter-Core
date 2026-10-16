// Copyright UE CLI. All rights reserved.

#include "Http/UECliHttpTypes.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpServerResponse.h"
#include "HttpServerConstants.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UECli::Http
{
	static FString SerializeObject(const TSharedRef<FJsonObject>& Object)
	{
		FString Output;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Object, Writer);
		return Output;
	}

	FString QueryParam(const FHttpServerRequest& Request, const FString& Key)
	{
		return QueryParam(Request, Key, FString());
	}

	FString QueryParam(const FHttpServerRequest& Request, const FString& Key, const FString& Default)
	{
		for (const TPair<FString, FString>& Pair : Request.QueryParams)
		{
			if (Pair.Key.Equals(Key, ESearchCase::IgnoreCase))
			{
				return Pair.Value;
			}
		}

		return Default;
	}

	FString HeaderValue(const FHttpServerRequest& Request, const FString& Key)
	{
		for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
		{
			if (Pair.Key.Equals(Key, ESearchCase::IgnoreCase) && Pair.Value.Num() > 0)
			{
				return Pair.Value[0];
			}
		}

		return FString();
	}

	bool TokenEquals(const FString& A, const FString& B)
	{
		const int32 Len = FMath::Max(A.Len(), B.Len());
		uint32 Diff = static_cast<uint32>(A.Len() ^ B.Len());
		for (int32 Index = 0; Index < Len; ++Index)
		{
			const TCHAR CA = Index < A.Len() ? A[Index] : 0;
			const TCHAR CB = Index < B.Len() ? B[Index] : 0;
			Diff |= static_cast<uint32>(CA ^ CB);
		}
		return Diff == 0;
	}

	TSharedPtr<FJsonObject> ParseJsonBody(const FHttpServerRequest& Request, FString& OutError)
	{
		if (Request.Body.Num() == 0)
		{
			OutError = TEXT("Request body is empty.");
			return nullptr;
		}

		const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		const FString Text(Converter.Length(), Converter.Get());

		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
		{
			OutError = TEXT("Request body is not a JSON object.");
			return nullptr;
		}

		OutError.Empty();
		return Object;
	}

	bool SendJson(const FHttpResultCallback& OnComplete, int32 StatusCode, const TSharedRef<FJsonObject>& Body)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(SerializeObject(Body), TEXT("application/json"));
		Response->Code = static_cast<EHttpServerResponseCodes>(StatusCode);
		OnComplete(MoveTemp(Response));
		return true;
	}

	bool SendJsonArray(const FHttpResultCallback& OnComplete, int32 StatusCode, const TArray<TSharedPtr<FJsonValue>>& Items)
	{
		FString Output;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Items, Writer);

		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Output, TEXT("application/json"));
		Response->Code = static_cast<EHttpServerResponseCodes>(StatusCode);
		OnComplete(MoveTemp(Response));
		return true;
	}

	bool SendError(const FHttpResultCallback& OnComplete, int32 StatusCode, const FString& ErrorCode, const FString& Message, const TSharedPtr<FJsonObject>& Details)
	{
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("error"), ErrorCode);
		Body->SetStringField(TEXT("message"), Message);
		if (Details.IsValid())
		{
			Body->SetObjectField(TEXT("details"), Details);
		}

		return SendJson(OnComplete, StatusCode, Body);
	}
}
