// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "Logging/LogVerbosity.h"
#include "UECliServices.h"

class FJsonObject;
class INetworkingWebSocket;
class IWebSocketServer;

/**
 * v2 event channel: a loopback WebSocket that pushes editor events to the Core
 * (compile started/finished, PIE started/stopped, a Blueprint changed or saved,
 * and — on request — the log stream). HTTP stays the command channel; this is
 * the "here is what just happened" direction. See doc/transport-roadmap.md.
 *
 * Clients connect to ws://127.0.0.1:<port>/?token=<if required> and may send a
 * subscription frame:
 *   { "topics": ["*"] | ["blueprint.compile", "pie", ...], "logSeverity": "warning" }
 * Every event frame is { "type", "seq", "time", "payload" }.
 *
 * Built on the WebSocketNetworking plugin (same module RemoteControl uses).
 */
class FUECliEventHub : public IUECliEventSink
{
public:
	static FUECliEventHub& Get();

	void Init(uint32 Port, const FString& Token);
	void Shutdown();
	bool IsRunning() const { return bRunning; }

	/** Push an event to every subscribed client. Game thread only. */
	virtual void Broadcast(const FString& Type, const TSharedRef<FJsonObject>& Payload) override;
	void Broadcast(const FString& Type);

private:
	struct FClient
	{
		INetworkingWebSocket* Socket = nullptr;
		TSet<FString> Topics;
		bool bWantsLog = false;
		bool bAuthed = false;
		ELogVerbosity::Type LogSeverity = ELogVerbosity::Warning;
	};

	void OnClientConnected(INetworkingWebSocket* Socket);
	void OnClientClosed(INetworkingWebSocket* Socket);
	void OnRawMessage(void* Data, int32 Size, INetworkingWebSocket* Socket);

	FClient* FindClient(INetworkingWebSocket* Socket);
	void SendHello(INetworkingWebSocket* Socket);
	static void SendTo(INetworkingWebSocket* Socket, const FString& Text);
	static bool TopicMatches(const TSet<FString>& Topics, const FString& Type);

	void OnLogLine(double Time, ELogVerbosity::Type Verbosity, FName Category, const FString& Message);
	bool Tick(float DeltaTime);

	void HookEditorDelegates();
	void UnhookEditorDelegates();

	TUniquePtr<IWebSocketServer> Server;
	TArray<FClient> Clients;
	FString RequiredToken;
	uint64 NextSeq = 1;
	bool bRunning = false;

	struct FQueuedLog
	{
		double Time;
		ELogVerbosity::Type Verbosity;
		FString Category;
		FString Message;
	};
	TQueue<FQueuedLog, EQueueMode::Mpsc> LogQueue;

	/** Recent broadcast frames for {"resumeAfter": seq} replay (log lines excluded). */
	struct FHistoryEntry
	{
		uint64 Seq = 0;
		FString Type;
		FString Text;
	};
	TArray<FHistoryEntry> History;
	uint64 EvictedThrough = 0;          // highest seq dropped from History
	static constexpr int32 MaxHistory = 2000;
	FString SessionId;                  // new per editor session; sent in hello

	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle PostPieStartedHandle;
	FDelegateHandle EndPieHandle;
	FDelegateHandle PausePieHandle;
	FDelegateHandle ResumePieHandle;
};
