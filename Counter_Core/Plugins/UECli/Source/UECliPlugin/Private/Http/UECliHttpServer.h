// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpRequestHandler.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"

class IHttpRouter;

/**
 * v1 command channel: an embedded HTTP listener bound to loopback that answers
 * UE CLI Core requests on the game thread. HTTP was chosen for v1 because it is
 * trivial to implement and every call is reproducible with curl. Events/streams
 * (compile finished, PIE crashed, log lines) are a later WebSocket channel; see
 * doc/transport-roadmap.md.
 */
class FUECliHttpServer
{
public:
	FUECliHttpServer(uint32 InPort, const FString& InToken);
	~FUECliHttpServer();

	/** Binds routes and starts the listener. Returns false if the port could not be bound. */
	bool Start();

	/** Stops the listener and releases all routes. Safe to call more than once. */
	void Stop();

private:
	/** Rejects requests without the configured token before they reach a handler. */
	bool Authorize(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) const;

	bool BindVerb(const TCHAR* Path, EHttpServerRequestVerbs Verb, const FHttpRequestHandler& Handler);

	/** GET /capabilities: protocol version, every bound route, job kinds, feature flags. */
	bool Capabilities(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) const;

	uint32 Port;
	TArray<TPair<FString, FString>> Routes; // (method, path), as bound
	FString Token;

	TSharedPtr<IHttpRouter> Router;
	TArray<FHttpRouteHandle> RouteHandles;
	FDelegateHandle PreprocessorHandle;
	bool bListenerStarted = false;
};
