// Copyright UE CLI. All rights reserved.

#include "Handlers/UECliLandscapeHandlers.h"
#include "UECliServices.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Events/UECliEventHub.h"
#include "Http/UECliHttpTypes.h"
#include "Landscape/UECliLandscapeOps.h"

namespace UECli::LandscapeHandlers
{
	using namespace UECli::Http;

	namespace
	{
		bool Fail(const FHttpResultCallback& OnComplete, bool bNotFound, const FString& Error)
		{
			return bNotFound
				? SendError(OnComplete, 404, TEXT("landscape.not_found"), Error)
				: SendError(OnComplete, 422, TEXT("landscape.edit_failed"), Error);
		}

		void NotifyChanged(const TCHAR* Op, const FString& Name)
		{
			const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("op"), Op);
			Payload->SetStringField(TEXT("actor"), Name);
			UECli::Services::Events().Broadcast(TEXT("level.changed"), Payload);
		}
	}

	bool Create(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		if (!LandscapeOps::Create(Body.ToSharedRef(), Out, Error))
		{
			return SendError(OnComplete, 422, TEXT("landscape.create_failed"), Error);
		}
		NotifyChanged(TEXT("landscape-create"), Out->GetStringField(TEXT("label")));
		return SendJson(OnComplete, 200, Out);
	}

	bool Describe(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!LandscapeOps::Describe(QueryParam(Request, TEXT("name")), Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool ImportHeightmap(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}
		FString Name, File;
		Body->TryGetStringField(TEXT("name"), Name);
		Body->TryGetStringField(TEXT("file"), File);
		if (File.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'file' is required."));
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		bool bNotFound = false;
		if (!LandscapeOps::ImportHeightmap(Name, File, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		NotifyChanged(TEXT("landscape-heightmap"), Name);
		return SendJson(OnComplete, 200, Out);
	}

	bool Sculpt(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}
		FString Name, Tool;
		Body->TryGetStringField(TEXT("name"), Name);
		Body->TryGetStringField(TEXT("tool"), Tool);
		const TArray<TSharedPtr<FJsonValue>>* CenterValues = nullptr;
		double Radius = 0;
		if (Tool.IsEmpty() || !Body->TryGetArrayField(TEXT("center"), CenterValues) || CenterValues->Num() != 2 || !Body->TryGetNumberField(TEXT("radius"), Radius))
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'tool', 'center' [x, y] and 'radius' are required."));
		}
		double Strength = 0, Falloff = 0.5, Height = 0, Wavelength = 0;
		Body->TryGetNumberField(TEXT("strength"), Strength);
		Body->TryGetNumberField(TEXT("falloff"), Falloff);
		Body->TryGetNumberField(TEXT("wavelength"), Wavelength);
		TOptional<double> Target;
		if (Body->TryGetNumberField(TEXT("height"), Height))
		{
			Target = Height;
		}

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		bool bNotFound = false;
		const FVector2D Center((*CenterValues)[0]->AsNumber(), (*CenterValues)[1]->AsNumber());
		if (!LandscapeOps::Sculpt(Name, Tool, Center, Radius, Strength, Target, Falloff, Wavelength, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		NotifyChanged(TEXT("landscape-sculpt"), Name);
		return SendJson(OnComplete, 200, Out);
	}

	bool Ramp(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}
		auto Read = [&Body](const TCHAR* Field, FVector& Out)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Body->TryGetArrayField(Field, Values) || Values->Num() != 3)
			{
				return false;
			}
			Out = FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
			return true;
		};
		FVector From, To;
		double Width = 0, Falloff = 0.5;
		if (!Read(TEXT("from"), From) || !Read(TEXT("to"), To) || !Body->TryGetNumberField(TEXT("width"), Width))
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'from' [x, y, z], 'to' [x, y, z] and 'width' are required."));
		}
		Body->TryGetNumberField(TEXT("falloff"), Falloff);
		FString Name;
		Body->TryGetStringField(TEXT("name"), Name);
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		bool bNotFound = false;
		if (!LandscapeOps::Ramp(Name, From, To, Width, Falloff, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		NotifyChanged(TEXT("landscape-sculpt"), Name);
		return SendJson(OnComplete, 200, Out);
	}

	bool Road(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}
		const TArray<TSharedPtr<FJsonValue>>* PointValues = nullptr;
		double Width = 0, Falloff = 0.5;
		TArray<FVector> Points;
		if (Body->TryGetArrayField(TEXT("points"), PointValues))
		{
			for (const TSharedPtr<FJsonValue>& Value : *PointValues)
			{
				const TArray<TSharedPtr<FJsonValue>>* Xyz = nullptr;
				if (!Value->TryGetArray(Xyz) || Xyz->Num() != 3)
				{
					return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Every point must be [x, y, z]."));
				}
				Points.Add(FVector((*Xyz)[0]->AsNumber(), (*Xyz)[1]->AsNumber(), (*Xyz)[2]->AsNumber()));
			}
		}
		if (Points.Num() < 2 || !Body->TryGetNumberField(TEXT("width"), Width))
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'points' (2+ [x, y, z]) and 'width' are required."));
		}
		Body->TryGetNumberField(TEXT("falloff"), Falloff);
		FString Name;
		Body->TryGetStringField(TEXT("name"), Name);
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		bool bNotFound = false;
		if (!LandscapeOps::Road(Name, Points, Width, Falloff, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		NotifyChanged(TEXT("landscape-sculpt"), Name);
		return SendJson(OnComplete, 200, Out);
	}

	bool Delete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const FString Name = QueryParam(Request, TEXT("name"));
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!LandscapeOps::Delete(Name, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		NotifyChanged(TEXT("landscape-delete"), Name);
		return SendJson(OnComplete, 200, Out);
	}

	bool HeightAt(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const FString XText = QueryParam(Request, TEXT("x"));
		const FString YText = QueryParam(Request, TEXT("y"));
		if (!XText.IsNumeric() || !YText.IsNumeric())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameters 'x' and 'y' (numbers) are required."));
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!LandscapeOps::HeightAt(QueryParam(Request, TEXT("name")), FCString::Atod(*XText), FCString::Atod(*YText), Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool Export(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}
		FString Name, File, Layer;
		Body->TryGetStringField(TEXT("name"), Name);
		Body->TryGetStringField(TEXT("file"), File);
		Body->TryGetStringField(TEXT("layer"), Layer);
		if (File.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'file' is required."));
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		bool bNotFound = false;
		if (!LandscapeOps::Export(Name, File, Layer, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool ImportLayer(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}
		FString Name, Layer, File;
		Body->TryGetStringField(TEXT("name"), Name);
		Body->TryGetStringField(TEXT("layer"), Layer);
		Body->TryGetStringField(TEXT("file"), File);
		if (Layer.IsEmpty() || File.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'layer' and 'file' are required."));
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		bool bNotFound = false;
		if (!LandscapeOps::ImportLayer(Name, Layer, File, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		NotifyChanged(TEXT("landscape-paint"), Name);
		return SendJson(OnComplete, 200, Out);
	}

	bool Layers(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!LandscapeOps::Layers(QueryParam(Request, TEXT("name")), Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}

	bool AddLayer(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}
		FString Name, Layer, Folder;
		Body->TryGetStringField(TEXT("name"), Name);
		Body->TryGetStringField(TEXT("layer"), Layer);
		Body->TryGetStringField(TEXT("folder"), Folder);
		if (Layer.IsEmpty())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body field 'layer' is required."));
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		bool bNotFound = false;
		if (!LandscapeOps::AddLayer(Name, Layer, Folder, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		NotifyChanged(TEXT("landscape-layer"), Name);
		return SendJson(OnComplete, 200, Out);
	}

	bool Paint(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Body = ParseJsonBody(Request, Error);
		if (!Body.IsValid())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), Error);
		}
		FString Name, Layer, Tool = TEXT("paint");
		Body->TryGetStringField(TEXT("name"), Name);
		Body->TryGetStringField(TEXT("layer"), Layer);
		Body->TryGetStringField(TEXT("tool"), Tool);
		const TArray<TSharedPtr<FJsonValue>>* CenterValues = nullptr;
		double Radius = 0;
		if (Layer.IsEmpty() || !Body->TryGetArrayField(TEXT("center"), CenterValues) || CenterValues->Num() != 2 || !Body->TryGetNumberField(TEXT("radius"), Radius))
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Body fields 'layer', 'center' [x, y] and 'radius' are required."));
		}
		double Strength = 1.0, Falloff = 0.5;
		Body->TryGetNumberField(TEXT("strength"), Strength);
		Body->TryGetNumberField(TEXT("falloff"), Falloff);
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		bool bNotFound = false;
		const FVector2D Center((*CenterValues)[0]->AsNumber(), (*CenterValues)[1]->AsNumber());
		if (!LandscapeOps::Paint(Name, Layer, Tool, Center, Radius, Strength, Falloff, Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		NotifyChanged(TEXT("landscape-paint"), Name);
		return SendJson(OnComplete, 200, Out);
	}

	bool WeightAt(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		const FString Layer = QueryParam(Request, TEXT("layer"));
		const FString XText = QueryParam(Request, TEXT("x"));
		const FString YText = QueryParam(Request, TEXT("y"));
		if (Layer.IsEmpty() || !XText.IsNumeric() || !YText.IsNumeric())
		{
			return SendError(OnComplete, 400, TEXT("request.bad_request"), TEXT("Query parameters 'layer', 'x' and 'y' (numbers) are required."));
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FString Error;
		bool bNotFound = false;
		if (!LandscapeOps::WeightAt(QueryParam(Request, TEXT("name")), Layer, FCString::Atod(*XText), FCString::Atod(*YText), Out, Error, bNotFound))
		{
			return Fail(OnComplete, bNotFound, Error);
		}
		return SendJson(OnComplete, 200, Out);
	}
}
