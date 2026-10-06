// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

struct FHttpServerRequest;

/**
 * Phase 5 runtime handlers: log tail, Play-In-Editor control, screenshots.
 * The module calls Init()/Shutdown() to own the log-capture device.
 */
namespace UECli::RuntimeHandlers
{
	void Init();
	void Shutdown();

	bool Logs(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);        // GET  /logs?since=&limit=&severity=&category=
	bool Play(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);         // POST /editor/play
	bool StopPlay(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);     // POST /editor/play/stop
	bool PlayStatus(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);   // GET  /editor/play
	bool Screenshot(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);   // POST /editor/screenshot
}
