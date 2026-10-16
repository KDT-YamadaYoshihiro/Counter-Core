// Copyright UE CLI. All rights reserved.

#include "UECliPluginModule.h"

#include "Events/UECliEventHub.h"
#include "Handlers/UECliCodeHandlers.h"
#include "Handlers/UECliRuntimeHandlers.h"
#include "Http/UECliHttpServer.h"
#include "Jobs/UECliBuiltinJobs.h"
#include "Jobs/UECliJobManager.h"
#include "UECliLog.h"
#include "UECliProtocol.h"

#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "IPAddress.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Misc/CString.h"

#define LOCTEXT_NAMESPACE "FUECliPluginModule"

DEFINE_LOG_CATEGORY(LogUECli);

namespace UECliPluginModulePrivate
{
	uint32 ResolvePort(const TCHAR* EnvVar, uint32 Default)
	{
		const FString PortText = FPlatformMisc::GetEnvironmentVariable(EnvVar);
		if (!PortText.IsEmpty())
		{
			const int32 Parsed = FCString::Atoi(*PortText);
			if (Parsed > 0 && Parsed < 65536)
			{
				return static_cast<uint32>(Parsed);
			}

			UE_LOG(LogUECli, Warning, TEXT("%s='%s' is out of range; using default %u."), EnvVar, *PortText, Default);
		}

		return Default;
	}

	/** True if nothing else (e.g. a second editor) already listens on the loopback port. */
	bool IsLoopbackPortFree(uint32 Port)
	{
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!Sockets)
		{
			return true;
		}
		FSocket* Probe = Sockets->CreateSocket(NAME_Stream, TEXT("UECliPortProbe"), FNetworkProtocolTypes::IPv4);
		if (!Probe)
		{
			return true;
		}
		const TSharedRef<FInternetAddr> Addr = Sockets->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
		Addr->SetLoopbackAddress();
		Addr->SetPort(static_cast<int32>(Port));
		const bool bFree = Probe->Bind(*Addr);
		Probe->Close();
		Sockets->DestroySocket(Probe);
		return bFree;
	}

	/**
	 * UECLI_TOKEN if set; otherwise a fresh random token per editor session.
	 * Either way it is written to <UserSettingsDir>/UECli/token-<port> (the
	 * per-user %LOCALAPPDATA% on Windows) where the CLI/MCP pick it up.
	 */
	FString ResolveToken(uint32 HttpPort)
	{
		FString Token = FPlatformMisc::GetEnvironmentVariable(TEXT("UECLI_TOKEN"));
		if (Token.IsEmpty())
		{
			Token = FGuid::NewGuid().ToString(EGuidFormats::Digits) + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		}

		const FString Path = FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("UECli"), FString::Printf(TEXT("token-%u"), HttpPort));
		if (!FFileHelper::SaveStringToFile(Token, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogUECli, Warning, TEXT("Could not write the UE CLI token file '%s'; set UECLI_TOKEN for the CLI manually."), *Path);
		}
		return Token;
	}
}

void FUECliPluginModule::StartupModule()
{
	const uint32 HttpPort = UECliPluginModulePrivate::ResolvePort(TEXT("UECLI_PORT"), UECLI_DEFAULT_HTTP_PORT);
	const uint32 EventPort = UECliPluginModulePrivate::ResolvePort(TEXT("UECLI_EVENT_PORT"), UECLI_DEFAULT_EVENT_PORT);

	UECli::RuntimeHandlers::Init();
	UECli::CodeHandlers::Init();
	UECli::Jobs::RegisterBuiltins();
	FUECliJobManager::Get().Init();

	// Another editor owns the port: serve nothing (the CLI keeps talking to that
	// one) and leave its token file alone.
	if (!UECliPluginModulePrivate::IsLoopbackPortFree(HttpPort))
	{
		UE_LOG(LogUECli, Error, TEXT("UE CLI port %u is already in use (another editor?). This editor is not reachable by the CLI/MCP; set UECLI_PORT and UECLI_EVENT_PORT to run a second instance."), HttpPort);
		return;
	}

	const FString Token = UECliPluginModulePrivate::ResolveToken(HttpPort);

	HttpServer = MakeUnique<FUECliHttpServer>(HttpPort, Token);
	if (HttpServer->Start())
	{
		UE_LOG(LogUECli, Display, TEXT("UE CLI Plugin listening on http://127.0.0.1:%u/ (protocol v%d)."), HttpPort, UECLI_PROTOCOL_VERSION);
	}
	else
	{
		UE_LOG(LogUECli, Error, TEXT("UE CLI Plugin failed to bind port %u. The CLI/MCP will not be able to connect."), HttpPort);
	}

	FUECliEventHub::Get().Init(EventPort, Token);
}

void FUECliPluginModule::ShutdownModule()
{
	FUECliJobManager::Get().Shutdown();
	FUECliEventHub::Get().Shutdown();

	if (HttpServer.IsValid())
	{
		HttpServer->Stop();
		HttpServer.Reset();
	}

	UECli::CodeHandlers::Shutdown();
	UECli::RuntimeHandlers::Shutdown();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUECliPluginModule, UECliPlugin)
