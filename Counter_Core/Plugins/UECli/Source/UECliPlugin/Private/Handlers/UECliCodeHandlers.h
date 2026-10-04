// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

struct FHttpServerRequest;

/** Live Coding control for the running editor. */
namespace UECli::CodeHandlers
{
	void Init();
	void Shutdown();

	bool LiveCodingStatus(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);  // GET  /code/livecoding
	bool LiveCodingCompile(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete); // POST /code/livecoding/compile { wait }
}
