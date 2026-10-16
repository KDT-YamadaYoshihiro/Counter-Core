// Copyright UE CLI. All rights reserved.

#include "Jobs/UECliJobManager.h"
#include "UECliServices.h"

#include "Dom/JsonObject.h"
#include "Events/UECliEventHub.h"
#include "Jobs/UECliJob.h"
#include "UECliLog.h"

const TCHAR* LexToString(EUECliJobStatus Status)
{
	switch (Status)
	{
	case EUECliJobStatus::Queued:    return TEXT("queued");
	case EUECliJobStatus::Running:   return TEXT("running");
	case EUECliJobStatus::Succeeded: return TEXT("succeeded");
	case EUECliJobStatus::Failed:    return TEXT("failed");
	case EUECliJobStatus::Cancelled: return TEXT("cancelled");
	default:                         return TEXT("unknown");
	}
}

TSharedRef<FJsonObject> FUECliJob::ToJson() const
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("jobId"), Id.ToString(EGuidFormats::DigitsWithHyphens));
	Json->SetStringField(TEXT("kind"), Kind);
	Json->SetStringField(TEXT("status"), LexToString(Status));
	if (!Message.IsEmpty())
	{
		Json->SetStringField(TEXT("message"), Message);
	}
	if (Progress >= 0.0f)
	{
		Json->SetNumberField(TEXT("progress"), Progress);
	}
	if (StartedAt != FDateTime()) // a job cancelled while queued never started
	{
		Json->SetStringField(TEXT("startedAt"), StartedAt.ToIso8601());
	}
	if (IsTerminal())
	{
		Json->SetStringField(TEXT("finishedAt"), FinishedAt.ToIso8601());
	}
	if (Result.IsValid())
	{
		Json->SetObjectField(TEXT("result"), Result);
	}
	return Json;
}

// ---------------------------------------------------------------------------

FUECliJobManager& FUECliJobManager::Get()
{
	static FUECliJobManager Instance;
	return Instance;
}

void FUECliJobManager::Init()
{
	if (!TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FUECliJobManager::Tick), 0.0f);
	}
}

void FUECliJobManager::Shutdown()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	for (const TPair<FGuid, TSharedPtr<FUECliJob>>& Pair : Jobs)
	{
		if (Pair.Value.IsValid() && !Pair.Value->IsTerminal())
		{
			Pair.Value->Cancel();
		}
	}
	Jobs.Reset();
	Order.Reset();
	Announced.Reset();
	LastProgress.Reset();
}

void FUECliJobManager::RegisterKind(const FString& Kind, FJobFactory Factory)
{
	Factories.Add(Kind, MoveTemp(Factory));
}

TSharedPtr<FUECliJob> FUECliJobManager::Enqueue(const FString& Kind, const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	const FJobFactory* Factory = Factories.Find(Kind);
	if (!Factory)
	{
		OutError = FString::Printf(TEXT("Unknown job kind '%s'."), *Kind);
		return nullptr;
	}

	TSharedPtr<FUECliJob> Job = (*Factory)(Params, OutError);
	if (!Job.IsValid())
	{
		return nullptr;
	}

	Job->OnFinished = [](FUECliJob& Finished)
	{
		UECli::Services::Events().Broadcast(TEXT("job.finished"), Finished.ToJson());
		UE_LOG(LogUECli, Display, TEXT("Job %s (%s) %s"), *Finished.Id.ToString(EGuidFormats::DigitsWithHyphens), *Finished.Kind, LexToString(Finished.Status));
	};

	Jobs.Add(Job->Id, Job);
	Order.Add(Job->Id);

	// Trim the oldest terminal jobs; queued/running jobs are never evicted.
	for (int32 Index = 0; Index < Order.Num() && Order.Num() > MaxRetained; )
	{
		const FGuid Old = Order[Index];
		const TSharedPtr<FUECliJob> OldJob = Jobs.FindRef(Old);
		if (OldJob.IsValid() && !OldJob->IsTerminal())
		{
			++Index;
			continue;
		}
		Order.RemoveAt(Index);
		Jobs.Remove(Old);
		Announced.Remove(Old);
		LastProgress.Remove(Old);
	}

	return Job;
}

TSharedPtr<FUECliJob> FUECliJobManager::Find(const FGuid& Id) const
{
	return Jobs.FindRef(Id);
}

TArray<TSharedPtr<FUECliJob>> FUECliJobManager::List(int32 Limit) const
{
	TArray<TSharedPtr<FUECliJob>> Result;
	for (int32 Index = Order.Num() - 1; Index >= 0 && Result.Num() < Limit; --Index)
	{
		if (const TSharedPtr<FUECliJob> Job = Jobs.FindRef(Order[Index]))
		{
			Result.Add(Job);
		}
	}
	return Result;
}

bool FUECliJobManager::Cancel(const FGuid& Id)
{
	const TSharedPtr<FUECliJob> Job = Jobs.FindRef(Id);
	if (!Job.IsValid() || Job->IsTerminal())
	{
		return false;
	}
	Job->Cancel();
	return true;
}

TArray<FString> FUECliJobManager::KnownKinds() const
{
	TArray<FString> Kinds;
	Factories.GetKeys(Kinds);
	Kinds.Sort();
	return Kinds;
}

bool FUECliJobManager::Tick(float DeltaSeconds)
{
	// Iterate a copy: a job's Start/Tick or a job.* event handler may Enqueue another job,
	// which appends to (and trims) Order mid-loop.
	const TArray<FGuid> Snapshot = Order;
	for (const FGuid& Id : Snapshot)
	{
		const TSharedPtr<FUECliJob> Job = Jobs.FindRef(Id);
		if (!Job.IsValid() || Job->IsTerminal())
		{
			continue;
		}

		if (Job->Status == EUECliJobStatus::Queued)
		{
			Job->Status = EUECliJobStatus::Running;
			Job->StartedAt = FDateTime::UtcNow();
			Job->Start();

			const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("jobId"), Id.ToString(EGuidFormats::DigitsWithHyphens));
			Payload->SetStringField(TEXT("kind"), Job->Kind);
			UECli::Services::Events().Broadcast(TEXT("job.started"), Payload);
			Announced.Add(Id);
		}

		Job->Tick(DeltaSeconds);

		const float* Previous = LastProgress.Find(Id);
		if (!Job->IsTerminal() && Job->Progress >= 0.0f && (!Previous || !FMath::IsNearlyEqual(*Previous, Job->Progress)))
		{
			LastProgress.Add(Id, Job->Progress);
			const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("jobId"), Id.ToString(EGuidFormats::DigitsWithHyphens));
			Payload->SetNumberField(TEXT("progress"), Job->Progress);
			if (!Job->Message.IsEmpty())
			{
				Payload->SetStringField(TEXT("message"), Job->Message);
			}
			UECli::Services::Events().Broadcast(TEXT("job.progress"), Payload);
		}
	}

	return true;
}
