// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

struct FHttpServerRequest;

/** Request handlers for Widget Blueprint (UMG) designer-tree edits. */
namespace UECli::WidgetHandlers
{
	bool GetTree(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);       // GET    /widget/tree?path=
	bool AddWidget(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);     // POST   /widget/widgets { path, class, name?, parent?, index? }
	bool RemoveWidget(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);  // DELETE /widget/widget?path=&name=
	bool Reparent(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);      // POST   /widget/widget/parent { path, name, parent, index? }
	bool SetBinding(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);    // POST   /widget/binding { path, widget, property, function }
}
