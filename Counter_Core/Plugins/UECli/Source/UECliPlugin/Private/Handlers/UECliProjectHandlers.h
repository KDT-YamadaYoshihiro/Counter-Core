// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

struct FHttpServerRequest;

/** Request handlers for the project / asset routes. */
namespace UECli::ProjectHandlers
{
	bool ProjectInfo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);   // GET  /project
	bool SearchAssets(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);  // GET  /assets?query=&type=&path=&limit=
	bool SaveAsset(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);     // POST /asset/save?path=
}
