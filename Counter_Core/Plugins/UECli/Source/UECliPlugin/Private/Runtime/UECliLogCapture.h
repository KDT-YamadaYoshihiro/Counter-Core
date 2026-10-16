// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"
#include "HAL/CriticalSection.h"

class FJsonObject;

/**
 * Ring buffer of recent engine log lines so an AI agent can poll for what the
 * editor / PIE session printed. Registered with GLog for the plugin's lifetime.
 * Each line gets a monotonic sequence number for incremental polling.
 */
class FUECliLogCapture : public FOutputDevice
{
public:
	/** Live capture instance, if the plugin has one. */
	static FUECliLogCapture* Active() { return Instance; }

	/** Called for every captured line (game or any thread). Set to null to clear. */
	using FSink = TFunction<void(double Time, ELogVerbosity::Type Verbosity, FName Category, const FString& Message)>;
	void SetSink(FSink InSink);

	FUECliLogCapture();
	virtual ~FUECliLogCapture() override;

	//~ FOutputDevice
	virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
	virtual bool CanBeUsedOnPanicThread() const override { return true; }
	//~ End FOutputDevice

	/**
	 * Entries with Seq >= Since (0 = from the oldest retained), newest last,
	 * capped at Limit, filtered to MinVerbosity or noisier and optionally a
	 * category substring. OutResult = { entries[], nextSince }.
	 */
	void Read(uint64 Since, int32 Limit, ELogVerbosity::Type MinVerbosity, const FString& CategoryFilter,
		const TSharedRef<FJsonObject>& OutResult) const;

	/** Highest sequence number assigned so far (0 if nothing captured yet). */
	uint64 HighestSeq() const;

private:
	struct FEntry
	{
		uint64 Seq = 0;
		double Time = 0.0;
		ELogVerbosity::Type Verbosity = ELogVerbosity::Log;
		FName Category;
		FString Message;
	};

	static constexpr int32 Capacity = 2000;
	static FUECliLogCapture* Instance;

	mutable FCriticalSection Lock;
	TArray<FEntry> Buffer;   // fixed Capacity slots used as a ring: oldest at Head, Count in use
	int32 Head = 0;
	int32 Count = 0;
	uint64 NextSeq = 1;
	bool bRegistered = false;

	FCriticalSection SinkLock;
	FSink Sink;
};
