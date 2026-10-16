// Copyright UE CLI. All rights reserved.

#include "Handlers/UECliWidgetHandlers.h"
#include "UECliServices.h"

#include "Dom/JsonObject.h"
#include "Events/UECliEventHub.h"
#include "Http/UECliHttpTypes.h"
#include "Widget/UECliWidgetOps.h"

namespace UECli::WidgetHandlers
{
	using namespace UECli::Http;

	namespace
	{
		bool Fail(const FHttpResultCallback& OnComplete, bool bNotFound, const FString& Error)
		{
			return bNotFound
				? SendError(OnComplete, 404, TEXT("widget.not_found"), Error)
				: SendError(OnComplete, 422, TEXT("widget.edit_failed"), Error);
		}

		void NotifyChanged(const FString& Path, const TCHAR* Op)
		{
			const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("op"), Op);
			Payload->SetStringField(TEXT("path"), Path);
			UECli::Services::Events().Broadcast(TEXT("asset.changed"), Payload);
		}

		bool SendOk(const FHttpResultCallback& OnComplete)
		{
			const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetBoolField(TEXT("ok"), true);
			return SendJson(OnComplete, 200, Body);
		}

		int32 IndexField(const TSharedPtr<FJsonObject>& Body)
		{
			double Index = -1;
			Body->TryGetNumberField(TEXT("index"), Index);
			return static_cast<int32>(Index);
		}
	}

	bool GetTree(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const FString Path = QueryParam(Request, TEXT("path"));
		if (Path.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'path' is required."));
		}

		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!WidgetOps::GetTree(Path, Body, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Body);
	}

	bool AddWidget(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}

		FString Path, Class, Name, Parent;
		Body->TryGetStringField(TEXT("path"), Path);
		Body->TryGetStringField(TEXT("class"), Class);
		Body->TryGetStringField(TEXT("name"), Name);
		Body->TryGetStringField(TEXT("parent"), Parent);
		if (Path.IsEmpty() || Class.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'path' and 'class' are required."));
		}

		TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
		bool bNotFound = false;
		if (!WidgetOps::AddWidget(Path, Class, Name, Parent, IndexField(Body), Node, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		NotifyChanged(Path, TEXT("widget-add"));
		return SendJson(OnComplete, 200, Node);
	}

	bool RemoveWidget(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const FString Path = QueryParam(Request, TEXT("path"));
		const FString Name = QueryParam(Request, TEXT("name"));
		if (Path.IsEmpty() || Name.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameters 'path' and 'name' are required."));
		}

		FString Error;
		bool bNotFound = false;
		if (!WidgetOps::RemoveWidget(Path, Name, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		NotifyChanged(Path, TEXT("widget-remove"));
		return SendOk(OnComplete);
	}

	bool Reparent(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}

		FString Path, Name, Parent;
		Body->TryGetStringField(TEXT("path"), Path);
		Body->TryGetStringField(TEXT("name"), Name);
		Body->TryGetStringField(TEXT("parent"), Parent);
		if (Path.IsEmpty() || Name.IsEmpty() || Parent.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'path', 'name' and 'parent' are required."));
		}

		TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
		bool bNotFound = false;
		if (!WidgetOps::Reparent(Path, Name, Parent, IndexField(Body), Node, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		NotifyChanged(Path, TEXT("widget-move"));
		return SendJson(OnComplete, 200, Node);
	}

	bool SetBinding(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}

		FString Path, Widget, Property, Function;
		Body->TryGetStringField(TEXT("path"), Path);
		Body->TryGetStringField(TEXT("widget"), Widget);
		Body->TryGetStringField(TEXT("property"), Property);
		Body->TryGetStringField(TEXT("function"), Function);
		if (Path.IsEmpty() || Widget.IsEmpty() || Property.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'path', 'widget' and 'property' are required."));
		}

		bool bNotFound = false;
		if (!WidgetOps::SetBinding(Path, Widget, Property, Function, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		NotifyChanged(Path, TEXT("widget-binding"));
		return SendOk(OnComplete);
	}
}
