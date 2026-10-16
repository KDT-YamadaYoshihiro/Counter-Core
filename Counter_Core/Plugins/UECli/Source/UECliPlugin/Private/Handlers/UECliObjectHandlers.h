// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

struct FHttpServerRequest;

/** Request handlers for generic reflection access to any asset / object / actor. */
namespace UECli::ObjectHandlers
{
	bool GetProperties(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);  // GET  /object/properties?path=&filter=&all=&property=
	bool SetProperty(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);    // POST /object/property { path, property, value }
}
