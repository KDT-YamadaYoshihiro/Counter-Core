// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

struct FHttpServerRequest;

/** Request handlers for landscape creation, heightmap import and sculpting. */
namespace UECli::LandscapeHandlers
{
	bool Create(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);          // POST /landscapes { location?, componentsX?, ..., heightmap?, material?, label? }
	bool Describe(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);        // GET  /landscape?name=
	bool ImportHeightmap(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete); // POST /landscape/heightmap { name?, file }
	bool Sculpt(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);          // POST /landscape/sculpt { name?, tool, center[2], radius, strength?, height?, falloff? }
	bool Ramp(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);            // POST /landscape/ramp { name?, from[3], to[3], width, falloff? }
	bool Road(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);            // POST /landscape/road { name?, points[[x,y,z],...], width, falloff? }
	bool Delete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);          // DELETE /landscape?name=
	bool HeightAt(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);        // GET  /landscape/height?name=&x=&y=
	bool Export(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);          // POST /landscape/export { name?, file, layer? }
	bool ImportLayer(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);     // POST /landscape/layer-import { name?, layer, file }
	bool Layers(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);          // GET  /landscape/layers?name=
	bool AddLayer(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);        // POST /landscape/layers { name?, layer, folder? }
	bool Paint(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);           // POST /landscape/paint { name?, layer, tool?, center[2], radius, strength?, falloff? }
	bool WeightAt(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);        // GET  /landscape/weight?name=&layer=&x=&y=
}
