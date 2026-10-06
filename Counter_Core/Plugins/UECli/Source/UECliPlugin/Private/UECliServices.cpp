// Copyright UE CLI. All rights reserved.

#include "UECliServices.h"

#include "Events/UECliEventHub.h"
#include "Jobs/UECliJobManager.h"

namespace UECli::Services
{
	namespace
	{
		IUECliEventSink* GEventsOverride = nullptr;
		FUECliJobManager* GJobsOverride = nullptr;
	}

	IUECliEventSink& Events()
	{
		return GEventsOverride ? *GEventsOverride : static_cast<IUECliEventSink&>(FUECliEventHub::Get());
	}

	FUECliJobManager& Jobs()
	{
		return GJobsOverride ? *GJobsOverride : FUECliJobManager::Get();
	}

	FScopedEventSink::FScopedEventSink(IUECliEventSink& Sink)
		: Previous(GEventsOverride)
	{
		GEventsOverride = &Sink;
	}

	FScopedEventSink::~FScopedEventSink()
	{
		GEventsOverride = Previous;
	}

	FScopedJobManager::FScopedJobManager(FUECliJobManager& Manager)
		: Previous(GJobsOverride)
	{
		GJobsOverride = &Manager;
	}

	FScopedJobManager::~FScopedJobManager()
	{
		GJobsOverride = Previous;
	}
}
