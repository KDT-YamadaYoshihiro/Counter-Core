// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

struct FHttpServerRequest;

/** Request handlers for the v3 job routes. */
namespace UECli::JobHandlers
{
	bool Start(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);   // POST /jobs   { kind, params }
	bool List(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);    // GET  /jobs?limit=
	bool Status(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);  // GET  /job?id=
	bool Cancel(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);  // POST /job/cancel?id=
}
