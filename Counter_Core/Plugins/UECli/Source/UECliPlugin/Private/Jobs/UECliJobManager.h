// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class FJsonObject;
class FUECliJob;

/**
 * Owns and ticks async jobs. Jobs are created by kind through a factory table;
 * the manager ticks every non-terminal job each frame and broadcasts
 * job.started / job.progress / job.finished on the event channel.
 */
class FUECliJobManager
{
public:
	static FUECliJobManager& Get();

	void Init();
	void Shutdown();

	using FJobFactory = TFunction<TSharedPtr<FUECliJob>(const TSharedPtr<FJsonObject>& Params, FString& OutError)>;
	void RegisterKind(const FString& Kind, FJobFactory Factory);

	/** Create and queue a job. Returns the new job, or null with OutError. */
	TSharedPtr<FUECliJob> Enqueue(const FString& Kind, const TSharedPtr<FJsonObject>& Params, FString& OutError);

	TSharedPtr<FUECliJob> Find(const FGuid& Id) const;
	TArray<TSharedPtr<FUECliJob>> List(int32 Limit) const;
	bool Cancel(const FGuid& Id);

	TArray<FString> KnownKinds() const;

private:
	bool Tick(float DeltaSeconds);

	TMap<FString, FJobFactory> Factories;
	TMap<FGuid, TSharedPtr<FUECliJob>> Jobs;
	TArray<FGuid> Order;                 // newest last
	TSet<FGuid> Announced;               // job.started already sent
	TMap<FGuid, float> LastProgress;

	FTSTicker::FDelegateHandle TickerHandle;
	static constexpr int32 MaxRetained = 100;
};
