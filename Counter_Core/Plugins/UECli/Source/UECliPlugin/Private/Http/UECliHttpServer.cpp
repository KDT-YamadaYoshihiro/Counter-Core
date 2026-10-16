// Copyright UE CLI. All rights reserved.

#include "Http/UECliHttpServer.h"
#include "UECliServices.h"

#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "IPAddress.h"
#include "Misc/ConfigCacheIni.h"

#include "Handlers/UECliBlueprintHandlers.h"
#include "Handlers/UECliCodeHandlers.h"
#include "Handlers/UECliEditorHandlers.h"
#include "Handlers/UECliJobHandlers.h"
#include "Handlers/UECliLandscapeHandlers.h"
#include "Handlers/UECliNiagaraHandlers.h"
#include "Handlers/UECliLevelHandlers.h"
#include "Handlers/UECliMaterialHandlers.h"
#include "Handlers/UECliObjectHandlers.h"
#include "Handlers/UECliProjectHandlers.h"
#include "Handlers/UECliRuntimeHandlers.h"
#include "Handlers/UECliWidgetHandlers.h"
#include "Http/UECliHttpTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Jobs/UECliJobManager.h"
#include "UECliLog.h"
#include "UECliProtocol.h"

namespace
{
	using FUECliRouteHandler = bool (*)(const FHttpServerRequest&, const FHttpResultCallback&);

	struct FUECliRoute
	{
		const TCHAR* Path;
		EHttpServerRequestVerbs Verb;
		FUECliRouteHandler Handler;
	};

	/** Every stateless route: (path, verb, handler). `/capabilities` is bound separately (it reads the server). */
	const FUECliRoute GUECliHttpRoutes[] =
	{
		{ TEXT("/ping"),                           EHttpServerRequestVerbs::VERB_GET,    &UECli::EditorHandlers::Ping },
		{ TEXT("/version"),                        EHttpServerRequestVerbs::VERB_GET,    &UECli::EditorHandlers::Version },
		{ TEXT("/project"),                        EHttpServerRequestVerbs::VERB_GET,    &UECli::ProjectHandlers::ProjectInfo },
		{ TEXT("/assets"),                         EHttpServerRequestVerbs::VERB_GET,    &UECli::ProjectHandlers::SearchAssets },
		{ TEXT("/asset/save"),                     EHttpServerRequestVerbs::VERB_POST,   &UECli::ProjectHandlers::SaveAsset },
		{ TEXT("/object/properties"),              EHttpServerRequestVerbs::VERB_GET,    &UECli::ObjectHandlers::GetProperties },
		{ TEXT("/object/property"),                EHttpServerRequestVerbs::VERB_POST,   &UECli::ObjectHandlers::SetProperty },
		{ TEXT("/blueprints"),                     EHttpServerRequestVerbs::VERB_GET,    &UECli::BlueprintHandlers::List },
		{ TEXT("/blueprints"),                     EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::Create },
		{ TEXT("/blueprint"),                      EHttpServerRequestVerbs::VERB_GET,    &UECli::BlueprintHandlers::Inspect },
		{ TEXT("/blueprint"),                      EHttpServerRequestVerbs::VERB_DELETE, &UECli::BlueprintHandlers::DeleteAsset },
		{ TEXT("/blueprint/snapshot"),             EHttpServerRequestVerbs::VERB_GET,    &UECli::BlueprintHandlers::Snapshot },
		{ TEXT("/nodes/search"),                   EHttpServerRequestVerbs::VERB_GET,    &UECli::BlueprintHandlers::SearchNodes },
		{ TEXT("/blueprint/graph"),                EHttpServerRequestVerbs::VERB_GET,    &UECli::BlueprintHandlers::GetGraph },
		{ TEXT("/blueprint/graph/nodes"),          EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::AddNode },
		{ TEXT("/blueprint/graph/node"),           EHttpServerRequestVerbs::VERB_DELETE, &UECli::BlueprintHandlers::DeleteNode },
		{ TEXT("/blueprint/graph/node/pin"),       EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::SetPinValue },
		{ TEXT("/blueprint/graph/node/position"),  EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::MoveNode },
		{ TEXT("/blueprint/graph/connections"),    EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::Connect },
		{ TEXT("/blueprint/graph/connection"),     EHttpServerRequestVerbs::VERB_DELETE, &UECli::BlueprintHandlers::Disconnect },
		{ TEXT("/blueprint/compile"),              EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::Compile },
		{ TEXT("/blueprint/save"),                 EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::Save },
		{ TEXT("/blueprint/variables"),            EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::AddVariable },
		{ TEXT("/blueprint/variable/rename"),      EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::RenameVariable },
		{ TEXT("/blueprint/variable"),             EHttpServerRequestVerbs::VERB_DELETE, &UECli::BlueprintHandlers::RemoveVariable },
		{ TEXT("/blueprint/variable/default"),     EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::SetVariableDefault },
		{ TEXT("/blueprint/functions"),            EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::AddFunction },
		{ TEXT("/blueprint/function"),             EHttpServerRequestVerbs::VERB_DELETE, &UECli::BlueprintHandlers::RemoveFunction },
		{ TEXT("/blueprint/components"),           EHttpServerRequestVerbs::VERB_GET,    &UECli::BlueprintHandlers::ListComponents },
		{ TEXT("/blueprint/components"),           EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::AddComponent },
		{ TEXT("/blueprint/component"),            EHttpServerRequestVerbs::VERB_DELETE, &UECli::BlueprintHandlers::RemoveComponent },
		{ TEXT("/blueprint/component/property"),   EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::SetComponentProperty },
		{ TEXT("/blueprint/interfaces"),           EHttpServerRequestVerbs::VERB_GET,    &UECli::BlueprintHandlers::ListInterfaces },
		{ TEXT("/blueprint/interfaces"),           EHttpServerRequestVerbs::VERB_POST,   &UECli::BlueprintHandlers::AddInterface },
		{ TEXT("/blueprint/interface"),            EHttpServerRequestVerbs::VERB_DELETE, &UECli::BlueprintHandlers::RemoveInterface },
		{ TEXT("/material/graph"),                EHttpServerRequestVerbs::VERB_GET,    &UECli::MaterialHandlers::GetGraph },
		{ TEXT("/widget/tree"),                    EHttpServerRequestVerbs::VERB_GET,    &UECli::WidgetHandlers::GetTree },
		{ TEXT("/widget/widgets"),                 EHttpServerRequestVerbs::VERB_POST,   &UECli::WidgetHandlers::AddWidget },
		{ TEXT("/widget/widget"),                  EHttpServerRequestVerbs::VERB_DELETE, &UECli::WidgetHandlers::RemoveWidget },
		{ TEXT("/widget/widget/parent"),           EHttpServerRequestVerbs::VERB_POST,   &UECli::WidgetHandlers::Reparent },
		{ TEXT("/widget/binding"),                 EHttpServerRequestVerbs::VERB_POST,   &UECli::WidgetHandlers::SetBinding },
		{ TEXT("/editor/quit"),                    EHttpServerRequestVerbs::VERB_POST,   &UECli::EditorHandlers::Quit },
		{ TEXT("/editor/undo"),                    EHttpServerRequestVerbs::VERB_POST,   &UECli::EditorHandlers::Undo },
		{ TEXT("/editor/redo"),                    EHttpServerRequestVerbs::VERB_POST,   &UECli::EditorHandlers::Redo },
		{ TEXT("/logs"),                           EHttpServerRequestVerbs::VERB_GET,    &UECli::RuntimeHandlers::Logs },
		{ TEXT("/editor/play"),                    EHttpServerRequestVerbs::VERB_GET,    &UECli::RuntimeHandlers::PlayStatus },
		{ TEXT("/editor/play"),                    EHttpServerRequestVerbs::VERB_POST,   &UECli::RuntimeHandlers::Play },
		{ TEXT("/editor/play/stop"),               EHttpServerRequestVerbs::VERB_POST,   &UECli::RuntimeHandlers::StopPlay },
		{ TEXT("/editor/screenshot"),              EHttpServerRequestVerbs::VERB_POST,   &UECli::RuntimeHandlers::Screenshot },
		{ TEXT("/code/livecoding"),                EHttpServerRequestVerbs::VERB_GET,    &UECli::CodeHandlers::LiveCodingStatus },
		{ TEXT("/code/livecoding/compile"),        EHttpServerRequestVerbs::VERB_POST,   &UECli::CodeHandlers::LiveCodingCompile },
		{ TEXT("/jobs"),                           EHttpServerRequestVerbs::VERB_POST,   &UECli::JobHandlers::Start },
		{ TEXT("/jobs"),                           EHttpServerRequestVerbs::VERB_GET,    &UECli::JobHandlers::List },
		{ TEXT("/job"),                            EHttpServerRequestVerbs::VERB_GET,    &UECli::JobHandlers::Status },
		{ TEXT("/job/cancel"),                     EHttpServerRequestVerbs::VERB_POST,   &UECli::JobHandlers::Cancel },
		{ TEXT("/level/actors"),                   EHttpServerRequestVerbs::VERB_GET,    &UECli::LevelHandlers::ListActors },
		{ TEXT("/level/actors"),                   EHttpServerRequestVerbs::VERB_POST,   &UECli::LevelHandlers::SpawnActor },
		{ TEXT("/level/actor"),                    EHttpServerRequestVerbs::VERB_DELETE, &UECli::LevelHandlers::DeleteActor },
		{ TEXT("/level/actor/property"),           EHttpServerRequestVerbs::VERB_POST,   &UECli::LevelHandlers::SetProperty },
		{ TEXT("/level/actor/transform"),          EHttpServerRequestVerbs::VERB_POST,   &UECli::LevelHandlers::SetTransform },
		{ TEXT("/landscapes"),                     EHttpServerRequestVerbs::VERB_POST,   &UECli::LandscapeHandlers::Create },
		{ TEXT("/landscape"),                      EHttpServerRequestVerbs::VERB_GET,    &UECli::LandscapeHandlers::Describe },
		{ TEXT("/landscape"),                      EHttpServerRequestVerbs::VERB_DELETE, &UECli::LandscapeHandlers::Delete },
		{ TEXT("/landscape/heightmap"),            EHttpServerRequestVerbs::VERB_POST,   &UECli::LandscapeHandlers::ImportHeightmap },
		{ TEXT("/landscape/road"),                 EHttpServerRequestVerbs::VERB_POST,   &UECli::LandscapeHandlers::Road },
		{ TEXT("/landscape/ramp"),                 EHttpServerRequestVerbs::VERB_POST,   &UECli::LandscapeHandlers::Ramp },
		{ TEXT("/landscape/sculpt"),               EHttpServerRequestVerbs::VERB_POST,   &UECli::LandscapeHandlers::Sculpt },
		{ TEXT("/landscape/height"),               EHttpServerRequestVerbs::VERB_GET,    &UECli::LandscapeHandlers::HeightAt },
		{ TEXT("/landscape/layer-import"),         EHttpServerRequestVerbs::VERB_POST,   &UECli::LandscapeHandlers::ImportLayer },
		{ TEXT("/landscape/export"),               EHttpServerRequestVerbs::VERB_POST,   &UECli::LandscapeHandlers::Export },
		{ TEXT("/landscape/layers"),               EHttpServerRequestVerbs::VERB_GET,    &UECli::LandscapeHandlers::Layers },
		{ TEXT("/landscape/layers"),               EHttpServerRequestVerbs::VERB_POST,   &UECli::LandscapeHandlers::AddLayer },
		{ TEXT("/landscape/paint"),                EHttpServerRequestVerbs::VERB_POST,   &UECli::LandscapeHandlers::Paint },
		{ TEXT("/landscape/weight"),               EHttpServerRequestVerbs::VERB_GET,    &UECli::LandscapeHandlers::WeightAt },
		{ TEXT("/niagara/systems"),               EHttpServerRequestVerbs::VERB_POST,   &UECli::NiagaraHandlers::CreateSystem },
		{ TEXT("/niagara/system"),                EHttpServerRequestVerbs::VERB_GET,    &UECli::NiagaraHandlers::Describe },
		{ TEXT("/niagara/emitters"),              EHttpServerRequestVerbs::VERB_POST,   &UECli::NiagaraHandlers::AddEmitter },
		{ TEXT("/niagara/emitter"),               EHttpServerRequestVerbs::VERB_DELETE, &UECli::NiagaraHandlers::RemoveEmitter },
		{ TEXT("/niagara/modules"),               EHttpServerRequestVerbs::VERB_POST,   &UECli::NiagaraHandlers::AddModule },
		{ TEXT("/niagara/module"),                EHttpServerRequestVerbs::VERB_DELETE, &UECli::NiagaraHandlers::RemoveModule },
		{ TEXT("/niagara/module/move"),           EHttpServerRequestVerbs::VERB_POST,   &UECli::NiagaraHandlers::MoveModule },
		{ TEXT("/niagara/module/enabled"),        EHttpServerRequestVerbs::VERB_POST,   &UECli::NiagaraHandlers::SetModuleEnabled },
		{ TEXT("/niagara/module/input"),          EHttpServerRequestVerbs::VERB_POST,   &UECli::NiagaraHandlers::SetInput },
		{ TEXT("/niagara/module/input/dynamic"),  EHttpServerRequestVerbs::VERB_POST,   &UECli::NiagaraHandlers::SetDynamicInput },
		{ TEXT("/niagara/module/input/link"),     EHttpServerRequestVerbs::VERB_POST,   &UECli::NiagaraHandlers::LinkInput },
		{ TEXT("/niagara/module/input"),          EHttpServerRequestVerbs::VERB_DELETE, &UECli::NiagaraHandlers::ResetInput },
		{ TEXT("/niagara/renderers"),             EHttpServerRequestVerbs::VERB_POST,   &UECli::NiagaraHandlers::AddRenderer },
		{ TEXT("/niagara/user-parameters"),       EHttpServerRequestVerbs::VERB_POST,   &UECli::NiagaraHandlers::SetUserParameter },
		{ TEXT("/niagara/user-parameter"),        EHttpServerRequestVerbs::VERB_DELETE, &UECli::NiagaraHandlers::RemoveUserParameter },
		{ TEXT("/niagara/emitter/settings"),     EHttpServerRequestVerbs::VERB_POST,   &UECli::NiagaraHandlers::SetEmitterSettings },
		{ TEXT("/niagara/renderer"),              EHttpServerRequestVerbs::VERB_DELETE, &UECli::NiagaraHandlers::RemoveRenderer },
		{ TEXT("/niagara/compile"),               EHttpServerRequestVerbs::VERB_POST,   &UECli::NiagaraHandlers::Compile },
	};
}

FUECliHttpServer::FUECliHttpServer(uint32 InPort, const FString& InToken)
	: Port(InPort)
	, Token(InToken)
{
}

FUECliHttpServer::~FUECliHttpServer()
{
	Stop();
}

bool FUECliHttpServer::BindVerb(const TCHAR* Path, EHttpServerRequestVerbs Verb, const FHttpRequestHandler& Handler)
{
	FHttpRouteHandle Handle = Router->BindRoute(FHttpPath(Path), Verb, Handler);
	if (!Handle.IsValid())
	{
		UE_LOG(LogUECli, Error, TEXT("UE CLI failed to bind route %s."), Path);
		return false;
	}
	RouteHandles.Add(MoveTemp(Handle));

	const TCHAR* Method =
		Verb == EHttpServerRequestVerbs::VERB_GET ? TEXT("GET") :
		Verb == EHttpServerRequestVerbs::VERB_POST ? TEXT("POST") :
		Verb == EHttpServerRequestVerbs::VERB_DELETE ? TEXT("DELETE") : TEXT("OTHER");
	Routes.Emplace(Method, Path);
	return true;
}

bool FUECliHttpServer::Capabilities(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) const
{
	using namespace UECli::Http;

	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("protocolVersion"), UECLI_PROTOCOL_VERSION);

	TArray<TSharedPtr<FJsonValue>> RouteValues;
	for (const TPair<FString, FString>& Route : Routes)
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("method"), Route.Key);
		Json->SetStringField(TEXT("path"), Route.Value);
		RouteValues.Add(MakeShared<FJsonValueObject>(Json));
	}
	Body->SetArrayField(TEXT("routes"), RouteValues);

	TArray<TSharedPtr<FJsonValue>> Kinds;
	for (const FString& Kind : UECli::Services::Jobs().KnownKinds())
	{
		Kinds.Add(MakeShared<FJsonValueString>(Kind));
	}
	Body->SetArrayField(TEXT("jobKinds"), Kinds);

	// Behaviours a client may want to probe for rather than infer from routes.
	TArray<TSharedPtr<FJsonValue>> Features;
	for (const TCHAR* Feature : {
		TEXT("auth.first_message_token"), TEXT("events.resume"), TEXT("jobs.live_compile"),
		TEXT("blueprint.snapshot"), TEXT("blueprint.save_requires_compile"), TEXT("blueprint.delete_reference_check"),
		TEXT("editor.quit") })
	{
		Features.Add(MakeShared<FJsonValueString>(Feature));
	}
	Body->SetArrayField(TEXT("features"), Features);
	return SendJson(OnComplete, 200, Body);
}

bool FUECliHttpServer::Start()
{
	// Pin our listener to loopback even if the project sets DefaultBindAddress=any.
	{
		static const TCHAR* Section = TEXT("HTTPServer.Listeners");
		TArray<FString> Overrides;
		GConfig->GetArray(Section, TEXT("ListenerOverrides"), Overrides, GEngineIni);
		const FString PortKey = FString::Printf(TEXT("Port=%u"), Port);
		Overrides.RemoveAll([&PortKey](const FString& Entry) { return Entry.Contains(PortKey + TEXT(",")) || Entry.Contains(PortKey + TEXT(")")); });
		Overrides.Insert(FString::Printf(TEXT("(%s,BindAddress=127.0.0.1)"), *PortKey), 0);
		GConfig->SetArray(Section, TEXT("ListenerOverrides"), Overrides, GEngineIni);
	}

	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
	Router = HttpServerModule.GetHttpRouter(Port);
	if (!Router.IsValid())
	{
		return false;
	}

	PreprocessorHandle = Router->RegisterRequestPreprocessor(
		FHttpRequestHandler::CreateRaw(this, &FUECliHttpServer::Authorize));

	for (const FUECliRoute& Route : GUECliHttpRoutes)
	{
		if (!BindVerb(Route.Path, Route.Verb, FHttpRequestHandler::CreateStatic(Route.Handler)))
		{
			Stop();
			return false;
		}
	}

	const bool bBound =
		BindVerb(TEXT("/capabilities"), EHttpServerRequestVerbs::VERB_GET, FHttpRequestHandler::CreateRaw(this, &FUECliHttpServer::Capabilities));

	if (!bBound)
	{
		Stop();
		return false;
	}

	HttpServerModule.StartAllListeners();
	bListenerStarted = true;
	return true;
}

void FUECliHttpServer::Stop()
{
	if (Router.IsValid())
	{
		if (PreprocessorHandle.IsValid())
		{
			Router->UnregisterRequestPreprocessor(PreprocessorHandle);
			PreprocessorHandle.Reset();
		}
		for (const FHttpRouteHandle& Handle : RouteHandles)
		{
			if (Handle.IsValid())
			{
				Router->UnbindRoute(Handle);
			}
		}
	}

	RouteHandles.Reset();
	Router.Reset();

	// The listener itself stays up: FHttpServerModule can only stop every listener at
	// once, which would also take down other users (Remote Control, ...). With our
	// routes unbound the port answers nothing of ours; the module closes it at shutdown.
	bListenerStarted = false;
}

bool FUECliHttpServer::Authorize(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) const
{
	using namespace UECli::Http;

	// Loopback peers only, whatever the listener ended up bound to.
	if (Request.PeerAddress.IsValid() && Request.PeerAddress->ToString(false) != TEXT("127.0.0.1"))
	{
		SendError(OnComplete, 403, TEXT("auth.forbidden"), TEXT("Only loopback clients are accepted."));
		return true;
	}

	// DNS-rebinding / browser guard: Host must be loopback, and browsers (which
	// always send Origin) are refused outright.
	const FString Host = HeaderValue(Request, TEXT("Host"));
	const FString Expected = FString::Printf(TEXT(":%u"), Port);
	const bool bHostOk = Host == FString(TEXT("127.0.0.1")) + Expected || Host == FString(TEXT("localhost")) + Expected;
	if (!bHostOk || !HeaderValue(Request, TEXT("Origin")).IsEmpty())
	{
		SendError(OnComplete, 403, TEXT("auth.forbidden"), TEXT("Bad Host or cross-origin request."));
		return true;
	}

	if (TokenEquals(HeaderValue(Request, UECLI_TOKEN_HEADER), Token))
	{
		return false; // fall through to the route handler
	}

	SendError(OnComplete, 401, TEXT("auth.unauthorized"), TEXT("Missing or invalid X-UECli-Token."));
	return true; // handled — stop dispatch
}
