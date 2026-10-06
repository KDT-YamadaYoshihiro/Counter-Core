// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FUECliHttpServer;

/**
 * Editor module entry point. Owns the local command endpoint that the UE CLI
 * Core (C#) talks to. Phase 0 scope: bring the endpoint up and answer the
 * /ping and /version handshake.
 */
class FUECliPluginModule : public IModuleInterface
{
public:
	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

private:
	TUniquePtr<FUECliHttpServer> HttpServer;
};
