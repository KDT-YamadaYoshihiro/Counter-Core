// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

struct FHttpServerRequest;

/** Request handlers for material graphs. */
namespace UECli::MaterialHandlers
{
	bool GetGraph(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete); // GET /material/graph?path=
}
