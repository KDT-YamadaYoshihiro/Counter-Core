// Copyright UE CLI. All rights reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Jobs/UECliJob.h"
#include "Jobs/UECliJobManager.h"
#include "UECliServices.h"

namespace
{
	class FUECliIdleJob final : public FUECliJob
	{
	public:
		using FUECliJob::FUECliJob;
		virtual void Tick(float DeltaSeconds) override {}
		void ForceFail() { Fail(TEXT("forced")); }
	};

	const TCHAR* IdleKind = TEXT("uecli.selftest.idle");

	/** Records events instead of sending them to WebSocket clients. */
	class FRecordingSink final : public IUECliEventSink
	{
	public:
		virtual void Broadcast(const FString& Type, const TSharedRef<FJsonObject>& Payload) override
		{
			Events.Add({ Type, Payload });
		}

		int32 Count(const FString& Type) const
		{
			return Events.FilterByPredicate([&Type](const TPair<FString, TSharedRef<FJsonObject>>& E) { return E.Key == Type; }).Num();
		}

		TArray<TPair<FString, TSharedRef<FJsonObject>>> Events;
	};

	/** A private manager + sink, so tests never touch the editor's real job list. */
	struct FIsolatedJobs
	{
		FUECliJobManager Manager;
		FRecordingSink Sink;
		UECli::Services::FScopedJobManager JobsScope{ Manager };
		UECli::Services::FScopedEventSink EventsScope{ Sink };

		FIsolatedJobs()
		{
			Manager.RegisterKind(IdleKind, [](const TSharedPtr<FJsonObject>& Params, FString&)
			{
				return MakeShared<FUECliIdleJob>(IdleKind, Params);
			});
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUECliJobFinishOnce, "UECli.SelfTest.Jobs.FinishedFiresOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUECliJobFinishOnce::RunTest(const FString& Parameters)
{
	const TSharedRef<FUECliIdleJob> Job = MakeShared<FUECliIdleJob>(IdleKind, nullptr);
	int32 Calls = 0;
	Job->OnFinished = [&Calls](FUECliJob&) { ++Calls; };

	Job->Cancel();
	Job->Cancel();
	Job->ForceFail();

	TestEqual(TEXT("OnFinished fires exactly once"), Calls, 1);
	TestEqual(TEXT("first terminal status wins"), Job->Status, EUECliJobStatus::Cancelled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUECliJobManagerCancelFinishes, "UECli.SelfTest.Jobs.ManagerCancelFinishes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUECliJobManagerCancelFinishes::RunTest(const FString& Parameters)
{
	FIsolatedJobs Isolated;
	FString Error;
	const TSharedPtr<FUECliJob> Job = Isolated.Manager.Enqueue(IdleKind, nullptr, Error);
	if (!TestTrue(TEXT("enqueued"), Job.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("cancel accepted"), Isolated.Manager.Cancel(Job->Id));
	TestFalse(TEXT("second cancel refused"), Isolated.Manager.Cancel(Job->Id));
	TestTrue(TEXT("job terminal after cancel"), Job->IsTerminal());
	TestEqual(TEXT("job.finished broadcast exactly once"), Isolated.Sink.Count(TEXT("job.finished")), 1);
	TestFalse(TEXT("never-started job has no startedAt"), Job->ToJson()->HasField(TEXT("startedAt")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUECliJobManagerTrim, "UECli.SelfTest.Jobs.TrimSkipsRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUECliJobManagerTrim::RunTest(const FString& Parameters)
{
	FIsolatedJobs Isolated;
	FUECliJobManager& Manager = Isolated.Manager;
	FString Error;

	const TSharedPtr<FUECliJob> LongRunning = Manager.Enqueue(IdleKind, nullptr, Error);
	TSharedPtr<FUECliJob> Oldest;
	for (int32 Index = 0; Index < 150; ++Index)
	{
		const TSharedPtr<FUECliJob> Job = Manager.Enqueue(IdleKind, nullptr, Error);
		Manager.Cancel(Job->Id);
		if (!Oldest.IsValid())
		{
			Oldest = Job;
		}
	}

	TestTrue(TEXT("unfinished job is retained"), Manager.Find(LongRunning->Id).IsValid());
	TestFalse(TEXT("oldest finished job is evicted"), Manager.Find(Oldest->Id).IsValid());
	TestEqual(TEXT("retention cap holds"), Manager.List(1000).Num(), 100);
	TestEqual(TEXT("one job.finished per cancelled job"), Isolated.Sink.Count(TEXT("job.finished")), 150);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
