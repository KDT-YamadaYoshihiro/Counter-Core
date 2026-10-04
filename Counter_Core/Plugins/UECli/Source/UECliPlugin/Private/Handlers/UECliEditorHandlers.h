// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

struct FHttpServerRequest;

/** Request handlers for the editor lifecycle / identity routes. */
namespace UECli::EditorHandlers
{
	/** GET /ping */
	bool Ping(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** GET /version */
	bool Version(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** POST /editor/undo — undo the last transaction (e.g. a UE CLI edit). */
	/** POST /editor/quit[?force=true] — exit the editor (refused with unsaved packages unless forced). Returns { ok, pid }. */
	bool Quit(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	bool Undo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** POST /editor/redo */
	bool Redo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
}
