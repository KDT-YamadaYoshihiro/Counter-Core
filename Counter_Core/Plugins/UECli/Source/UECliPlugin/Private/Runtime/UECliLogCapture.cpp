// Copyright UE CLI. All rights reserved.

#include "Runtime/UECliLogCapture.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/App.h"

namespace
{
	const TCHAR* LogVerbosityName(ELogVerbosity::Type Verbosity)
	{
		switch (Verbosity & ELogVerbosity::VerbosityMask)
		{
		case ELogVerbosity::Fatal:       return TEXT("fatal");
		case ELogVerbosity::Error:       return TEXT("error");
		case ELogVerbosity::Warning:     return TEXT("warning");
		case ELogVerbosity::Display:     return TEXT("display");
		case ELogVerbosity::Log:         return TEXT("log");
		case ELogVerbosity::Verbose:     return TEXT("verbose");
		case ELogVerbosity::VeryVerbose: return TEXT("veryverbose");
		default:                         return TEXT("log");
		}
	}
}

FUECliLogCapture* FUECliLogCapture::Instance = nullptr;

FUECliLogCapture::FUECliLogCapture()
{
	Buffer.SetNum(Capacity);
	Instance = this;
	if (GLog)
	{
		GLog->AddOutputDevice(this);
		bRegistered = true;
	}
}

FUECliLogCapture::~FUECliLogCapture()
{
	if (bRegistered && GLog)
	{
		GLog->RemoveOutputDevice(this);
	}
	SetSink(nullptr);
	if (Instance == this)
	{
		Instance = nullptr;
	}
}

void FUECliLogCapture::SetSink(FSink InSink)
{
	FScopeLock ScopeLock(&SinkLock);
	Sink = MoveTemp(InSink);
}

void FUECliLogCapture::Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category)
{
	const ELogVerbosity::Type Masked = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
	if (Masked == ELogVerbosity::NoLogging || Masked == ELogVerbosity::SetColor)
	{
		return; // control markers, not real log lines
	}
	if (!Message || Message[0] == TEXT('\0'))
	{
		return;
	}

	const double Time = FPlatformTime::Seconds() - GStartTime;
	{
		// O(1) append into the ring (the oldest slot is overwritten once full).
		FScopeLock ScopeLock(&Lock);
		int32 Slot;
		if (Count < Capacity)
		{
			Slot = (Head + Count) % Capacity;
			++Count;
		}
		else
		{
			Slot = Head;
			Head = (Head + 1) % Capacity;
		}

		FEntry& Entry = Buffer[Slot];
		Entry.Seq = NextSeq++;
		Entry.Time = Time;
		Entry.Verbosity = Masked;
		Entry.Category = Category;
		Entry.Message = Message;
	}

	// Outside the buffer lock: logging threads do not queue behind the sink.
	FScopeLock SinkScope(&SinkLock);
	if (Sink)
	{
		Sink(Time, Masked, Category, FString(Message));
	}
}

uint64 FUECliLogCapture::HighestSeq() const
{
	FScopeLock ScopeLock(&Lock);
	return NextSeq > 0 ? NextSeq - 1 : 0;
}

void FUECliLogCapture::Read(uint64 Since, int32 Limit, ELogVerbosity::Type MinVerbosity, const FString& CategoryFilter,
	const TSharedRef<FJsonObject>& OutResult) const
{
	if (Limit <= 0)
	{
		Limit = 200;
	}

	TArray<TSharedPtr<FJsonValue>> Entries;
	uint64 HighestSeq = 0;

	{
		FScopeLock ScopeLock(&Lock);
		HighestSeq = NextSeq > 0 ? NextSeq - 1 : 0;

		for (int32 Offset = 0; Offset < Count; ++Offset)
		{
			const FEntry& Entry = Buffer[(Head + Offset) % Capacity];
			if (Entry.Seq < Since)
			{
				continue;
			}
			if ((Entry.Verbosity & ELogVerbosity::VerbosityMask) > MinVerbosity)
			{
				continue; // numerically larger == less severe
			}
			if (!CategoryFilter.IsEmpty() && !Entry.Category.ToString().Contains(CategoryFilter))
			{
				continue;
			}

			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetNumberField(TEXT("seq"), static_cast<double>(Entry.Seq));
			Json->SetNumberField(TEXT("time"), Entry.Time);
			Json->SetStringField(TEXT("severity"), LogVerbosityName(Entry.Verbosity));
			Json->SetStringField(TEXT("category"), Entry.Category.ToString());
			Json->SetStringField(TEXT("message"), Entry.Message);
			Entries.Add(MakeShared<FJsonValueObject>(Json));
		}
	}

	// Keep only the most recent `Limit`.
	if (Entries.Num() > Limit)
	{
		Entries.RemoveAt(0, Entries.Num() - Limit, EAllowShrinking::No);
	}

	OutResult->SetArrayField(TEXT("entries"), Entries);
	OutResult->SetNumberField(TEXT("nextSince"), static_cast<double>(HighestSeq + 1));
}
