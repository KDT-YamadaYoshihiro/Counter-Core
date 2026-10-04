// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Lifecycle of an async job. */
enum class EUECliJobStatus : uint8
{
	Queued,
	Running,
	Succeeded,
	Failed,
	Cancelled,
};

const TCHAR* LexToString(EUECliJobStatus Status);

/**
 * A long-running editor operation the agent starts, polls and reacts to — the
 * v3 transport layer. Jobs are ticked on the game thread by FUECliJobManager;
 * a job advances its own small state machine and sets a terminal status when
 * done. See doc/transport-roadmap.md.
 */
class FUECliJob : public TSharedFromThis<FUECliJob>
{
public:
	explicit FUECliJob(const FString& InKind, const TSharedPtr<FJsonObject>& InParams)
		: Kind(InKind)
		, Params(InParams.IsValid() ? InParams.ToSharedRef() : MakeShared<FJsonObject>())
	{
		Id = FGuid::NewGuid();
	}

	virtual ~FUECliJob() = default;

	/** Called once, on the tick after the job is enqueued. */
	virtual void Start() {}

	/** Advance the job. Set Status to a terminal value when finished. */
	virtual void Tick(float DeltaSeconds) = 0;

	/** Best-effort stop. Default flips to Cancelled; override to clean up. */
	virtual void Cancel() { Finish(EUECliJobStatus::Cancelled, TEXT("cancelled")); }

	bool IsTerminal() const
	{
		return Status == EUECliJobStatus::Succeeded
			|| Status == EUECliJobStatus::Failed
			|| Status == EUECliJobStatus::Cancelled;
	}

	TSharedRef<FJsonObject> ToJson() const;

	FGuid Id;
	FString Kind;
	TSharedRef<FJsonObject> Params;

	EUECliJobStatus Status = EUECliJobStatus::Queued;
	FString Message;
	float Progress = -1.0f;   // 0..1, or <0 for indeterminate
	FDateTime StartedAt;
	FDateTime FinishedAt;
	TSharedPtr<FJsonObject> Result;

	/** Invoked exactly once, when the job first reaches a terminal status. */
	TFunction<void(FUECliJob&)> OnFinished;

protected:
	void Finish(EUECliJobStatus InStatus, const FString& InMessage)
	{
		if (!IsTerminal())
		{
			Status = InStatus;
			Message = InMessage;
			FinishedAt = FDateTime::UtcNow();
			Progress = InStatus == EUECliJobStatus::Succeeded ? 1.0f : Progress;
			if (OnFinished)
			{
				OnFinished(*this);
			}
		}
	}

	void Succeed(const TSharedRef<FJsonObject>& InResult, const FString& InMessage = TEXT("done"))
	{
		Result = InResult;
		Finish(EUECliJobStatus::Succeeded, InMessage);
	}

	void Fail(const FString& InMessage)
	{
		Finish(EUECliJobStatus::Failed, InMessage);
	}
};
