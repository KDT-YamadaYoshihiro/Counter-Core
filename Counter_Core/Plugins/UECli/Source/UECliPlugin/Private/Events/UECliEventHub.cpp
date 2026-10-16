// Copyright UE CLI. All rights reserved.

#include "Events/UECliEventHub.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "INetworkingWebSocket.h"
#include "IWebSocketNetworkingModule.h"
#include "IWebSocketServer.h"
#include "Runtime/UECliLogCapture.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UECliLog.h"
#include "UECliProtocol.h"
#include "Http/UECliHttpTypes.h"

using UECli::Http::TokenEquals;

namespace
{
	FString SerializeObject(const TSharedRef<FJsonObject>& Object)
	{
		FString Output;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Object, Writer);
		return Output;
	}

	ELogVerbosity::Type ParseSeverity(const FString& Text)
	{
		if (Text.Equals(TEXT("error"), ESearchCase::IgnoreCase))   return ELogVerbosity::Error;
		if (Text.Equals(TEXT("warning"), ESearchCase::IgnoreCase)) return ELogVerbosity::Warning;
		if (Text.Equals(TEXT("display"), ESearchCase::IgnoreCase)) return ELogVerbosity::Display;
		if (Text.Equals(TEXT("log"), ESearchCase::IgnoreCase))     return ELogVerbosity::Log;
		if (Text.Equals(TEXT("verbose"), ESearchCase::IgnoreCase)) return ELogVerbosity::Verbose;
		return ELogVerbosity::Warning;
	}

	const TCHAR* EventVerbosityName(ELogVerbosity::Type Verbosity)
	{
		switch (Verbosity & ELogVerbosity::VerbosityMask)
		{
		case ELogVerbosity::Fatal:   return TEXT("fatal");
		case ELogVerbosity::Error:   return TEXT("error");
		case ELogVerbosity::Warning: return TEXT("warning");
		case ELogVerbosity::Display: return TEXT("display");
		case ELogVerbosity::Verbose: return TEXT("verbose");
		default:                     return TEXT("log");
		}
	}
}

FUECliEventHub& FUECliEventHub::Get()
{
	static FUECliEventHub Instance;
	return Instance;
}

void FUECliEventHub::Init(uint32 Port, const FString& Token)
{
	if (bRunning)
	{
		return;
	}

	IWebSocketNetworkingModule* Module = FModuleManager::Get().LoadModulePtr<IWebSocketNetworkingModule>(TEXT("WebSocketNetworking"));
	if (!Module)
	{
		UE_LOG(LogUECli, Warning, TEXT("UE CLI event channel disabled: WebSocketNetworking module not available."));
		return;
	}

	RequiredToken = Token;
	SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Server = Module->CreateServer();

	FWebSocketClientConnectedCallBack ConnectedCallback;
	ConnectedCallback.BindRaw(this, &FUECliEventHub::OnClientConnected);
	if (!Server || !Server->Init(Port, ConnectedCallback, TEXT("127.0.0.1")))
	{
		UE_LOG(LogUECli, Error, TEXT("UE CLI event channel failed to bind ws port %u."), Port);
		Server.Reset();
		return;
	}

	// Refuse at the handshake (the only point the WS API can drop a client):
	// browsers always send Origin, the CLI/MCP clients never do.
	FWebSocketFilterConnectionCallback Filter;
	Filter.BindLambda([](FString Origin, FString /*ClientIP*/)
	{
		return Origin.IsEmpty() ? EWebsocketConnectionFilterResult::ConnectionAccepted
		                        : EWebsocketConnectionFilterResult::ConnectionRefused;
	});
	Server->SetFilterConnectionCallback(MoveTemp(Filter));

	if (FUECliLogCapture* Capture = FUECliLogCapture::Active())
	{
		Capture->SetSink([this](double Time, ELogVerbosity::Type Verbosity, FName Category, const FString& Message)
		{
			OnLogLine(Time, Verbosity, Category, Message);
		});
	}

	HookEditorDelegates();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FUECliEventHub::Tick), 0.0f);
	bRunning = true;
	UE_LOG(LogUECli, Display, TEXT("UE CLI event channel on ws://127.0.0.1:%u/ (protocol v%d)."), Port, UECLI_PROTOCOL_VERSION);
}

void FUECliEventHub::Shutdown()
{
	if (!bRunning)
	{
		return;
	}

	UnhookEditorDelegates();
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (FUECliLogCapture* Capture = FUECliLogCapture::Active())
	{
		Capture->SetSink(nullptr);
	}

	Clients.Reset();
	Server.Reset();
	LogQueue.Empty();
	History.Reset();
	EvictedThrough = 0;
	bRunning = false;
}

bool FUECliEventHub::Tick(float /*DeltaTime*/)
{
	if (Server)
	{
		Server->Tick();
	}

	// Flush queued log lines (enqueued from arbitrary threads) to subscribers.
	FQueuedLog Line;
	while (LogQueue.Dequeue(Line))
	{
		bool bAnyWants = false;
		for (const FClient& Client : Clients)
		{
			if (Client.bAuthed && Client.bWantsLog && Line.Verbosity <= Client.LogSeverity)
			{
				bAnyWants = true;
				break;
			}
		}
		if (!bAnyWants)
		{
			continue;
		}

		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("severity"), EventVerbosityName(Line.Verbosity));
		Payload->SetStringField(TEXT("category"), Line.Category);
		Payload->SetStringField(TEXT("message"), Line.Message);

		const TSharedRef<FJsonObject> Frame = MakeShared<FJsonObject>();
		Frame->SetStringField(TEXT("type"), TEXT("log"));
		Frame->SetNumberField(TEXT("seq"), static_cast<double>(NextSeq++));
		Frame->SetNumberField(TEXT("time"), Line.Time);
		Frame->SetObjectField(TEXT("payload"), Payload);
		const FString Text = SerializeObject(Frame);

		for (const FClient& Client : Clients)
		{
			if (Client.bAuthed && Client.bWantsLog && Line.Verbosity <= Client.LogSeverity)
			{
				SendTo(Client.Socket, Text);
			}
		}
	}

	return true;
}

void FUECliEventHub::Broadcast(const FString& Type)
{
	Broadcast(Type, MakeShared<FJsonObject>());
}

void FUECliEventHub::Broadcast(const FString& Type, const TSharedRef<FJsonObject>& Payload)
{
	if (!bRunning)
	{
		return;
	}

	const uint64 Seq = NextSeq++;
	const TSharedRef<FJsonObject> Frame = MakeShared<FJsonObject>();
	Frame->SetStringField(TEXT("type"), Type);
	Frame->SetNumberField(TEXT("seq"), static_cast<double>(Seq));
	Frame->SetNumberField(TEXT("time"), FPlatformTime::Seconds() - GStartTime);
	Frame->SetObjectField(TEXT("payload"), Payload);
	const FString Text = SerializeObject(Frame);

	// Kept even with no client connected, so a reconnecting client can resume.
	History.Add({ Seq, Type, Text });
	if (History.Num() > MaxHistory)
	{
		EvictedThrough = History[0].Seq;
		History.RemoveAt(0, History.Num() - MaxHistory, EAllowShrinking::No);
	}

	for (const FClient& Client : Clients)
	{
		if (Client.bAuthed && TopicMatches(Client.Topics, Type))
		{
			SendTo(Client.Socket, Text);
		}
	}
}

void FUECliEventHub::OnClientConnected(INetworkingWebSocket* Socket)
{
	if (!Socket)
	{
		return;
	}

	// Unauthenticated clients are tracked but receive nothing until they send
	// {"token": "..."} as their first message.
	FClient& Client = Clients.AddDefaulted_GetRef();
	Client.Socket = Socket;
	Client.Topics.Add(TEXT("*"));
	Client.bAuthed = RequiredToken.IsEmpty();

	FWebSocketPacketReceivedCallBack ReceiveCallback;
	ReceiveCallback.BindRaw(this, &FUECliEventHub::OnRawMessage, Socket);
	Socket->SetReceiveCallBack(ReceiveCallback);

	FWebSocketInfoCallBack ClosedCallback;
	ClosedCallback.BindRaw(this, &FUECliEventHub::OnClientClosed, Socket);
	Socket->SetSocketClosedCallBack(ClosedCallback);

	if (Client.bAuthed)
	{
		SendHello(Socket);
	}
}

void FUECliEventHub::SendHello(INetworkingWebSocket* Socket)
{
	const TSharedRef<FJsonObject> Hello = MakeShared<FJsonObject>();
	Hello->SetNumberField(TEXT("protocolVersion"), UECLI_PROTOCOL_VERSION);
	Hello->SetStringField(TEXT("sessionId"), SessionId);

	const TSharedRef<FJsonObject> Frame = MakeShared<FJsonObject>();
	Frame->SetStringField(TEXT("type"), TEXT("hello"));
	Frame->SetNumberField(TEXT("seq"), static_cast<double>(NextSeq++));
	Frame->SetObjectField(TEXT("payload"), Hello);
	SendTo(Socket, SerializeObject(Frame));
}

void FUECliEventHub::OnClientClosed(INetworkingWebSocket* Socket)
{
	Clients.RemoveAll([Socket](const FClient& Client) { return Client.Socket == Socket; });
}

void FUECliEventHub::OnRawMessage(void* Data, int32 Size, INetworkingWebSocket* Socket)
{
	FClient* Client = FindClient(Socket);
	if (!Client || !Data || Size <= 0)
	{
		return;
	}

	const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Data), Size);
	const FString Message(Converter.Length(), Converter.Get());

	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return;
	}

	if (!Client->bAuthed)
	{
		FString Token;
		if (!Json->TryGetStringField(TEXT("token"), Token) || !TokenEquals(Token, RequiredToken))
		{
			UE_LOG(LogUECli, Verbose, TEXT("UE CLI event channel: ignoring a client with a bad token."));
			return;
		}
		Client->bAuthed = true;
		SendHello(Socket);
	}

	const TArray<TSharedPtr<FJsonValue>>* Topics = nullptr;
	if (Json->TryGetArrayField(TEXT("topics"), Topics))
	{
		Client->Topics.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *Topics)
		{
			Client->Topics.Add(Value->AsString());
		}
	}

	FString LogSeverity;
	if (Json->TryGetStringField(TEXT("logSeverity"), LogSeverity))
	{
		Client->bWantsLog = true;
		Client->LogSeverity = ParseSeverity(LogSeverity);
	}

	// {"resumeAfter": <seq>, "sessionId": "..."}: replay retained events newer
	// than seq (log lines are not retained). A different sessionId means the
	// editor restarted and the client's seq is meaningless here.
	double ResumeAfter = 0;
	if (Json->TryGetNumberField(TEXT("resumeAfter"), ResumeAfter))
	{
		FString ClientSession;
		Json->TryGetStringField(TEXT("sessionId"), ClientSession);
		// Without a sessionId the seq cannot be trusted (it may come from an earlier editor run).
		const bool bSameSession = !ClientSession.IsEmpty() && ClientSession == SessionId;
		const uint64 After = bSameSession ? static_cast<uint64>(ResumeAfter) : 0;

		if (!bSameSession || After < EvictedThrough)
		{
			const TSharedRef<FJsonObject> Gap = MakeShared<FJsonObject>();
			Gap->SetNumberField(TEXT("resumeAfter"), ResumeAfter);
			Gap->SetNumberField(TEXT("oldestAvailable"), History.Num() > 0 ? static_cast<double>(History[0].Seq) : static_cast<double>(NextSeq));
			Gap->SetStringField(TEXT("reason"), bSameSession ? TEXT("evicted")
				: ClientSession.IsEmpty() ? TEXT("unknown_session") : TEXT("editor_restarted"));
			const TSharedRef<FJsonObject> Frame = MakeShared<FJsonObject>();
			Frame->SetStringField(TEXT("type"), TEXT("resume.gap"));
			Frame->SetNumberField(TEXT("seq"), static_cast<double>(NextSeq++));
			Frame->SetObjectField(TEXT("payload"), Gap);
			SendTo(Socket, SerializeObject(Frame));
		}

		for (const FHistoryEntry& Entry : History)
		{
			if (Entry.Seq > After && TopicMatches(Client->Topics, Entry.Type))
			{
				SendTo(Socket, Entry.Text);
			}
		}
	}
}

FUECliEventHub::FClient* FUECliEventHub::FindClient(INetworkingWebSocket* Socket)
{
	return Clients.FindByPredicate([Socket](const FClient& Client) { return Client.Socket == Socket; });
}

void FUECliEventHub::SendTo(INetworkingWebSocket* Socket, const FString& Text)
{
	if (!Socket)
	{
		return;
	}
	const FTCHARToUTF8 Utf8(*Text);
	Socket->Send(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), /*bPrependSize*/ false);
}

bool FUECliEventHub::TopicMatches(const TSet<FString>& Topics, const FString& Type)
{
	if (Topics.Contains(TEXT("*")))
	{
		return true;
	}
	for (const FString& Topic : Topics)
	{
		if (Type == Topic || Type.StartsWith(Topic + TEXT(".")))
		{
			return true;
		}
	}
	return false;
}

void FUECliEventHub::OnLogLine(double Time, ELogVerbosity::Type Verbosity, FName Category, const FString& Message)
{
	if (bRunning)
	{
		LogQueue.Enqueue(FQueuedLog{
			Time,
			static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask),
			Category.ToString(),
			Message });
	}
}

void FUECliEventHub::HookEditorDelegates()
{
	PostPieStartedHandle = FEditorDelegates::PostPIEStarted.AddLambda([this](const bool bSimulating)
	{
		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetBoolField(TEXT("simulating"), bSimulating);
		Broadcast(TEXT("pie.started"), Payload);
	});
	EndPieHandle = FEditorDelegates::EndPIE.AddLambda([this](const bool bSimulating)
	{
		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetBoolField(TEXT("simulating"), bSimulating);
		Broadcast(TEXT("pie.stopped"), Payload);
	});
	PausePieHandle = FEditorDelegates::PausePIE.AddLambda([this](const bool) { Broadcast(TEXT("pie.paused")); });
	ResumePieHandle = FEditorDelegates::ResumePIE.AddLambda([this](const bool) { Broadcast(TEXT("pie.resumed")); });
}

void FUECliEventHub::UnhookEditorDelegates()
{
	FEditorDelegates::PostPIEStarted.Remove(PostPieStartedHandle);
	FEditorDelegates::EndPIE.Remove(EndPieHandle);
	FEditorDelegates::PausePIE.Remove(PausePieHandle);
	FEditorDelegates::ResumePIE.Remove(ResumePieHandle);
}
