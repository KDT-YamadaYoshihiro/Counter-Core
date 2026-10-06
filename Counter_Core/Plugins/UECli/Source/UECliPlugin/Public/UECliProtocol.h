// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * UE CLI wire-protocol version. Bump whenever the request/response shape of any
 * endpoint changes in a way the C# Core would notice. The Core compares this
 * against its own expectation during the handshake.
 */
#define UECLI_PROTOCOL_VERSION 3

/** Default loopback port for the v1 HTTP command channel (matches UECliConnectionOptions in Core). */
#define UECLI_DEFAULT_HTTP_PORT 8720

/** Default loopback port for the v2 WebSocket event channel. */
#define UECLI_DEFAULT_EVENT_PORT 8721

/** Header carrying the optional shared secret. */
#define UECLI_TOKEN_HEADER TEXT("X-UECli-Token")
