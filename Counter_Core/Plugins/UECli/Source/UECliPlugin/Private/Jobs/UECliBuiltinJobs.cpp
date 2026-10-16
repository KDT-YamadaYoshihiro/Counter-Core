// Copyright UE CLI. All rights reserved.

#include "Jobs/UECliBuiltinJobs.h"
#include "UECliServices.h"

#include "Blueprint/UECliBlueprintCompiler.h"
#include "Blueprint/UECliBlueprintReader.h"
#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorBuildUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "IAutomationControllerManager.h"
#include "IAutomationControllerModule.h"
#include "IAutomationReport.h"
#include "IPythonScriptPlugin.h"
#include "Jobs/UECliJob.h"
#include "Jobs/UECliJobManager.h"
#include "Misc/AutomationTest.h"
#include "NavigationSystem.h"
#include "PlayInEditorDataTypes.h"
#include "Project/UECliProjectOps.h"
#include "Algo/Reverse.h"
#include "Runtime/UECliLogCapture.h"
#include "UECliLog.h"

namespace UECli::Jobs
{
	namespace
	{
		int32 IntParam(const TSharedRef<FJsonObject>& Params, const TCHAR* Field, int32 Default)
		{
			double Value = 0;
			return Params->TryGetNumberField(Field, Value) ? static_cast<int32>(Value) : Default;
		}

		FString StringParam(const TSharedRef<FJsonObject>& Params, const TCHAR* Field)
		{
			FString Value;
			Params->TryGetStringField(Field, Value);
			return Value;
		}
	}

	// ---------------------------------------------------------------- compile-all

	class FCompileAllJob : public FUECliJob
	{
	public:
		using FUECliJob::FUECliJob;

		virtual void Start() override
		{
			const FString Prefix = StringParam(Params, TEXT("path"));
			for (const TSharedPtr<FJsonValue>& Value : BlueprintReader::ListBlueprints(Prefix))
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (Value->TryGetObject(Obj))
				{
					FString Path;
					if ((*Obj)->TryGetStringField(TEXT("path"), Path))
					{
						Pending.Add(Path);
					}
				}
			}
			Total = Pending.Num();
			Progress = Total > 0 ? 0.0f : 1.0f;
			Message = FString::Printf(TEXT("%d blueprints"), Total);
		}

		virtual void Tick(float) override
		{
			if (Pending.Num() == 0)
			{
				const TSharedRef<FJsonObject> Res = MakeShared<FJsonObject>();
				Res->SetNumberField(TEXT("total"), Total);
				Res->SetNumberField(TEXT("succeeded"), Succeeded);
				Res->SetNumberField(TEXT("failed"), Failed);
				Res->SetArrayField(TEXT("blueprints"), Records);
				Succeed(Res, FString::Printf(TEXT("%d ok, %d failed"), Succeeded, Failed));
				return;
			}

			const FString Path = Pending.Pop(EAllowShrinking::No);
			const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
			Record->SetStringField(TEXT("blueprint"), Path);

			FString Error;
			if (UBlueprint* Blueprint = BlueprintReader::LoadBlueprintByPath(Path, Error))
			{
				TSharedRef<FJsonObject> Compile = MakeShared<FJsonObject>();
				BlueprintCompiler::Compile(*Blueprint, Compile, Error);
				const bool bOk = Compile->GetBoolField(TEXT("succeeded"));
				Record->SetStringField(TEXT("status"), Compile->GetStringField(TEXT("status")));
				Record->SetBoolField(TEXT("succeeded"), bOk);
				Record->SetNumberField(TEXT("errors"), Compile->GetNumberField(TEXT("errors")));
				Record->SetNumberField(TEXT("warnings"), Compile->GetNumberField(TEXT("warnings")));
				bOk ? ++Succeeded : ++Failed;
			}
			else
			{
				Record->SetBoolField(TEXT("succeeded"), false);
				Record->SetStringField(TEXT("error"), Error);
				++Failed;
			}
			Records.Add(MakeShared<FJsonValueObject>(Record));

			const int32 Done = Total - Pending.Num();
			Progress = Total > 0 ? static_cast<float>(Done) / Total : 1.0f;
			Message = FString::Printf(TEXT("%d/%d"), Done, Total);
		}

	private:
		TArray<FString> Pending;
		TArray<TSharedPtr<FJsonValue>> Records;
		int32 Total = 0;
		int32 Succeeded = 0;
		int32 Failed = 0;
	};

	// ---------------------------------------------------------------- pie.run

	class FPieRunJob : public FUECliJob
	{
	public:
		using FUECliJob::FUECliJob;

		virtual void Start() override
		{
			DurationSeconds = FMath::Max(1, IntParam(Params, TEXT("durationSeconds"), 5));
			StopOnLog = StringParam(Params, TEXT("stopOnLog"));

			if (FUECliLogCapture* Capture = FUECliLogCapture::Active())
			{
				StartSeq = Capture->HighestSeq() + 1;
			}

			if (!GEditor || GEditor->IsPlaySessionInProgress())
			{
				Fail(GEditor ? TEXT("a play session is already running") : TEXT("no editor"));
				return;
			}

			FRequestPlaySessionParams PlayParams;
			PlayParams.WorldType = EPlaySessionWorldType::PlayInEditor;
			GEditor->RequestPlaySession(PlayParams);
			Message = TEXT("starting PIE");
		}

		virtual void Tick(float DeltaSeconds) override
		{
			if (!GEditor)
			{
				Fail(TEXT("no editor"));
				return;
			}

			switch (Phase)
			{
			case EPhase::WaitingToStart:
				WaitElapsed += DeltaSeconds;
				if (GEditor->IsPlaySessionInProgress())
				{
					Phase = EPhase::Playing;
					Message = TEXT("playing");
				}
				else if (WaitElapsed > 15.0f)
				{
					Fail(TEXT("PIE did not start within 15s"));
				}
				break;

			case EPhase::Playing:
			{
				PlayElapsed += DeltaSeconds;
				Progress = FMath::Clamp(PlayElapsed / DurationSeconds, 0.0f, 0.99f);

				if (!GEditor->IsPlaySessionInProgress())
				{
					EndedReason = TEXT("ended");   // crash or self-stop
					Phase = EPhase::Collecting;
					break;
				}
				if (!StopOnLog.IsEmpty() && MatchedLog())
				{
					EndedReason = TEXT("logMatch");
					GEditor->RequestEndPlayMap();
					Phase = EPhase::Stopping;
				}
				else if (PlayElapsed >= DurationSeconds)
				{
					EndedReason = TEXT("duration");
					GEditor->RequestEndPlayMap();
					Phase = EPhase::Stopping;
				}
				break;
			}

			case EPhase::Stopping:
				if (!GEditor->IsPlaySessionInProgress())
				{
					Phase = EPhase::Collecting;
				}
				break;

			case EPhase::Collecting:
				Collect();
				break;
			}
		}

		virtual void Cancel() override
		{
			if (GEditor && GEditor->IsPlaySessionInProgress())
			{
				GEditor->RequestEndPlayMap();
			}
			FUECliJob::Cancel();
		}

	private:
		enum class EPhase { WaitingToStart, Playing, Stopping, Collecting };

		bool MatchedLog() const
		{
			FUECliLogCapture* Capture = FUECliLogCapture::Active();
			if (!Capture)
			{
				return false;
			}
			const TSharedRef<FJsonObject> Page = MakeShared<FJsonObject>();
			Capture->Read(StartSeq, 500, ELogVerbosity::Log, FString(), Page);
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (Page->TryGetArrayField(TEXT("entries"), Entries))
			{
				for (const TSharedPtr<FJsonValue>& Entry : *Entries)
				{
					const TSharedPtr<FJsonObject>* Obj = nullptr;
					if (Entry->TryGetObject(Obj) && (*Obj)->GetStringField(TEXT("message")).Contains(StopOnLog))
					{
						return true;
					}
				}
			}
			return false;
		}

		void Collect()
		{
			const TSharedRef<FJsonObject> ResultJson = MakeShared<FJsonObject>();
			ResultJson->SetBoolField(TEXT("ran"), true);
			ResultJson->SetStringField(TEXT("endedReason"), EndedReason);
			ResultJson->SetNumberField(TEXT("playSeconds"), PlayElapsed);

			TArray<TSharedPtr<FJsonValue>> Errors;
			int32 Warnings = 0;
			if (FUECliLogCapture* Capture = FUECliLogCapture::Active())
			{
				const TSharedRef<FJsonObject> Page = MakeShared<FJsonObject>();
				Capture->Read(StartSeq, 1000, ELogVerbosity::Warning, FString(), Page);
				const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
				if (Page->TryGetArrayField(TEXT("entries"), Entries))
				{
					for (const TSharedPtr<FJsonValue>& Entry : *Entries)
					{
						const TSharedPtr<FJsonObject>* Obj = nullptr;
						if (Entry->TryGetObject(Obj))
						{
							if ((*Obj)->GetStringField(TEXT("severity")) == TEXT("error"))
							{
								Errors.Add(Entry);
							}
							else
							{
								++Warnings;
							}
						}
					}
				}
			}
			ResultJson->SetArrayField(TEXT("errors"), Errors);
			ResultJson->SetNumberField(TEXT("warnings"), Warnings);

			Succeed(ResultJson, FString::Printf(TEXT("%s, %d errors"), *EndedReason, Errors.Num()));
		}

		int32 DurationSeconds = 5;
		FString StopOnLog;
		uint64 StartSeq = 1;
		EPhase Phase = EPhase::WaitingToStart;
		float WaitElapsed = 0.0f;
		float PlayElapsed = 0.0f;
		FString EndedReason = TEXT("duration");
	};

	// ---------------------------------------------------------------- automation.run

	// Drives the engine's own headless automation path — the `Automation RunTests`
	// exec command that CI and the Session Frontend use — and reads the results
	// back off IAutomationControllerManager once the run completes. This keeps the
	// fragile bits (worker discovery, per-test latent-command pumping, the
	// GIsAutomationTesting global) inside the engine's driver, out of our tick.
	class FAutomationJob : public FUECliJob
	{
	public:
		using FUECliJob::FUECliJob;

		virtual void Start() override
		{
			Filter = StringParam(Params, TEXT("filter")).TrimStartAndEnd();
			TimeoutSeconds = FMath::Max(15, IntParam(Params, TEXT("timeoutSeconds"), 300));

			if (!GEngine)
			{
				Fail(TEXT("no engine"));
				return;
			}

			IAutomationControllerModule* Module =
				FModuleManager::LoadModulePtr<IAutomationControllerModule>(TEXT("AutomationController"));
			if (!Module)
			{
				Fail(TEXT("AutomationController module is not available"));
				return;
			}
			Controller = Module->GetAutomationController();

			if (Controller->GetTestState() == EAutomationControllerModuleState::Running)
			{
				Fail(TEXT("an automation run is already in progress"));
				return;
			}

			CompleteHandle = Controller->OnTestsComplete().AddSP(this, &FAutomationJob::HandleComplete);

			// Empty filter => the standard editor/product test set; otherwise a
			// substring match on the test path (same rule as the console command).
			Command = Filter.IsEmpty()
				? FString(TEXT("Automation RunFilter Product"))
				: FString::Printf(TEXT("Automation RunTests %s"), *Filter);

			GEngine->Exec(nullptr, *Command);
			Progress = 0.0f;
			Message = FString::Printf(TEXT("dispatched: %s"), *Command);
		}

		virtual void Tick(float DeltaSeconds) override
		{
			if (IsTerminal())
			{
				return;
			}

			Elapsed += DeltaSeconds;

			if (!Controller.IsValid())
			{
				Fail(TEXT("automation controller went away"));
				return;
			}

			if (Controller->GetTestState() == EAutomationControllerModuleState::Running)
			{
				bSawRunning = true;
				UpdateProgress();
			}

			if (bComplete)
			{
				Collect();
				return;
			}

			// Never went Running and never completed within the startup window. The
			// engine's exec driver takes this path silently when the filter matched
			// no tests; it also covers a worker that never registered.
			if (!bSawRunning && Elapsed > FMath::Min<float>(25.0f, static_cast<float>(TimeoutSeconds)))
			{
				Cleanup();
				Fail(FString::Printf(
					TEXT("automation run did not start within %ds — no test matched '%s', or the automation worker is unavailable"),
					FMath::Min(25, TimeoutSeconds),
					Filter.IsEmpty() ? TEXT("(standard set)") : *Filter));
				return;
			}

			if (Elapsed > TimeoutSeconds)
			{
				Controller->StopTests();
				Cleanup();
				Fail(FString::Printf(TEXT("automation run timed out after %ds"), TimeoutSeconds));
				return;
			}
		}

		virtual void Cancel() override
		{
			if (Controller.IsValid() && Controller->GetTestState() == EAutomationControllerModuleState::Running)
			{
				Controller->StopTests();
			}
			Cleanup();
			FUECliJob::Cancel();
		}

	private:
		void HandleComplete()
		{
			bComplete = true;
		}

		void Cleanup()
		{
			if (Controller.IsValid() && CompleteHandle.IsValid())
			{
				Controller->OnTestsComplete().Remove(CompleteHandle);
			}
			CompleteHandle.Reset();
		}

		template <typename FnType>
		void ForEachLeaf(FnType&& Fn)
		{
			if (!Controller.IsValid())
			{
				return;
			}
			for (const TSharedPtr<IAutomationReport>& Report : Controller->GetEnabledReports())
			{
				VisitLeaf(Report, Fn);
			}
		}

		template <typename FnType>
		static void VisitLeaf(const TSharedPtr<IAutomationReport>& Report, FnType& Fn)
		{
			if (!Report.IsValid())
			{
				return;
			}
			TArray<TSharedPtr<IAutomationReport>>& Children = Report->GetChildReports();
			if (Children.Num() == 0)
			{
				Fn(*Report);
				return;
			}
			for (const TSharedPtr<IAutomationReport>& Child : Children)
			{
				VisitLeaf(Child, Fn);
			}
		}

		void UpdateProgress()
		{
			int32 Total = 0;
			int32 Done = 0;
			ForEachLeaf([&Total, &Done](IAutomationReport& Report)
			{
				++Total;
				const EAutomationState State = Report.GetState(0, 0);
				if (State != EAutomationState::NotRun && State != EAutomationState::InProcess)
				{
					++Done;
				}
			});
			if (Total > 0)
			{
				Progress = FMath::Clamp(static_cast<float>(Done) / Total, 0.0f, 0.99f);
				Message = FString::Printf(TEXT("%d/%d"), Done, Total);
			}
		}

		void Collect()
		{
			Cleanup();

			TArray<TSharedPtr<FJsonValue>> Records;
			int32 Passed = 0;
			int32 Failed = 0;
			int32 Skipped = 0;

			ForEachLeaf([&](IAutomationReport& Report)
			{
				const EAutomationState State = Report.GetState(0, 0);
				if (State == EAutomationState::NotRun)
				{
					return;   // enabled, but the run stopped before reaching it
				}

				const FAutomationTestResults& Results = Report.GetResults(0, 0);

				const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
				Record->SetStringField(TEXT("name"), Report.GetFullTestPath());
				Record->SetStringField(TEXT("state"), StateName(State));
				Record->SetBoolField(TEXT("passed"), State == EAutomationState::Success);
				Record->SetNumberField(TEXT("durationMs"), Results.Duration * 1000.0);
				Record->SetNumberField(TEXT("errors"), Results.GetErrorTotal());
				Record->SetNumberField(TEXT("warnings"), Results.GetWarningTotal());

				TArray<TSharedPtr<FJsonValue>> Messages;
				for (const FAutomationExecutionEntry& Entry : Results.GetEntries())
				{
					if (Entry.Event.Type == EAutomationEventType::Error
						|| Entry.Event.Type == EAutomationEventType::Warning)
					{
						Messages.Add(MakeShared<FJsonValueString>(Entry.ToString()));
					}
				}
				Record->SetArrayField(TEXT("messages"), Messages);
				Records.Add(MakeShared<FJsonValueObject>(Record));

				switch (State)
				{
				case EAutomationState::Success: ++Passed; break;
				case EAutomationState::Skipped: ++Skipped; break;
				default:                        ++Failed; break;
				}
			});

			const TSharedRef<FJsonObject> ResultJson = MakeShared<FJsonObject>();
			ResultJson->SetStringField(TEXT("command"), Command);
			ResultJson->SetNumberField(TEXT("total"), Records.Num());
			ResultJson->SetNumberField(TEXT("passed"), Passed);
			ResultJson->SetNumberField(TEXT("failed"), Failed);
			ResultJson->SetNumberField(TEXT("skipped"), Skipped);
			ResultJson->SetArrayField(TEXT("tests"), Records);

			Succeed(ResultJson, FString::Printf(TEXT("%d passed, %d failed, %d skipped"), Passed, Failed, Skipped));
		}

		static const TCHAR* StateName(EAutomationState State)
		{
			switch (State)
			{
			case EAutomationState::Success:   return TEXT("passed");
			case EAutomationState::Fail:      return TEXT("failed");
			case EAutomationState::Skipped:   return TEXT("skipped");
			case EAutomationState::InProcess: return TEXT("running");
			default:                          return TEXT("notRun");
			}
		}

		IAutomationControllerManagerPtr Controller;
		FDelegateHandle CompleteHandle;
		FString Filter;
		FString Command;
		int32 TimeoutSeconds = 300;
		float Elapsed = 0.0f;
		bool bSawRunning = false;
		bool bComplete = false;
	};

	// ---------------------------------------------------------------- level.build

	// Runs an editor level build — geometry (BSP), navigation, or lighting —
	// through FEditorBuildUtils and polls the engine's in-progress flags for
	// completion. Errors/warnings emitted during the build window are pulled from
	// the log capture, same as pie.run. HLOD / "Build All" are deliberately not
	// offered: they run modal slow tasks that hang a headless editor.
	class FLevelBuildJob : public FUECliJob
	{
	public:
		using FUECliJob::FUECliJob;

		virtual void Start() override
		{
			What = StringParam(Params, TEXT("what")).ToLower();
			if (What.IsEmpty())
			{
				What = TEXT("geometry");
			}
			TimeoutSeconds = FMath::Max(30, IntParam(Params, TEXT("timeoutSeconds"), 900));

			FName BuildId;
			if (What == TEXT("geometry"))
			{
				BuildId = FBuildOptions::BuildGeometry;
			}
			else if (What == TEXT("paths") || What == TEXT("navigation"))
			{
				BuildId = FBuildOptions::BuildAIPaths;
				What = TEXT("paths");
			}
			else if (What == TEXT("lighting"))
			{
				BuildId = FBuildOptions::BuildLighting;
			}
			else
			{
				Fail(FString::Printf(TEXT("unknown build target '%s' (geometry | paths | lighting)"), *What));
				return;
			}

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World)
			{
				Fail(TEXT("no editor world"));
				return;
			}
			if (GEditor->IsLightingBuildCurrentlyRunning() || FEditorBuildUtils::IsBuildCurrentlyRunning())
			{
				Fail(TEXT("a build is already running"));
				return;
			}

			if (FUECliLogCapture* Capture = FUECliLogCapture::Active())
			{
				StartSeq = Capture->HighestSeq() + 1;
			}

			Message = FString::Printf(TEXT("building %s"), *What);
			FEditorBuildUtils::EditorBuild(World, BuildId, /*bAllowLightingDialog*/ false);
		}

		virtual void Tick(float DeltaSeconds) override
		{
			if (IsTerminal())
			{
				return;
			}
			Elapsed += DeltaSeconds;

			if (!GEditor)
			{
				Fail(TEXT("no editor"));
				return;
			}

			const bool bBusy =
				GEditor->IsLightingBuildCurrentlyRunning()
				|| FEditorBuildUtils::IsBuildCurrentlyRunning()
				|| NavBuilding();

			if (!bBusy)
			{
				// Let an async build a moment to actually flip "busy" true first.
				if (Elapsed >= 1.5f)
				{
					Collect();
				}
				return;
			}

			Progress = FMath::Clamp(Elapsed / TimeoutSeconds, 0.0f, 0.99f);
			if (Elapsed > TimeoutSeconds)
			{
				Fail(FString::Printf(TEXT("build '%s' timed out after %ds"), *What, TimeoutSeconds));
			}
		}

	private:
		bool NavBuilding() const
		{
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
			return Nav && Nav->IsNavigationBuildInProgress();
		}

		void Collect()
		{
			const TSharedRef<FJsonObject> Res = MakeShared<FJsonObject>();
			Res->SetStringField(TEXT("what"), What);
			Res->SetNumberField(TEXT("seconds"), Elapsed);

			TArray<TSharedPtr<FJsonValue>> Errors;
			int32 Warnings = 0;
			if (FUECliLogCapture* Capture = FUECliLogCapture::Active())
			{
				const TSharedRef<FJsonObject> Page = MakeShared<FJsonObject>();
				Capture->Read(StartSeq, 2000, ELogVerbosity::Warning, FString(), Page);
				const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
				if (Page->TryGetArrayField(TEXT("entries"), Entries))
				{
					for (const TSharedPtr<FJsonValue>& Entry : *Entries)
					{
						const TSharedPtr<FJsonObject>* Obj = nullptr;
						if (Entry->TryGetObject(Obj))
						{
							if ((*Obj)->GetStringField(TEXT("severity")) == TEXT("error"))
							{
								Errors.Add(Entry);
							}
							else
							{
								++Warnings;
							}
						}
					}
				}
			}
			Res->SetArrayField(TEXT("errors"), Errors);
			Res->SetNumberField(TEXT("warnings"), Warnings);
			Res->SetBoolField(TEXT("succeeded"), Errors.Num() == 0);

			Succeed(Res, FString::Printf(TEXT("%s built — %d error(s), %d warning(s)"), *What, Errors.Num(), Warnings));
		}

		FString What;
		int32 TimeoutSeconds = 900;
		float Elapsed = 0.0f;
		uint64 StartSeq = 1;
	};

	// ---------------------------------------------------------------- asset.import

	/**
	 * Imports { files: [...] } (or { file }) into { destination?, replaceExisting? },
	 * one file per tick: the editor stays responsive between files, progress is
	 * reported, and a long batch is not bound by the HTTP request timeout. Each
	 * file import itself still runs synchronously on the game thread (UE's
	 * importers are). Fails only if every file failed.
	 */
	class FAssetImportJob : public FUECliJob
	{
	public:
		using FUECliJob::FUECliJob;

		virtual void Start() override
		{
			const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
			if (Params->TryGetArrayField(TEXT("files"), Files))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Files)
				{
					Pending.Add(Value->AsString());
				}
			}
			const FString Single = StringParam(Params, TEXT("file"));
			if (!Single.IsEmpty())
			{
				Pending.Add(Single);
			}
			Pending.RemoveAll([](const FString& File) { return File.IsEmpty(); });
			if (Pending.Num() == 0)
			{
				Fail(TEXT("params.files (or params.file) is required"));
				return;
			}

			Destination = StringParam(Params, TEXT("destination"));
			Params->TryGetBoolField(TEXT("replaceExisting"), bReplaceExisting);
			Total = Pending.Num();
			Algo::Reverse(Pending); // Pop() from the back keeps the caller's order
			Progress = 0.0f;
			Message = FString::Printf(TEXT("0/%d"), Total);
		}

		virtual void Tick(float DeltaSeconds) override
		{
			if (IsTerminal())
			{
				return;
			}

			if (Pending.Num() == 0)
			{
				const TSharedRef<FJsonObject> Res = MakeShared<FJsonObject>();
				Res->SetNumberField(TEXT("total"), Total);
				Res->SetArrayField(TEXT("imported"), Imported);
				Res->SetArrayField(TEXT("failed"), Failed);
				if (Imported.Num() == 0 && Failed.Num() > 0)
				{
					Result = Res;
					Fail(FirstError);
					return;
				}
				Succeed(Res, FString::Printf(TEXT("%d imported, %d failed"), Imported.Num(), Failed.Num()));
				return;
			}

			const FString File = Pending.Pop(EAllowShrinking::No);
			TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
			FString Error;
			if (ProjectOps::ImportAsset(File, Destination, bReplaceExisting, One, Error))
			{
				const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
				if (One->TryGetArrayField(TEXT("imported"), Paths))
				{
					Imported.Append(*Paths);
				}
				const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
				Payload->SetStringField(TEXT("op"), TEXT("import"));
				Payload->SetStringField(TEXT("file"), File);
				UECli::Services::Events().Broadcast(TEXT("asset.changed"), Payload);
			}
			else
			{
				const TSharedRef<FJsonObject> Failure = MakeShared<FJsonObject>();
				Failure->SetStringField(TEXT("file"), File);
				Failure->SetStringField(TEXT("error"), Error);
				Failed.Add(MakeShared<FJsonValueObject>(Failure));
				if (FirstError.IsEmpty())
				{
					FirstError = Error;
				}
			}

			const int32 Done = Total - Pending.Num();
			Progress = static_cast<float>(Done) / Total;
			Message = FString::Printf(TEXT("%d/%d"), Done, Total);
		}

	private:
		TArray<FString> Pending;
		FString Destination;
		bool bReplaceExisting = false;
		int32 Total = 0;
		TArray<TSharedPtr<FJsonValue>> Imported;
		TArray<TSharedPtr<FJsonValue>> Failed;
		FString FirstError;
	};

	// ---------------------------------------------------------------- python.exec

	/**
	 * Runs { code } (multi-statement Python, `import unreal`) in the editor's
	 * interpreter on the next tick. Result: { succeeded, output (info lines),
	 * error (error lines / traceback), log: [{ type, text }] }. Fails when the
	 * script raised; the result is attached either way.
	 */
	class FPythonExecJob : public FUECliJob
	{
	public:
		using FUECliJob::FUECliJob;

		virtual void Start() override
		{
			Code = StringParam(Params, TEXT("code"));
			if (Code.IsEmpty())
			{
				Fail(TEXT("params.code is required"));
			}
		}

		virtual void Tick(float DeltaSeconds) override
		{
			if (IsTerminal())
			{
				return;
			}

			IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
			if (Python == nullptr || !Python->IsPythonAvailable())
			{
				Fail(TEXT("Python is not available in this editor (PythonScriptPlugin disabled or failed to initialize)"));
				return;
			}

			// print() through UE's stdout redirect costs ~11 ms a call (unreal.log: microseconds),
			// so a script that prints a few thousand lines would block the editor for tens of
			// seconds. Route stdout through a line-buffered writer for the duration of the script.
			auto RunHelper = [Python](const TCHAR* Source, TArray<FPythonLogOutputEntry>* Collect)
			{
				FPythonCommandEx Helper;
				Helper.Command = Source;
				Helper.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
				Helper.FileExecutionScope = EPythonFileExecutionScope::Private;
				Helper.Flags = EPythonCommandFlags::Unattended;
				Python->ExecPythonCommandEx(Helper);
				if (Collect)
				{
					Collect->Append(Helper.LogOutput);
				}
			};
			// A bare print() would be lost (UE's logger drops empty messages), so empty lines are sent as a
			// per-run marker that no script output can collide with.
			const FString BlankMarker = TEXT("__uecli_blank_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("__");
			const FString Setup = FString(TEXT(
				"import sys, unreal\n"
				"class _UECliStdout:\n"
				"    def __init__(self): self._buf = ''\n"
				"    def write(self, s):\n"
				"        self._buf += s\n"
				"        while '\\n' in self._buf:\n"
				"            line, self._buf = self._buf.split('\\n', 1)\n"
				"            unreal.log(line or '{BLANK}')\n"
				"        return len(s)\n"
				"    def flush(self): pass\n"
				"    def uecli_drain(self):\n"
				"        if self._buf: unreal.log(self._buf); self._buf = ''\n"
				"if not hasattr(sys, '_uecli_prev_stdout'):\n"
				"    sys._uecli_prev_stdout = sys.stdout\n"
				"    sys.stdout = _UECliStdout()\n")).Replace(TEXT("{BLANK}"), *BlankMarker);
			RunHelper(*Setup, nullptr);

			FPythonCommandEx Command;
			Command.Command = Code;
			Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
			Command.FileExecutionScope = EPythonFileExecutionScope::Private;
			Command.Flags = EPythonCommandFlags::Unattended;
			const bool bOk = Python->ExecPythonCommandEx(Command);

			// Restore stdout; a trailing partial line (write() without newline) is logged here.
			RunHelper(TEXT(
				"import sys\n"
				"if hasattr(sys, '_uecli_prev_stdout'):\n"
				"    if hasattr(sys.stdout, 'uecli_drain'): sys.stdout.uecli_drain()\n"
				"    sys.stdout = sys._uecli_prev_stdout\n"
				"    del sys._uecli_prev_stdout\n"), &Command.LogOutput);

			// Non-error output is capped so a runaway print() cannot balloon the job result;
			// error lines (the traceback) are always kept.
			constexpr int32 OutputBudget = 512 * 1024;
			int32 OutputUsed = 0;
			bool bTruncated = false;
			TArray<FString> Output;
			TArray<FString> Errors;
			TArray<TSharedPtr<FJsonValue>> Log;
			for (const FPythonLogOutputEntry& Entry : Command.LogOutput)
			{
				const TCHAR* Type = Entry.Type == EPythonLogOutputType::Error ? TEXT("error")
					: Entry.Type == EPythonLogOutputType::Warning ? TEXT("warning") : TEXT("info");
				FString Text = Entry.Output.TrimEnd(); // print() lines end in "\r\n"
				if (Text == BlankMarker)
				{
					Text.Reset();
				}
				if (Entry.Type != EPythonLogOutputType::Error)
				{
					if (bTruncated || OutputUsed + Text.Len() > OutputBudget)
					{
						bTruncated = true;
						continue;
					}
					OutputUsed += Text.Len();
				}
				(Entry.Type == EPythonLogOutputType::Error ? Errors : Output).Add(Text);
				const TSharedRef<FJsonObject> Line = MakeShared<FJsonObject>();
				Line->SetStringField(TEXT("type"), Type);
				Line->SetStringField(TEXT("text"), Text);
				Log.Add(MakeShared<FJsonValueObject>(Line));
			}
			FString Error = FString::Join(Errors, TEXT("\n"));
			if (!bOk && Error.IsEmpty())
			{
				Error = Command.CommandResult.IsEmpty() ? TEXT("Python script failed") : Command.CommandResult.Replace(TEXT("\n\n"), TEXT("\n")).TrimEnd();
			}

			const TSharedRef<FJsonObject> Res = MakeShared<FJsonObject>();
			Res->SetBoolField(TEXT("succeeded"), bOk);
			Res->SetStringField(TEXT("output"), FString::Join(Output, TEXT("\n")));
			if (!Error.IsEmpty())
			{
				Res->SetStringField(TEXT("error"), Error);
			}
			Res->SetArrayField(TEXT("log"), Log);
			if (bTruncated)
			{
				Res->SetBoolField(TEXT("truncated"), true);
			}

			if (bOk)
			{
				Succeed(Res, TEXT("ok"));
				return;
			}
			Result = Res;
			TArray<FString> Lines;
			Error.ParseIntoArrayLines(Lines);
			Fail(Lines.Num() > 0 ? Lines.Last() : Error);
		}

	private:
		FString Code;
	};

	// ----------------------------------------------------------------

	void RegisterBuiltins()
	{
		FUECliJobManager& Manager = UECli::Services::Jobs();

		Manager.RegisterKind(TEXT("blueprint.compile-all"),
			[](const TSharedPtr<FJsonObject>& Params, FString&) -> TSharedPtr<FUECliJob>
			{
				return MakeShared<FCompileAllJob>(TEXT("blueprint.compile-all"), Params);
			});

		Manager.RegisterKind(TEXT("pie.run"),
			[](const TSharedPtr<FJsonObject>& Params, FString&) -> TSharedPtr<FUECliJob>
			{
				return MakeShared<FPieRunJob>(TEXT("pie.run"), Params);
			});

		Manager.RegisterKind(TEXT("automation.run"),
			[](const TSharedPtr<FJsonObject>& Params, FString&) -> TSharedPtr<FUECliJob>
			{
				return MakeShared<FAutomationJob>(TEXT("automation.run"), Params);
			});

		Manager.RegisterKind(TEXT("asset.import"),
			[](const TSharedPtr<FJsonObject>& Params, FString&) -> TSharedPtr<FUECliJob>
			{
				return MakeShared<FAssetImportJob>(TEXT("asset.import"), Params);
			});

		Manager.RegisterKind(TEXT("level.build"),
			[](const TSharedPtr<FJsonObject>& Params, FString&) -> TSharedPtr<FUECliJob>
			{
				return MakeShared<FLevelBuildJob>(TEXT("level.build"), Params);
			});

		// python.exec runs arbitrary code; UECLI_DISABLE_PYTHON=1 in the editor's environment leaves it out
		// (capabilities then omit it and starting it is job.bad_kind).
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("UECLI_DISABLE_PYTHON")) != TEXT("1"))
		{
			Manager.RegisterKind(TEXT("python.exec"),
				[](const TSharedPtr<FJsonObject>& Params, FString&) -> TSharedPtr<FUECliJob>
				{
					return MakeShared<FPythonExecJob>(TEXT("python.exec"), Params);
				});
		}
		else
		{
			UE_LOG(LogUECli, Display, TEXT("python.exec disabled (UECLI_DISABLE_PYTHON=1)."));
		}
	}
}
