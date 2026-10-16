// Copyright UE CLI. All rights reserved.

#include "Handlers/UECliCodeHandlers.h"
#include "UECliServices.h"

#include "Dom/JsonObject.h"
#include "Events/UECliEventHub.h"
#include "Http/UECliHttpTypes.h"
#include "Jobs/UECliJob.h"
#include "Jobs/UECliJobManager.h"
#include "Runtime/UECliLogCapture.h"
#include "Modules/ModuleManager.h"
#include "UECliLog.h"

#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#define UECLI_HAS_LIVE_CODING 1
#else
#define UECLI_HAS_LIVE_CODING 0
#endif

namespace UECli::CodeHandlers
{
	using namespace UECli::Http;

	namespace
	{
		FString GLastResult = TEXT("none");
		FDelegateHandle GPatchHandle;

		void BroadcastFinished(const FString& Result)
		{
			GLastResult = Result;
			const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("channel"), TEXT("livecoding"));
			Payload->SetStringField(TEXT("result"), Result);
			Payload->SetBoolField(TEXT("success"), Result == TEXT("Success") || Result == TEXT("NoChanges"));
			UECli::Services::Events().Broadcast(TEXT("code.compile.finished"), Payload);
		}

#if UECLI_HAS_LIVE_CODING
		ILiveCodingModule* LiveCoding()
		{
			return FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
		}

		const TCHAR* ResultName(ELiveCodingCompileResult Result)
		{
			switch (Result)
			{
			case ELiveCodingCompileResult::Success:             return TEXT("Success");
			case ELiveCodingCompileResult::NoChanges:           return TEXT("NoChanges");
			case ELiveCodingCompileResult::InProgress:          return TEXT("InProgress");
			case ELiveCodingCompileResult::CompileStillActive:  return TEXT("CompileStillActive");
			case ELiveCodingCompileResult::NotStarted:          return TEXT("NotStarted");
			case ELiveCodingCompileResult::Failure:             return TEXT("Failure");
			case ELiveCodingCompileResult::Cancelled:           return TEXT("Cancelled");
			default:                                            return TEXT("Unknown");
			}
		}
		/**
		 * Non-blocking Live Coding compile. The module only exposes a success
		 * delegate, so the other outcomes are read from LogLiveCoding's own
		 * result lines (LiveCodingModule.cpp: "no code changes detected",
		 * "Live coding canceled", "Live coding failed").
		 */
		class FLiveCompileJob;
		TWeakPtr<FLiveCompileJob> GActiveJob;

		class FLiveCompileJob : public FUECliJob
		{
		public:
			using FUECliJob::FUECliJob;

			bool bPatched = false;

			virtual void Start() override
			{
				ILiveCodingModule* LC = LiveCoding();
				if (!LC)
				{
					Fail(TEXT("Live Coding is not available"));
					return;
				}
				if (FUECliLogCapture* Capture = FUECliLogCapture::Active())
				{
					StartSeq = Capture->HighestSeq() + 1;
				}

				const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
				Payload->SetStringField(TEXT("channel"), TEXT("livecoding"));
				UECli::Services::Events().Broadcast(TEXT("code.compile.started"), Payload);

				ELiveCodingCompileResult Started = ELiveCodingCompileResult::InProgress;
				LC->Compile(ELiveCodingCompileFlags::None, &Started);
				if (Started != ELiveCodingCompileResult::InProgress)
				{
					Complete(ResultName(Started));
					return;
				}
				Message = TEXT("compiling");
			}

			virtual void Tick(float DeltaSeconds) override
			{
				if (IsTerminal())
				{
					return;
				}
				Elapsed += DeltaSeconds;

				ILiveCodingModule* LC = LiveCoding();
				if (LC && LC->IsCompiling())
				{
					IdleElapsed = 0.0f;
					if (Elapsed > TimeoutSeconds)
					{
						Complete(TEXT("Timeout"));
					}
					return;
				}

				// The module logs its result a frame or more after IsCompiling()
				// drops, so wait for that line (or the patch delegate) briefly.
				IdleElapsed += DeltaSeconds;
				const FString Outcome = bPatched ? FString(TEXT("Success")) : ResultFromLog();
				if (Outcome != TEXT("Unknown") || IdleElapsed > 5.0f)
				{
					Complete(Outcome);
				}
			}

		private:
			FString ResultFromLog() const
			{
				FUECliLogCapture* Capture = FUECliLogCapture::Active();
				if (!Capture)
				{
					return TEXT("Unknown");
				}
				const TSharedRef<FJsonObject> Page = MakeShared<FJsonObject>();
				Capture->Read(StartSeq, 500, ELogVerbosity::Log, TEXT("LogLiveCoding"), Page);
				const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
				FString Outcome = TEXT("Unknown");
				if (Page->TryGetArrayField(TEXT("entries"), Entries))
				{
					for (const TSharedPtr<FJsonValue>& Entry : *Entries)
					{
						const TSharedPtr<FJsonObject>* Obj = nullptr;
						if (!Entry->TryGetObject(Obj))
						{
							continue;
						}
						const FString Line = (*Obj)->GetStringField(TEXT("message"));
						if (Line.Contains(TEXT("no code changes detected")))      { Outcome = TEXT("NoChanges"); }
						else if (Line.Contains(TEXT("Live coding canceled")))     { Outcome = TEXT("Cancelled"); }
						else if (Line.Contains(TEXT("Live coding failed")))       { Outcome = TEXT("Failure"); }
						else if (Line.Contains(TEXT("Live coding succeeded")))    { Outcome = TEXT("Success"); }
					}
				}
				return Outcome;
			}

			void Complete(const FString& Outcome)
			{
				if (GActiveJob.Pin().Get() == this)
				{
					GActiveJob.Reset();
				}
				BroadcastFinished(Outcome);

				const bool bOk = Outcome == TEXT("Success") || Outcome == TEXT("NoChanges");
				const TSharedRef<FJsonObject> Res = MakeShared<FJsonObject>();
				Res->SetStringField(TEXT("result"), Outcome);
				Res->SetBoolField(TEXT("success"), bOk);
				Res->SetNumberField(TEXT("seconds"), Elapsed);
				Result = Res;
				if (bOk)
				{
					Succeed(Res, Outcome);
				}
				else if (Outcome == TEXT("Cancelled"))
				{
					Finish(EUECliJobStatus::Cancelled, Outcome);
				}
				else
				{
					Fail(Outcome == TEXT("Failure")
						? TEXT("Failure: see the Live Coding console / LogLiveCoding for compiler errors")
						: Outcome);
				}
			}

			uint64 StartSeq = 1;
			float Elapsed = 0.0f;
			float IdleElapsed = 0.0f;
			static constexpr float TimeoutSeconds = 600.0f;
		};
#endif

		TSharedRef<FJsonObject> StatusJson()
		{
			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
#if UECLI_HAS_LIVE_CODING
			if (ILiveCodingModule* LC = LiveCoding())
			{
				Json->SetBoolField(TEXT("available"), true);
				Json->SetBoolField(TEXT("enabledForSession"), LC->IsEnabledForSession());
				Json->SetBoolField(TEXT("canEnable"), LC->CanEnableForSession());
				Json->SetBoolField(TEXT("compiling"), LC->IsCompiling());
				Json->SetStringField(TEXT("lastResult"), GLastResult);
				return Json;
			}
#endif
			Json->SetBoolField(TEXT("available"), false);
			Json->SetBoolField(TEXT("enabledForSession"), false);
			Json->SetBoolField(TEXT("canEnable"), false);
			Json->SetBoolField(TEXT("compiling"), false);
			return Json;
		}
	}

	void Init()
	{
#if UECLI_HAS_LIVE_CODING
		if (ILiveCodingModule* LC = LiveCoding())
		{
			// Fires only when a patch was applied, i.e. a successful compile. A
			// compile we started is reported by its job; one the user started
			// (Ctrl+Alt+F11) is reported here.
			GPatchHandle = LC->GetOnPatchCompleteDelegate().AddLambda([]()
			{
				if (const TSharedPtr<FLiveCompileJob> Job = GActiveJob.Pin())
				{
					Job->bPatched = true;
					return;
				}
				BroadcastFinished(TEXT("Success"));
			});
		}

		UECli::Services::Jobs().RegisterKind(TEXT("code.live-compile"),
			[](const TSharedPtr<FJsonObject>& Params, FString&) -> TSharedPtr<FUECliJob>
			{
				return MakeShared<FLiveCompileJob>(TEXT("code.live-compile"), Params);
			});
#endif
	}

	void Shutdown()
	{
#if UECLI_HAS_LIVE_CODING
		if (GPatchHandle.IsValid())
		{
			if (ILiveCodingModule* LC = LiveCoding())
			{
				LC->GetOnPatchCompleteDelegate().Remove(GPatchHandle);
			}
			GPatchHandle.Reset();
		}
#endif
	}

	bool LiveCodingStatus(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		return SendJson(OnComplete, 200, StatusJson());
	}

	bool LiveCodingCompile(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
#if UECLI_HAS_LIVE_CODING
		ILiveCodingModule* LC = LiveCoding();
		if (!LC)
		{
			return SendError(OnComplete, 503, TEXT("livecoding.unavailable"), TEXT("Live Coding is not available in this editor."));
		}
		if (!LC->IsEnabledForSession())
		{
			if (!LC->CanEnableForSession())
			{
				return SendError(OnComplete, 409, TEXT("livecoding.disabled"),
					FString::Printf(TEXT("Live Coding cannot be enabled: %s"), *LC->GetEnableErrorText().ToString()));
			}
			LC->EnableForSession(true);
		}

		const TSharedPtr<FLiveCompileJob> Active = GActiveJob.Pin();
		if (LC->IsCompiling() || (Active.IsValid() && !Active->IsTerminal())) // a cancelled job stays retained but is not busy
		{
			return SendError(OnComplete, 409, TEXT("livecoding.busy"), TEXT("A Live Coding compile is already running."));
		}

		// Never block the game thread: the compile runs as a `code.live-compile`
		// job; the client waits on the job (body field `wait` is client-side only).
		FString Error;
		const TSharedPtr<FUECliJob> Job = UECli::Services::Jobs().Enqueue(TEXT("code.live-compile"), nullptr, Error);
		if (!Job.IsValid())
		{
			return SendError(OnComplete, 500, TEXT("job.enqueue_failed"), Error);
		}
		GActiveJob = StaticCastSharedPtr<FLiveCompileJob>(Job);

		const TSharedRef<FJsonObject> Json = StatusJson();
		Json->SetStringField(TEXT("result"), TEXT("InProgress"));
		Json->SetStringField(TEXT("jobId"), Job->Id.ToString(EGuidFormats::DigitsWithHyphens));
		return SendJson(OnComplete, 202, Json);
#else
		return SendError(OnComplete, 503, TEXT("livecoding.unavailable"), TEXT("Live Coding is Windows-only."));
#endif
	}
}
