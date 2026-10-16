// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

struct FHttpServerRequest;

/** Request handlers for editing Niagara systems: emitters, module stacks, module inputs, renderers. */
namespace UECli::NiagaraHandlers
{
	bool CreateSystem(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);     // POST   /niagara/systems { path, emitters?[] }
	bool Describe(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);         // GET    /niagara/system?path=
	bool AddEmitter(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);       // POST   /niagara/emitters { path, emitter, name? }
	bool RemoveEmitter(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);    // DELETE /niagara/emitter?path=&emitter=
	bool AddModule(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);        // POST   /niagara/modules { path, emitter, stage, script, index? }
	bool RemoveModule(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);     // DELETE /niagara/module?path=&emitter=&stage=&module=
	bool MoveModule(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);       // POST   /niagara/module/move { path, emitter, stage, module, index }
	bool SetModuleEnabled(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete); // POST   /niagara/module/enabled { path, emitter, stage, module, enabled }
	bool SetInput(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);         // POST   /niagara/module/input { path, emitter, stage, module, input, value }
	bool SetDynamicInput(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);  // POST   /niagara/module/input/dynamic { path, emitter, stage, module, input, script }
	bool LinkInput(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);        // POST   /niagara/module/input/link { path, emitter, stage, module, input, parameter }
	bool ResetInput(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);       // DELETE /niagara/module/input?path=&emitter=&stage=&module=&input=
	bool AddRenderer(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);      // POST   /niagara/renderers { path, emitter, type }
	bool SetUserParameter(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);    // POST   /niagara/user-parameters { path, name, type?, value }
	bool RemoveUserParameter(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete); // DELETE /niagara/user-parameter?path=&name=
	bool SetEmitterSettings(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete); // POST   /niagara/emitter/settings { path, emitter, enabled?, simTarget?, localSpace?, determinism? }
	bool RemoveRenderer(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);     // DELETE /niagara/renderer?path=&emitter=&index=
	bool Compile(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);          // POST   /niagara/compile { path }
}
