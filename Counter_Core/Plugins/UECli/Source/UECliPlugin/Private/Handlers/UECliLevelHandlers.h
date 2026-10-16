// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

struct FHttpServerRequest;

/** Request handlers for the level / actor routes. */
namespace UECli::LevelHandlers
{
	bool ListActors(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);   // GET    /level/actors?class=&name=
	bool SpawnActor(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);    // POST   /level/actors { class, location, rotation?, name? }
	bool DeleteActor(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);   // DELETE /level/actor?name=
	bool SetProperty(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);   // POST   /level/actor/property { actor, property, value }
	bool SetTransform(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);  // POST   /level/actor/transform { actor, location?, rotation?, scale? }
}
