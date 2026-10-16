// Copyright UE CLI. All rights reserved.

#include "Handlers/UECliNiagaraHandlers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Http/UECliHttpTypes.h"
#include "Niagara/UECliNiagaraOps.h"

namespace UECli::NiagaraHandlers
{
	using namespace UECli::Http;

	namespace
	{
		bool Fail(const FHttpResultCallback& OnComplete, bool bNotFound, const FString& Error)
		{
			return bNotFound
				? SendError(OnComplete, 404, TEXT("niagara.not_found"), Error)
				: SendError(OnComplete, 422, TEXT("niagara.edit_failed"), Error);
		}

		/** Parse the body and require the listed string fields; fills Fields in the same order. */
		TSharedPtr<FJsonObject> Body(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete,
			std::initializer_list<const TCHAR*> Required, TArray<FString>& Fields, bool& bHandled)
		{
			FString Error;
			TSharedPtr<FJsonObject> Json = ParseJsonBody(Request, Error);
			bHandled = false;
			if (!Json.IsValid())
			{
				bHandled = SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
				return nullptr;
			}
			for (const TCHAR* Key : Required)
			{
				FString Value;
				if (!Json->TryGetStringField(Key, Value) || Value.IsEmpty())
				{
					bHandled = SendError(OnComplete, 400, TEXT("request.bad_request"), FString::Printf(TEXT("Body field '%s' is required."), Key));
					return nullptr;
				}
				Fields.Add(Value);
			}
			return Json;
		}

		bool Query(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete,
			std::initializer_list<const TCHAR*> Required, TArray<FString>& Fields, bool& bHandled)
		{
			bHandled = false;
			for (const TCHAR* Key : Required)
			{
				const FString Value = QueryParam(Request, Key);
				if (Value.IsEmpty())
				{
					bHandled = SendError(OnComplete, 400, TEXT("request.bad_request"), FString::Printf(TEXT("Query parameter '%s' is required."), Key));
					return false;
				}
				Fields.Add(Value);
			}
			return true;
		}

		/** Reply with the system's current state (used after removals / toggles). */
		bool SendSystem(const FHttpResultCallback& OnComplete, const FString& Path)
		{
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			FString Error;
			bool bNotFound = false;
			if (!NiagaraOps::Describe(Path, Out, Error, bNotFound))
			{
				return Fail(OnComplete, bNotFound, Error);
			}
			return SendJson(OnComplete, 200, Out);
		}
	}

	bool Describe(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		if (!Query(Request, OnComplete, { TEXT("path") }, F, bHandled))
		{
			return bHandled;
		}
		return SendSystem(OnComplete, F[0]);
	}

	bool CreateSystem(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		const TSharedPtr<FJsonObject> Json = Body(Request, OnComplete, { TEXT("path") }, F, bHandled);
		if (!Json.IsValid())
		{
			return bHandled;
		}
		TArray<FString> Emitters;
		Json->TryGetStringArrayField(TEXT("emitters"), Emitters);
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		if (!NiagaraOps::CreateSystem(F[0], Emitters, Out, Error))
		{
			return SendError(OnComplete, 422, TEXT("niagara.edit_failed"), Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool AddEmitter(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		const TSharedPtr<FJsonObject> Json = Body(Request, OnComplete, { TEXT("path"), TEXT("emitter") }, F, bHandled);
		if (!Json.IsValid())
		{
			return bHandled;
		}
		FString Name;
		Json->TryGetStringField(TEXT("name"), Name);
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::AddEmitter(F[0], F[1], Name, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool RemoveEmitter(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		if (!Query(Request, OnComplete, { TEXT("path"), TEXT("emitter") }, F, bHandled))
		{
			return bHandled;
		}
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::RemoveEmitter(F[0], F[1], Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendSystem(OnComplete, F[0]);
	}

	bool AddModule(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		const TSharedPtr<FJsonObject> Json = Body(Request, OnComplete, { TEXT("path"), TEXT("emitter"), TEXT("stage"), TEXT("script") }, F, bHandled);
		if (!Json.IsValid())
		{
			return bHandled;
		}
		int32 Index = -1;
		Json->TryGetNumberField(TEXT("index"), Index);
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::AddModule(F[0], F[1], F[2], F[3], Index, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool RemoveModule(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		if (!Query(Request, OnComplete, { TEXT("path"), TEXT("emitter"), TEXT("stage"), TEXT("module") }, F, bHandled))
		{
			return bHandled;
		}
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::RemoveModule(F[0], F[1], F[2], F[3], Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendSystem(OnComplete, F[0]);
	}

	bool MoveModule(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		const TSharedPtr<FJsonObject> Json = Body(Request, OnComplete, { TEXT("path"), TEXT("emitter"), TEXT("stage"), TEXT("module") }, F, bHandled);
		if (!Json.IsValid())
		{
			return bHandled;
		}
		int32 Index = 0;
		if (!Json->TryGetNumberField(TEXT("index"), Index))
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'index' (number) is required."));
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::MoveModule(F[0], F[1], F[2], F[3], Index, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool SetModuleEnabled(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		const TSharedPtr<FJsonObject> Json = Body(Request, OnComplete, { TEXT("path"), TEXT("emitter"), TEXT("stage"), TEXT("module") }, F, bHandled);
		if (!Json.IsValid())
		{
			return bHandled;
		}
		bool bEnabled = true;
		if (!Json->TryGetBoolField(TEXT("enabled"), bEnabled))
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'enabled' (bool) is required."));
		}
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::SetModuleEnabled(F[0], F[1], F[2], F[3], bEnabled, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendSystem(OnComplete, F[0]);
	}

	bool SetInput(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		const TSharedPtr<FJsonObject> Json = Body(Request, OnComplete, { TEXT("path"), TEXT("emitter"), TEXT("stage"), TEXT("module"), TEXT("input") }, F, bHandled);
		if (!Json.IsValid())
		{
			return bHandled;
		}
		const TSharedPtr<FJsonValue> Value = Json->TryGetField(TEXT("value"));
		if (!Value.IsValid() || Value->IsNull())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'value' is required."));
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::SetInput(F[0], F[1], F[2], F[3], F[4], Value, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool SetDynamicInput(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		const TSharedPtr<FJsonObject> Json = Body(Request, OnComplete, { TEXT("path"), TEXT("emitter"), TEXT("stage"), TEXT("module"), TEXT("input"), TEXT("script") }, F, bHandled);
		if (!Json.IsValid())
		{
			return bHandled;
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::SetDynamicInput(F[0], F[1], F[2], F[3], F[4], F[5], Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool LinkInput(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		const TSharedPtr<FJsonObject> Json = Body(Request, OnComplete, { TEXT("path"), TEXT("emitter"), TEXT("stage"), TEXT("module"), TEXT("input"), TEXT("parameter") }, F, bHandled);
		if (!Json.IsValid())
		{
			return bHandled;
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::LinkInput(F[0], F[1], F[2], F[3], F[4], F[5], Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool ResetInput(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		if (!Query(Request, OnComplete, { TEXT("path"), TEXT("emitter"), TEXT("stage"), TEXT("module"), TEXT("input") }, F, bHandled))
		{
			return bHandled;
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::ResetInput(F[0], F[1], F[2], F[3], F[4], Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool AddRenderer(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		const TSharedPtr<FJsonObject> Json = Body(Request, OnComplete, { TEXT("path"), TEXT("emitter"), TEXT("type") }, F, bHandled);
		if (!Json.IsValid())
		{
			return bHandled;
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::AddRenderer(F[0], F[1], F[2], Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool SetUserParameter(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		const TSharedPtr<FJsonObject> Json = Body(Request, OnComplete, { TEXT("path"), TEXT("name") }, F, bHandled);
		if (!Json.IsValid())
		{
			return bHandled;
		}
		const TSharedPtr<FJsonValue> Value = Json->TryGetField(TEXT("value"));
		if (!Value.IsValid() || Value->IsNull())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'value' is required."));
		}
		FString Type;
		Json->TryGetStringField(TEXT("type"), Type);
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::SetUserParameter(F[0], F[1], Type, Value, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool RemoveUserParameter(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		if (!Query(Request, OnComplete, { TEXT("path"), TEXT("name") }, F, bHandled))
		{
			return bHandled;
		}
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::RemoveUserParameter(F[0], F[1], Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendSystem(OnComplete, F[0]);
	}

	bool SetEmitterSettings(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		const TSharedPtr<FJsonObject> Json = Body(Request, OnComplete, { TEXT("path"), TEXT("emitter") }, F, bHandled);
		if (!Json.IsValid())
		{
			return bHandled;
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::SetEmitterSettings(F[0], F[1], Json.ToSharedRef(), Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool RemoveRenderer(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		if (!Query(Request, OnComplete, { TEXT("path"), TEXT("emitter"), TEXT("index") }, F, bHandled))
		{
			return bHandled;
		}
		if (!F[2].IsNumeric())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameter 'index' must be a number."));
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::RemoveRenderer(F[0], F[1], FCString::Atoi(*F[2]), Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool Compile(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TArray<FString> F;
		bool bHandled = false;
		const TSharedPtr<FJsonObject> Json = Body(Request, OnComplete, { TEXT("path") }, F, bHandled);
		if (!Json.IsValid())
		{
			return bHandled;
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!NiagaraOps::Compile(F[0], Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}
}
