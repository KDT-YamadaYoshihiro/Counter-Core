// Copyright UE CLI. All rights reserved.

#include "Handlers/UECliLevelHandlers.h"
#include "UECliServices.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Events/UECliEventHub.h"
#include "Http/UECliHttpTypes.h"
#include "Level/UECliLevelOps.h"

namespace UECli::LevelHandlers
{
	using namespace UECli::Http;

	namespace
	{
		TOptional<FVector> ReadVector(const TSharedPtr<FJsonObject>& Body, const TCHAR* Field)
		{
			const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
			if (Body->TryGetArrayField(Field, Array) && Array->Num() == 3)
			{
				return FVector((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber(), (*Array)[2]->AsNumber());
			}
			return {};
		}

		TOptional<FRotator> ReadRotator(const TSharedPtr<FJsonObject>& Body, const TCHAR* Field)
		{
			const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
			if (Body->TryGetArrayField(Field, Array) && Array->Num() == 3)
			{
				// [pitch, yaw, roll]
				return FRotator((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber(), (*Array)[2]->AsNumber());
			}
			return {};
		}

		void NotifyLevelChanged(const TCHAR* Op, const FString& Actor)
		{
			const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("op"), Op);
			if (!Actor.IsEmpty())
			{
				Payload->SetStringField(TEXT("actor"), Actor);
			}
			UECli::Services::Events().Broadcast(TEXT("level.changed"), Payload);
		}
	}

	bool ListActors(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		return SendJsonArray(OnComplete, 200,
			LevelOps::ListActors(QueryParam(Request, TEXT("class")), QueryParam(Request, TEXT("name"))));
	}

	bool SpawnActor(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}

		FString ClassRef;
		if (!Body->TryGetStringField(TEXT("class"), ClassRef) || ClassRef.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'class' is required."));
		}

		const FVector Location = ReadVector(Body, TEXT("location")).Get(FVector::ZeroVector);
		const FRotator Rotation = ReadRotator(Body, TEXT("rotation")).Get(FRotator::ZeroRotator);
		FString Name;
		Body->TryGetStringField(TEXT("name"), Name);

		TSharedRef<FJsonObject> Actor = MakeShared<FJsonObject>();
		if (!LevelOps::SpawnActor(ClassRef, Location, Rotation, Name, Actor, Error))
		{
			return SendError(OnComplete, 422, TEXT("level.spawn_failed"), Error);
		}

		FString SpawnedName;
		Actor->TryGetStringField(TEXT("name"), SpawnedName);
		NotifyLevelChanged(TEXT("spawn"), SpawnedName);
		return SendJson(OnComplete, 200, Actor);
	}

	bool DeleteActor(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const FString Ref = QueryParam(Request, TEXT("name"));
		if (Ref.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'name' is required."));
		}

		FString Error;
		if (!LevelOps::DeleteActor(Ref, Error))
		{
			return SendError(OnComplete, 422, TEXT("level.delete_failed"), Error);
		}

		NotifyLevelChanged(TEXT("delete"), Ref);
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetBoolField(TEXT("ok"), true);
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

		FString Actor, Property, Value;
		Body->TryGetStringField(TEXT("actor"), Actor);
		Body->TryGetStringField(TEXT("property"), Property);
		Body->TryGetStringField(TEXT("value"), Value);
		if (Actor.IsEmpty() || Property.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'actor' and 'property' are required."));
		}

		if (!LevelOps::SetActorProperty(Actor, Property, Value, Error))
		{
			return SendError(OnComplete, 422, TEXT("level.set_property_failed"), Error);
		}

		NotifyLevelChanged(TEXT("set-property"), Actor);
		const TSharedRef<FJsonObject> ResponseBody = MakeShared<FJsonObject>();
		ResponseBody->SetBoolField(TEXT("ok"), true);
		return SendJson(OnComplete, 200, ResponseBody);
	}

	bool SetTransform(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}

		FString Actor;
		Body->TryGetStringField(TEXT("actor"), Actor);
		if (Actor.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'actor' is required."));
		}

		if (!LevelOps::SetActorTransform(Actor,
			ReadVector(Body, TEXT("location")), ReadRotator(Body, TEXT("rotation")), ReadVector(Body, TEXT("scale")), Error))
		{
			return SendError(OnComplete, 422, TEXT("level.set_transform_failed"), Error);
		}

		NotifyLevelChanged(TEXT("transform"), Actor);
		const TSharedRef<FJsonObject> ResponseBody = MakeShared<FJsonObject>();
		ResponseBody->SetBoolField(TEXT("ok"), true);
		return SendJson(OnComplete, 200, ResponseBody);
	}
}
