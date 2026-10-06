// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FUECliJobManager;

/** Where handlers, jobs and the job manager publish events (the WebSocket hub in production). */
class IUECliEventSink
{
public:
	virtual ~IUECliEventSink() = default;
	virtual void Broadcast(const FString& Type, const TSharedRef<FJsonObject>& Payload) = 0;
};

/**
 * Injection point for the plugin's shared services. Code asks here instead of
 * calling FUECliEventHub::Get() / FUECliJobManager::Get() directly, so tests
 * can substitute a recording sink or a private job manager with the scoped
 * overrides below (game thread only; overrides nest).
 */
namespace UECli::Services
{
	IUECliEventSink& Events();
	FUECliJobManager& Jobs();

	class FScopedEventSink
	{
	public:
		explicit FScopedEventSink(IUECliEventSink& Sink);
		~FScopedEventSink();
		FScopedEventSink(const FScopedEventSink&) = delete;
		FScopedEventSink& operator=(const FScopedEventSink&) = delete;

	private:
		IUECliEventSink* Previous;
	};

	class FScopedJobManager
	{
	public:
		explicit FScopedJobManager(FUECliJobManager& Manager);
		~FScopedJobManager();
		FScopedJobManager(const FScopedJobManager&) = delete;
		FScopedJobManager& operator=(const FScopedJobManager&) = delete;

	private:
		FUECliJobManager* Previous;
	};
}
