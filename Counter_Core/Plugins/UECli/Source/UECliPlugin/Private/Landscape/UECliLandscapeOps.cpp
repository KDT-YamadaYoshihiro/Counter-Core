// Copyright UE CLI. All rights reserved.

#include "Landscape/UECliLandscapeOps.h"

#include "Algo/Find.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeSubsystem.h"
#include "LandscapeUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Project/UECliProjectOps.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UECliLandscapeOps"

namespace UECli::LandscapeOps
{
	namespace
	{
		UWorld* EditorWorld()
		{
			return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		}

		ALandscape* FindLandscape(const FString& Ref, FString& OutError)
		{
			UWorld* World = EditorWorld();
			if (!World)
			{
				OutError = TEXT("No editor world.");
				return nullptr;
			}

			TArray<ALandscape*> All;
			for (TActorIterator<ALandscape> It(World); It; ++It)
			{
				All.Add(*It);
			}
			if (Ref.IsEmpty())
			{
				if (All.Num() == 1)
				{
					return All[0];
				}
				OutError = All.Num() == 0
					? FString(TEXT("The open level has no landscape."))
					: FString::Printf(TEXT("The level has %d landscapes; pass its label."), All.Num());
				return nullptr;
			}
			for (ALandscape* Landscape : All)
			{
				if (Landscape->GetActorLabel().Equals(Ref, ESearchCase::IgnoreCase) || Landscape->GetName().Equals(Ref, ESearchCase::IgnoreCase))
				{
					return Landscape;
				}
			}
			OutError = FString::Printf(TEXT("No landscape '%s' in the level."), *Ref);
			return nullptr;
		}

		FGuid HeightLayerGuid(ALandscape& Landscape)
		{
			const ULandscapeEditLayerBase* Layer = Landscape.GetEditLayerConst(0);
			return Layer ? Layer->GetGuid() : FGuid();
		}

		bool ReadVector(const TSharedRef<FJsonObject>& Body, const TCHAR* Field, FVector& Out)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Body->TryGetArrayField(Field, Values))
			{
				return true; // absent: keep the default
			}
			if (Values->Num() != 3)
			{
				return false;
			}
			Out = FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
			return true;
		}

		/** 16-bit grayscale PNG (8-bit is widened) or raw little-endian .r16/.raw (square). */
		bool LoadHeightmap(const FString& File, TArray<uint16>& OutData, int32& OutWidth, int32& OutHeight, FString& OutError)
		{
			FString FullPath;
			if (!UECli::ProjectOps::ResolveImportSource(File, FullPath, OutError))
			{
				return false;
			}
			TArray<uint8> Bytes;
			if (!FFileHelper::LoadFileToArray(Bytes, *FullPath))
			{
				OutError = FString::Printf(TEXT("Could not read '%s'."), *File);
				return false;
			}

			const FString Extension = FPaths::GetExtension(FullPath).ToLower();
			if (Extension == TEXT("png"))
			{
				IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
				const TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
				if (!Png.IsValid() || !Png->SetCompressed(Bytes.GetData(), Bytes.Num()))
				{
					OutError = FString::Printf(TEXT("'%s' is not a readable PNG."), *File);
					return false;
				}
				OutWidth = Png->GetWidth();
				OutHeight = Png->GetHeight();
				TArray64<uint8> Raw;
				if (Png->GetRaw(ERGBFormat::Gray, 16, Raw) && Raw.Num() == int64(OutWidth) * OutHeight * 2)
				{
					OutData.SetNumUninitialized(OutWidth * OutHeight);
					FMemory::Memcpy(OutData.GetData(), Raw.GetData(), Raw.Num());
					return true;
				}
				if (Png->GetRaw(ERGBFormat::Gray, 8, Raw) && Raw.Num() == int64(OutWidth) * OutHeight)
				{
					OutData.SetNumUninitialized(OutWidth * OutHeight);
					for (int32 Index = 0; Index < OutData.Num(); ++Index)
					{
						OutData[Index] = static_cast<uint16>(Raw[Index]) * 257; // 0..255 -> 0..65535
					}
					return true;
				}
				OutError = FString::Printf(TEXT("'%s' could not be decoded as a grayscale heightmap."), *File);
				return false;
			}
			if (Extension == TEXT("r16") || Extension == TEXT("raw"))
			{
				const int32 Samples = Bytes.Num() / 2;
				const int32 Side = FMath::RoundToInt(FMath::Sqrt(static_cast<double>(Samples)));
				if (Bytes.Num() % 2 != 0 || Side * Side != Samples || Side < 2)
				{
					OutError = FString::Printf(TEXT("'%s' is not a square 16-bit raw heightmap (%d bytes)."), *File, Bytes.Num());
					return false;
				}
				OutWidth = OutHeight = Side;
				OutData.SetNumUninitialized(Samples);
				FMemory::Memcpy(OutData.GetData(), Bytes.GetData(), Samples * 2);
				return true;
			}
			OutError = FString::Printf(TEXT("Unsupported heightmap format '.%s' (use a 16-bit grayscale .png or .r16)."), *Extension);
			return false;
		}

		/** Bilinear resample of a heightmap to the landscape's vertex grid. */
		TArray<uint16> Resample(const TArray<uint16>& Source, int32 SourceW, int32 SourceH, int32 TargetW, int32 TargetH)
		{
			TArray<uint16> Result;
			Result.SetNumUninitialized(TargetW * TargetH);
			for (int32 Y = 0; Y < TargetH; ++Y)
			{
				const double SY = TargetH > 1 ? double(Y) * (SourceH - 1) / (TargetH - 1) : 0.0;
				const int32 Y0 = FMath::FloorToInt(SY);
				const int32 Y1 = FMath::Min(Y0 + 1, SourceH - 1);
				const double FY = SY - Y0;
				for (int32 X = 0; X < TargetW; ++X)
				{
					const double SX = TargetW > 1 ? double(X) * (SourceW - 1) / (TargetW - 1) : 0.0;
					const int32 X0 = FMath::FloorToInt(SX);
					const int32 X1 = FMath::Min(X0 + 1, SourceW - 1);
					const double FX = SX - X0;
					const double Top = FMath::Lerp(double(Source[Y0 * SourceW + X0]), double(Source[Y0 * SourceW + X1]), FX);
					const double Bottom = FMath::Lerp(double(Source[Y1 * SourceW + X0]), double(Source[Y1 * SourceW + X1]), FX);
					Result[Y * TargetW + X] = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(Top, Bottom, FY)), 0, 65535));
				}
			}
			return Result;
		}

		struct FExtent
		{
			int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
			int32 Width() const { return MaxX - MinX + 1; }
			int32 Height() const { return MaxY - MinY + 1; }
		};

		bool GetExtent(ALandscape& Landscape, FExtent& Out, FString& OutError)
		{
			ULandscapeInfo* Info = Landscape.GetLandscapeInfo();
			if (!Info || !Info->GetLandscapeExtent(Out.MinX, Out.MinY, Out.MaxX, Out.MaxY))
			{
				OutError = TEXT("The landscape has no components.");
				return false;
			}
			return true;
		}

		double WorldZ(const ALandscape& Landscape, uint16 Height)
		{
			return Landscape.GetActorTransform().TransformPosition(FVector(0, 0, LandscapeDataAccess::GetLocalHeight(Height))).Z;
		}

		void WriteHeights(ALandscape& Landscape, const FExtent& Region, TArray<uint16>& Data)
		{
			FLandscapeEditDataInterface Edit(Landscape.GetLandscapeInfo(), HeightLayerGuid(Landscape));
			Edit.SetHeightData(Region.MinX, Region.MinY, Region.MaxX, Region.MaxY, Data.GetData(), Region.Width(), /*bCalcNormals*/ true);
			Edit.Flush();
			Landscape.RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_Heightmap_All);
		}

		void ReadHeights(ALandscape& Landscape, FExtent& Region, TArray<uint16>& Data)
		{
			Data.SetNumZeroed(Region.Width() * Region.Height());
			FLandscapeEditDataInterface Edit(Landscape.GetLandscapeInfo(), HeightLayerGuid(Landscape));
			Edit.SetShouldDirtyPackage(false); // reading touches (Modify()s) the textures; a read must not dirty the level
			Edit.GetHeightData(Region.MinX, Region.MinY, Region.MaxX, Region.MaxY, Data.GetData(), Region.Width());
		}

		void AddZRange(const ALandscape& Landscape, const TArray<uint16>& Data, const TSharedRef<FJsonObject>& Out)
		{
			uint16 Low = TNumericLimits<uint16>::Max(), High = 0;
			for (const uint16 Height : Data)
			{
				Low = FMath::Min(Low, Height);
				High = FMath::Max(High, Height);
			}
			if (Data.Num() > 0)
			{
				Out->SetNumberField(TEXT("minZ"), WorldZ(Landscape, Low));
				Out->SetNumberField(TEXT("maxZ"), WorldZ(Landscape, High));
			}
		}

		TArray<TSharedPtr<FJsonValue>> Vec(const FVector& V)
		{
			return { MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) };
		}
	}

	bool Describe(const FString& Ref, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		ALandscape* Landscape = FindLandscape(Ref, OutError);
		bOutNotFound = Landscape == nullptr;
		if (!Landscape)
		{
			return false;
		}
		FExtent Extent;
		if (!GetExtent(*Landscape, Extent, OutError))
		{
			return false;
		}

		const int32 QuadsPerComponent = Landscape->ComponentSizeQuads;
		Out->SetStringField(TEXT("name"), Landscape->GetName());
		Out->SetStringField(TEXT("label"), Landscape->GetActorLabel());
		Out->SetArrayField(TEXT("location"), Vec(Landscape->GetActorLocation()));
		Out->SetArrayField(TEXT("scale"), Vec(Landscape->GetActorScale3D()));
		Out->SetArrayField(TEXT("componentCount"), {
			MakeShared<FJsonValueNumber>(QuadsPerComponent > 0 ? (Extent.Width() - 1) / QuadsPerComponent : 0),
			MakeShared<FJsonValueNumber>(QuadsPerComponent > 0 ? (Extent.Height() - 1) / QuadsPerComponent : 0) });
		Out->SetArrayField(TEXT("resolution"), { MakeShared<FJsonValueNumber>(Extent.Width()), MakeShared<FJsonValueNumber>(Extent.Height()) });
		Out->SetNumberField(TEXT("quadsPerSection"), Landscape->SubsectionSizeQuads);
		Out->SetNumberField(TEXT("sectionsPerComponent"), Landscape->NumSubsections);
		if (Landscape->LandscapeMaterial)
		{
			Out->SetStringField(TEXT("material"), Landscape->LandscapeMaterial->GetPathName());
		}
		TArray<uint16> Heights;
		ReadHeights(*Landscape, Extent, Heights);
		AddZRange(*Landscape, Heights, Out);
		return true;
	}

	bool Create(const TSharedRef<FJsonObject>& Body, TSharedRef<FJsonObject>& Out, FString& OutError)
	{
		UWorld* World = EditorWorld();
		if (!World)
		{
			OutError = TEXT("No editor world.");
			return false;
		}

		FVector Location = FVector::ZeroVector;
		FVector RotationPYR = FVector::ZeroVector;
		FVector Scale(100.0, 100.0, 100.0);
		if (!ReadVector(Body, TEXT("location"), Location) || !ReadVector(Body, TEXT("rotation"), RotationPYR) || !ReadVector(Body, TEXT("scale"), Scale))
		{
			OutError = TEXT("location, rotation and scale need exactly 3 numbers.");
			return false;
		}

		double Number = 0;
		const int32 ComponentsX = Body->TryGetNumberField(TEXT("componentsX"), Number) ? static_cast<int32>(Number) : 8;
		const int32 ComponentsY = Body->TryGetNumberField(TEXT("componentsY"), Number) ? static_cast<int32>(Number) : 8;
		const int32 Sections = Body->TryGetNumberField(TEXT("sectionsPerComponent"), Number) ? static_cast<int32>(Number) : 1;
		const int32 QuadsPerSection = Body->TryGetNumberField(TEXT("quadsPerSection"), Number) ? static_cast<int32>(Number) : 63;
		static const int32 ValidQuads[] = { 7, 15, 31, 63, 127, 255 };
		if (ComponentsX < 1 || ComponentsY < 1 || ComponentsX > 32 || ComponentsY > 32)
		{
			OutError = TEXT("componentsX / componentsY must be 1..32.");
			return false;
		}
		if (Sections != 1 && Sections != 2)
		{
			OutError = TEXT("sectionsPerComponent must be 1 or 2.");
			return false;
		}
		if (!Algo::Find(ValidQuads, QuadsPerSection))
		{
			OutError = TEXT("quadsPerSection must be 7, 15, 31, 63, 127 or 255.");
			return false;
		}

		const int32 QuadsPerComponent = Sections * QuadsPerSection;
		const int32 SizeX = ComponentsX * QuadsPerComponent + 1;
		const int32 SizeY = ComponentsY * QuadsPerComponent + 1;

		TArray<uint16> Heights;
		FString Heightmap;
		Body->TryGetStringField(TEXT("heightmap"), Heightmap);
		if (!Heightmap.IsEmpty())
		{
			TArray<uint16> Source;
			int32 SourceW = 0, SourceH = 0;
			if (!LoadHeightmap(Heightmap, Source, SourceW, SourceH, OutError))
			{
				return false;
			}
			Heights = (SourceW == SizeX && SourceH == SizeY) ? MoveTemp(Source) : Resample(Source, SourceW, SourceH, SizeX, SizeY);
		}
		else
		{
			Heights.Init(static_cast<uint16>(LandscapeDataAccess::MidValue), SizeX * SizeY); // flat at the actor's Z
		}

		UMaterialInterface* Material = nullptr;
		FString MaterialPath;
		if (Body->TryGetStringField(TEXT("material"), MaterialPath) && !MaterialPath.IsEmpty())
		{
			Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
			if (!Material)
			{
				OutError = FString::Printf(TEXT("No material at '%s'."), *MaterialPath);
				return false;
			}
		}

		const FScopedTransaction Transaction(LOCTEXT("CreateLandscape", "UE CLI: Create Landscape"));
		const FRotator Rotation(RotationPYR.X, RotationPYR.Y, RotationPYR.Z);
		// Center the landscape on Location, like the editor's New Landscape tool.
		const FVector Offset = FTransform(Rotation, FVector::ZeroVector, Scale).TransformVector(
			FVector(-ComponentsX * QuadsPerComponent / 2.0, -ComponentsY * QuadsPerComponent / 2.0, 0.0));
		ALandscape* Landscape = World->SpawnActor<ALandscape>(Location + Offset, Rotation);
		if (!Landscape)
		{
			OutError = TEXT("Could not spawn a landscape actor.");
			return false;
		}
		Landscape->LandscapeMaterial = Material;
		Landscape->SetActorRelativeScale3D(Scale);
		Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), (uint32)2);

		TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
		HeightDataPerLayers.Add(FGuid(), MoveTemp(Heights));
		TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayers;
		MaterialLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());
		const FString HeightmapPath = Heightmap.IsEmpty() ? FString() : FPaths::ConvertRelativePathToFull(Heightmap);
		Landscape->Import(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1, Sections, QuadsPerSection, HeightDataPerLayers,
			*HeightmapPath, MaterialLayers, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());

		FString Label;
		if (Body->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
		{
			Landscape->SetActorLabel(Label);
		}
		if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
		{
			Info->UpdateLayerInfoMap(Landscape);

			// World Partition maps stream landscapes as grid-sized proxies; split it like the editor's
			// New Landscape tool does (default grid: 2 components).
			ULandscapeSubsystem* Subsystem = World->GetSubsystem<ULandscapeSubsystem>();
			const int32 GridSize = Body->TryGetNumberField(TEXT("gridSize"), Number) ? FMath::Max(1, static_cast<int32>(Number)) : 2;
			if (Subsystem && Subsystem->IsGridBased())
			{
				Subsystem->ChangeGridSize(Info, GridSize);
			}
		}

		bool bNotFound = false;
		return Describe(Landscape->GetActorLabel(), Out, OutError, bNotFound);
	}

	bool ImportHeightmap(const FString& Ref, const FString& File, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		ALandscape* Landscape = FindLandscape(Ref, OutError);
		bOutNotFound = Landscape == nullptr;
		if (!Landscape)
		{
			return false;
		}
		FExtent Extent;
		if (!GetExtent(*Landscape, Extent, OutError))
		{
			return false;
		}
		TArray<uint16> Source;
		int32 SourceW = 0, SourceH = 0;
		if (!LoadHeightmap(File, Source, SourceW, SourceH, OutError))
		{
			return false;
		}
		TArray<uint16> Heights = (SourceW == Extent.Width() && SourceH == Extent.Height())
			? MoveTemp(Source)
			: Resample(Source, SourceW, SourceH, Extent.Width(), Extent.Height());

		const FScopedTransaction Transaction(LOCTEXT("ImportHeightmap", "UE CLI: Import Heightmap"));
		Landscape->Modify();
		WriteHeights(*Landscape, Extent, Heights);

		Out->SetNumberField(TEXT("sourceWidth"), SourceW);
		Out->SetNumberField(TEXT("sourceHeight"), SourceH);
		Out->SetBoolField(TEXT("resampled"), SourceW != Extent.Width() || SourceH != Extent.Height());
		Out->SetArrayField(TEXT("resolution"), { MakeShared<FJsonValueNumber>(Extent.Width()), MakeShared<FJsonValueNumber>(Extent.Height()) });
		AddZRange(*Landscape, Heights, Out);
		return true;
	}

	bool Sculpt(const FString& Ref, const FString& Tool, const FVector2D& Center, double Radius, double Strength,
		TOptional<double> TargetHeight, double Falloff, double Wavelength, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		ALandscape* Landscape = FindLandscape(Ref, OutError);
		bOutNotFound = Landscape == nullptr;
		if (!Landscape)
		{
			return false;
		}
		const FString Mode = Tool.ToLower();
		if (Mode != TEXT("raise") && Mode != TEXT("lower") && Mode != TEXT("flatten") && Mode != TEXT("smooth") && Mode != TEXT("noise"))
		{
			OutError = FString::Printf(TEXT("Unknown sculpt tool '%s' (raise | lower | flatten | smooth | noise)."), *Tool);
			return false;
		}
		if (Radius <= 0)
		{
			OutError = TEXT("radius must be positive.");
			return false;
		}
		if (Mode == TEXT("flatten") && !TargetHeight.IsSet())
		{
			OutError = TEXT("flatten needs height (the target world Z).");
			return false;
		}
		FExtent Extent;
		if (!GetExtent(*Landscape, Extent, OutError))
		{
			return false;
		}

		// World -> landscape vertex space (one unit per quad; Z in local units).
		const FTransform Transform = Landscape->GetActorTransform();
		const FVector LocalCenter = Transform.InverseTransformPosition(FVector(Center.X, Center.Y, 0.0));
		const double ScaleXY = FMath::Max(1e-3, FMath::Abs(Transform.GetScale3D().X));
		const double ScaleZ = FMath::Max(1e-3, FMath::Abs(Transform.GetScale3D().Z));
		const double LocalRadius = Radius / ScaleXY;
		const double Inner = LocalRadius * (1.0 - FMath::Clamp(Falloff, 0.0, 1.0));

		FExtent Region;
		Region.MinX = FMath::Clamp(FMath::FloorToInt(LocalCenter.X - LocalRadius) - 1, Extent.MinX, Extent.MaxX);
		Region.MinY = FMath::Clamp(FMath::FloorToInt(LocalCenter.Y - LocalRadius) - 1, Extent.MinY, Extent.MaxY);
		Region.MaxX = FMath::Clamp(FMath::CeilToInt(LocalCenter.X + LocalRadius) + 1, Extent.MinX, Extent.MaxX);
		Region.MaxY = FMath::Clamp(FMath::CeilToInt(LocalCenter.Y + LocalRadius) + 1, Extent.MinY, Extent.MaxY);
		if (Region.MinX >= Region.MaxX || Region.MinY >= Region.MaxY)
		{
			OutError = TEXT("The brush does not touch the landscape.");
			return false;
		}

		TArray<uint16> Heights;
		ReadHeights(*Landscape, Region, Heights);
		const TArray<uint16> Original = Heights;
		const int32 W = Region.Width();
		auto At = [&](const TArray<uint16>& Data, int32 X, int32 Y) { return double(Data[FMath::Clamp(Y, 0, Region.Height() - 1) * W + FMath::Clamp(X, 0, W - 1)]); };

		// World units -> height units (65536 steps over 512 local units).
		const double HeightPerWorldUnit = 1.0 / (ScaleZ * LANDSCAPE_ZSCALE);
		const double TargetH = TargetHeight.IsSet()
			? LandscapeDataAccess::MidValue + Transform.InverseTransformPosition(FVector(Center.X, Center.Y, TargetHeight.GetValue())).Z / LANDSCAPE_ZSCALE
			: 0.0;

		int32 Touched = 0;
		for (int32 Y = 0; Y < Region.Height(); ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const double Distance = FVector2D(Region.MinX + X - LocalCenter.X, Region.MinY + Y - LocalCenter.Y).Size();
				if (Distance > LocalRadius)
				{
					continue;
				}
				// Full weight inside the inner radius, smooth (cosine) fade to 0 at the edge.
				const double Weight = Distance <= Inner || LocalRadius <= Inner
					? 1.0
					: 0.5 * (1.0 + FMath::Cos(PI * (Distance - Inner) / (LocalRadius - Inner)));
				const double Current = At(Original, X, Y);
				double Next = Current;
				if (Mode == TEXT("raise") || Mode == TEXT("lower"))
				{
					Next = Current + (Mode == TEXT("raise") ? 1.0 : -1.0) * Strength * HeightPerWorldUnit * Weight;
				}
				else if (Mode == TEXT("flatten"))
				{
					Next = FMath::Lerp(Current, TargetH, FMath::Clamp(Strength, 0.0, 1.0) * Weight);
				}
				else if (Mode == TEXT("noise"))
				{
					// Fractal Perlin noise (3 octaves, roughly -1..1) in world space, so strokes tile seamlessly.
					const FVector World = Transform.TransformPosition(FVector(Region.MinX + X, Region.MinY + Y, 0.0));
					const double Cell = Wavelength > 0 ? Wavelength : FMath::Max(1.0, Radius / 4.0);
					double Noise = 0, Amplitude = 1, Frequency = 1, Norm = 0;
					for (int32 Octave = 0; Octave < 3; ++Octave)
					{
						Noise += Amplitude * FMath::PerlinNoise2D(FVector2D(World.X, World.Y) * (Frequency / Cell));
						Norm += Amplitude;
						Amplitude *= 0.5;
						Frequency *= 2.0;
					}
					Next = Current + Strength * HeightPerWorldUnit * Weight * (Noise / Norm);
				}
				else // smooth: blend towards the 3x3 average
				{
					double Sum = 0;
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						for (int32 DX = -1; DX <= 1; ++DX)
						{
							Sum += At(Original, X + DX, Y + DY);
						}
					}
					Next = FMath::Lerp(Current, Sum / 9.0, FMath::Clamp(Strength, 0.0, 1.0) * Weight);
				}
				Heights[Y * W + X] = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(Next), 0, 65535));
				++Touched;
			}
		}

		const FScopedTransaction Transaction(LOCTEXT("SculptLandscape", "UE CLI: Sculpt Landscape"));
		Landscape->Modify();
		WriteHeights(*Landscape, Region, Heights);

		Out->SetStringField(TEXT("tool"), Mode);
		Out->SetNumberField(TEXT("samples"), Touched);
		AddZRange(*Landscape, Heights, Out);
		return true;
	}

	bool Road(const FString& Ref, const TArray<FVector>& Points, double Width, double Falloff, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		ALandscape* Landscape = FindLandscape(Ref, OutError);
		bOutNotFound = Landscape == nullptr;
		if (!Landscape)
		{
			return false;
		}
		if (Width <= 0 || Points.Num() < 2)
		{
			OutError = TEXT("A road needs at least two points and a positive width.");
			return false;
		}
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			if (FVector2D(Points[Index] - Points[Index - 1]).IsNearlyZero())
			{
				OutError = FString::Printf(TEXT("Points %d and %d are at the same X/Y."), Index - 1, Index);
				return false;
			}
		}
		FExtent Extent;
		if (!GetExtent(*Landscape, Extent, OutError))
		{
			return false;
		}
		const FTransform Transform = Landscape->GetActorTransform();
		TArray<FVector> Local;
		for (const FVector& Point : Points)
		{
			Local.Add(Transform.InverseTransformPosition(Point));
		}

		// Through-the-points curve (Catmull-Rom), sampled finely; two points stay a straight line.
		TArray<FVector> Path;
		const int32 Steps = Local.Num() == 2 ? 1 : 16;
		for (int32 Segment = 0; Segment + 1 < Local.Num(); ++Segment)
		{
			const FVector& P0 = Local[FMath::Max(Segment - 1, 0)];
			const FVector& P1 = Local[Segment];
			const FVector& P2 = Local[Segment + 1];
			const FVector& P3 = Local[FMath::Min(Segment + 2, Local.Num() - 1)];
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				const double T = double(Step) / Steps;
				const double T2 = T * T, T3 = T2 * T;
				Path.Add(0.5 * ((2.0 * P1) + (-P0 + P2) * T + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * T2 + (-P0 + 3.0 * P1 - 3.0 * P2 + P3) * T3));
			}
		}
		Path.Add(Local.Last());

		const double ScaleXY = FMath::Max(1e-3, FMath::Abs(Transform.GetScale3D().X));
		const double Half = Width / 2.0 / ScaleXY;
		const double Inner = Half * (1.0 - FMath::Clamp(Falloff, 0.0, 1.0));
		FBox2D Bounds(ForceInit);
		for (const FVector& Point : Path)
		{
			Bounds += FVector2D(Point.X, Point.Y);
		}
		FExtent Region;
		Region.MinX = FMath::Clamp(FMath::FloorToInt(Bounds.Min.X - Half) - 1, Extent.MinX, Extent.MaxX);
		Region.MinY = FMath::Clamp(FMath::FloorToInt(Bounds.Min.Y - Half) - 1, Extent.MinY, Extent.MaxY);
		Region.MaxX = FMath::Clamp(FMath::CeilToInt(Bounds.Max.X + Half) + 1, Extent.MinX, Extent.MaxX);
		Region.MaxY = FMath::Clamp(FMath::CeilToInt(Bounds.Max.Y + Half) + 1, Extent.MinY, Extent.MaxY);
		if (Region.MinX >= Region.MaxX || Region.MinY >= Region.MaxY)
		{
			OutError = TEXT("The road does not touch the landscape.");
			return false;
		}

		TArray<uint16> Heights;
		ReadHeights(*Landscape, Region, Heights);
		const int32 W = Region.Width();
		int32 Touched = 0;
		for (int32 Y = 0; Y < Region.Height(); ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const FVector2D P(Region.MinX + X, Region.MinY + Y);
				double Distance = TNumericLimits<double>::Max(), LocalZ = 0;
				for (int32 Index = 0; Index + 1 < Path.Num(); ++Index)
				{
					const FVector2D A(Path[Index].X, Path[Index].Y), B(Path[Index + 1].X, Path[Index + 1].Y);
					const FVector2D Segment = B - A;
					const double LengthSq = Segment.SizeSquared();
					const double T = LengthSq > 0 ? FMath::Clamp(FVector2D::DotProduct(P - A, Segment) / LengthSq, 0.0, 1.0) : 0.0;
					const double D = (P - (A + Segment * T)).Size();
					if (D < Distance)
					{
						Distance = D;
						LocalZ = FMath::Lerp(Path[Index].Z, Path[Index + 1].Z, T);
					}
				}
				if (Distance > Half)
				{
					continue;
				}
				const double Weight = Distance <= Inner || Half <= Inner
					? 1.0
					: 0.5 * (1.0 + FMath::Cos(PI * (Distance - Inner) / (Half - Inner)));
				const double Target = LandscapeDataAccess::MidValue + LocalZ / LANDSCAPE_ZSCALE;
				uint16& Height = Heights[Y * W + X];
				Height = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(double(Height), Target, Weight)), 0, 65535));
				++Touched;
			}
		}

		const FScopedTransaction Transaction(LOCTEXT("RoadLandscape", "UE CLI: Landscape Road"));
		Landscape->Modify();
		WriteHeights(*Landscape, Region, Heights);
		Out->SetStringField(TEXT("tool"), Points.Num() == 2 ? TEXT("ramp") : TEXT("road"));
		Out->SetNumberField(TEXT("samples"), Touched);
		AddZRange(*Landscape, Heights, Out);
		return true;
	}

	bool Ramp(const FString& Ref, const FVector& From, const FVector& To, double Width, double Falloff, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		if (FVector2D(To - From).IsNearlyZero())
		{
			bOutNotFound = false;
			OutError = TEXT("width must be positive and the two points must differ in X/Y.");
			return false;
		}
		return Road(Ref, { From, To }, Width, Falloff, Out, OutError, bOutNotFound);
	}

	bool Delete(const FString& Ref, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		ALandscape* Landscape = FindLandscape(Ref, OutError);
		bOutNotFound = Landscape == nullptr;
		if (!Landscape)
		{
			return false;
		}

		// Deleting only the ALandscape would leave its streaming proxies (and their components) behind.
		TArray<AActor*> Actors;
		if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
		{
			Info->ForEachLandscapeProxy([&Actors, Landscape](ALandscapeProxy* Proxy)
			{
				if (Proxy && Proxy != Landscape)
				{
					Actors.Add(Proxy);
				}
				return true;
			});
		}
		Actors.Add(Landscape);

		const FScopedTransaction Transaction(LOCTEXT("DeleteLandscape", "UE CLI: Delete Landscape"));
		UWorld* World = Landscape->GetWorld();
		int32 Deleted = 0;
		for (AActor* Actor : Actors)
		{
			if (World && World->EditorDestroyActor(Actor, /*bShouldModifyLevel*/ true))
			{
				++Deleted;
			}
		}
		Out->SetNumberField(TEXT("deleted"), Deleted);
		return true;
	}

	bool HeightAt(const FString& Ref, double X, double Y, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		ALandscape* Landscape = FindLandscape(Ref, OutError);
		bOutNotFound = Landscape == nullptr;
		if (!Landscape)
		{
			return false;
		}
		FExtent Extent;
		if (!GetExtent(*Landscape, Extent, OutError))
		{
			return false;
		}
		const FTransform Transform = Landscape->GetActorTransform();
		const FVector Local = Transform.InverseTransformPosition(FVector(X, Y, 0.0));
		if (Local.X < Extent.MinX || Local.Y < Extent.MinY || Local.X > Extent.MaxX || Local.Y > Extent.MaxY)
		{
			OutError = FString::Printf(TEXT("(%.1f, %.1f) is outside the landscape."), X, Y);
			return false;
		}

		FExtent Cell;
		Cell.MinX = FMath::Clamp(FMath::FloorToInt(Local.X), Extent.MinX, Extent.MaxX - 1);
		Cell.MinY = FMath::Clamp(FMath::FloorToInt(Local.Y), Extent.MinY, Extent.MaxY - 1);
		Cell.MaxX = Cell.MinX + 1;
		Cell.MaxY = Cell.MinY + 1;
		TArray<uint16> Heights;
		ReadHeights(*Landscape, Cell, Heights);
		const double FX = Local.X - Cell.MinX, FY = Local.Y - Cell.MinY;
		const double H = FMath::Lerp(FMath::Lerp(double(Heights[0]), double(Heights[1]), FX), FMath::Lerp(double(Heights[2]), double(Heights[3]), FX), FY);
		const double LocalZ = (H - LandscapeDataAccess::MidValue) * LANDSCAPE_ZSCALE;
		Out->SetNumberField(TEXT("x"), X);
		Out->SetNumberField(TEXT("y"), Y);
		Out->SetNumberField(TEXT("z"), Transform.TransformPosition(FVector(Local.X, Local.Y, LocalZ)).Z);
		return true;
	}

	namespace
	{
		void LayersToJson(const ALandscape& Landscape, const TSharedRef<FJsonObject>& Out)
		{
			TArray<TSharedPtr<FJsonValue>> Layers;
			for (const TPair<FName, FLandscapeTargetLayerSettings>& Pair : Landscape.GetTargetLayers())
			{
				const TSharedRef<FJsonObject> Layer = MakeShared<FJsonObject>();
				Layer->SetStringField(TEXT("name"), Pair.Key.ToString());
				if (Pair.Value.LayerInfoObj)
				{
					Layer->SetStringField(TEXT("layerInfo"), Pair.Value.LayerInfoObj->GetPathName());
				}
				Layers.Add(MakeShared<FJsonValueObject>(Layer));
			}
			Out->SetStringField(TEXT("landscape"), Landscape.GetActorLabel());
			Out->SetArrayField(TEXT("layers"), Layers);
			// The layer names the landscape material blends (LandscapeLayerBlend etc.) — paint shows only on these.
			TArray<TSharedPtr<FJsonValue>> MaterialLayers;
			for (const FName& Name : Landscape.RetrieveTargetLayerNamesFromMaterials())
			{
				MaterialLayers.Add(MakeShared<FJsonValueString>(Name.ToString()));
			}
			Out->SetArrayField(TEXT("materialLayers"), MaterialLayers);
		}

		ULandscapeLayerInfoObject* FindLayerInfo(ALandscape& Landscape, const FString& Layer, FString& OutError)
		{
			for (const TPair<FName, FLandscapeTargetLayerSettings>& Pair : Landscape.GetTargetLayers())
			{
				if (Pair.Key.ToString().Equals(Layer, ESearchCase::IgnoreCase))
				{
					if (!Pair.Value.LayerInfoObj)
					{
						OutError = FString::Printf(TEXT("Layer '%s' has no layer info yet; add it with POST /landscape/layers."), *Layer);
					}
					return Pair.Value.LayerInfoObj;
				}
			}
			TArray<FString> Names;
			for (const TPair<FName, FLandscapeTargetLayerSettings>& Pair : Landscape.GetTargetLayers())
			{
				Names.Add(Pair.Key.ToString());
			}
			OutError = FString::Printf(TEXT("The landscape has no paint layer '%s' (layers: %s)."), *Layer, Names.Num() ? *FString::Join(Names, TEXT(", ")) : TEXT("none"));
			return nullptr;
		}
	}

	bool Export(const FString& Ref, const FString& File, const FString& Layer, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		ALandscape* Landscape = FindLandscape(Ref, OutError);
		bOutNotFound = Landscape == nullptr;
		if (!Landscape)
		{
			return false;
		}
		FExtent Extent;
		if (!GetExtent(*Landscape, Extent, OutError))
		{
			return false;
		}
		const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FString FullPath = FPaths::IsRelative(File) ? FPaths::Combine(ProjectDir, File) : File;
		FullPath = FPaths::ConvertRelativePathToFull(FullPath);
		FPaths::NormalizeFilename(FullPath);
		if (!FPaths::IsUnderDirectory(FullPath, ProjectDir))
		{
			OutError = FString::Printf(TEXT("'%s' is outside the project; export writes only inside it."), *File);
			return false;
		}
		const FString Extension = FPaths::GetExtension(FullPath).ToLower();
		if (Extension != TEXT("png") && Extension != TEXT("r16") && Extension != TEXT("raw"))
		{
			OutError = TEXT("file must end in .png, .r16 or .raw.");
			return false;
		}
		ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPath), /*Tree*/ true);
		if (Layer.IsEmpty())
		{
			Info->ExportHeightmap(FullPath);
		}
		else
		{
			ULandscapeLayerInfoObject* LayerInfo = FindLayerInfo(*Landscape, Layer, OutError);
			if (!LayerInfo)
			{
				bOutNotFound = OutError.StartsWith(TEXT("The landscape has no paint layer"));
				return false;
			}
			Info->ExportLayer(LayerInfo, FullPath);
			Out->SetStringField(TEXT("layer"), LayerInfo->GetLayerName().ToString());
		}
		if (!IFileManager::Get().FileExists(*FullPath))
		{
			OutError = FString::Printf(TEXT("The export did not produce '%s'."), *FullPath);
			return false;
		}
		Out->SetStringField(TEXT("file"), FullPath);
		Out->SetNumberField(TEXT("width"), Extent.Width());
		Out->SetNumberField(TEXT("height"), Extent.Height());
		return true;
	}

	bool ImportLayer(const FString& Ref, const FString& Layer, const FString& File, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		ALandscape* Landscape = FindLandscape(Ref, OutError);
		bOutNotFound = Landscape == nullptr;
		if (!Landscape)
		{
			return false;
		}
		ULandscapeLayerInfoObject* LayerInfo = FindLayerInfo(*Landscape, Layer, OutError);
		if (!LayerInfo)
		{
			bOutNotFound = OutError.StartsWith(TEXT("The landscape has no paint layer"));
			return false;
		}
		FExtent Extent;
		if (!GetExtent(*Landscape, Extent, OutError))
		{
			return false;
		}
		TArray<uint16> Source;
		int32 SourceW = 0, SourceH = 0;
		if (!LoadHeightmap(File, Source, SourceW, SourceH, OutError))
		{
			return false;
		}
		const TArray<uint16> Sized = (SourceW == Extent.Width() && SourceH == Extent.Height())
			? Source
			: Resample(Source, SourceW, SourceH, Extent.Width(), Extent.Height());
		TArray<uint8> Weights;
		Weights.SetNumUninitialized(Sized.Num());
		for (int32 Index = 0; Index < Sized.Num(); ++Index)
		{
			Weights[Index] = static_cast<uint8>(Sized[Index] >> 8); // 8-bit images were widened x257, so this is exact
		}

		const FScopedTransaction Transaction(LOCTEXT("ImportLayer", "UE CLI: Import Landscape Layer"));
		Landscape->Modify();
		FLandscapeEditDataInterface Edit(Landscape->GetLandscapeInfo(), HeightLayerGuid(*Landscape));
		TSet<ULandscapeComponent*> Components;
		if (Edit.GetComponentsInRegion(Extent.MinX, Extent.MinY, Extent.MaxX, Extent.MaxY, &Components))
		{
			for (ULandscapeComponent* Component : Components)
			{
				Component->Modify();
				Component->RequestWeightmapUpdate();
			}
		}
		const bool bWeightBlend = LayerInfo->GetBlendMethod() == ELandscapeTargetLayerBlendMethod::FinalWeightBlending;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Edit.SetAlphaData(LayerInfo, Extent.MinX, Extent.MinY, Extent.MaxX, Extent.MaxY, Weights.GetData(), Extent.Width(),
			ELandscapeLayerPaintingRestriction::None, /*bWeightAdjust*/ bWeightBlend, /*bTotalWeightAdjust*/ false);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		Edit.Flush();
		Landscape->RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_Weightmap_All);

		Out->SetStringField(TEXT("layer"), LayerInfo->GetLayerName().ToString());
		Out->SetNumberField(TEXT("sourceWidth"), SourceW);
		Out->SetNumberField(TEXT("sourceHeight"), SourceH);
		Out->SetBoolField(TEXT("resampled"), SourceW != Extent.Width() || SourceH != Extent.Height());
		return true;
	}

	bool Layers(const FString& Ref, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		ALandscape* Landscape = FindLandscape(Ref, OutError);
		bOutNotFound = Landscape == nullptr;
		if (!Landscape)
		{
			return false;
		}
		LayersToJson(*Landscape, Out);
		return true;
	}

	bool AddLayer(const FString& Ref, const FString& Layer, const FString& Folder, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		ALandscape* Landscape = FindLandscape(Ref, OutError);
		bOutNotFound = Landscape == nullptr;
		if (!Landscape)
		{
			return false;
		}
		if (Layer.IsEmpty() || !FName::IsValidXName(Layer, INVALID_OBJECTNAME_CHARACTERS))
		{
			OutError = FString::Printf(TEXT("'%s' is not a valid layer name."), *Layer);
			return false;
		}
		FString Dir = Folder.IsEmpty() ? FString(TEXT("/Game/Landscape/LayerInfos")) : Folder;
		Dir.RemoveFromEnd(TEXT("/"));
		if (!Dir.StartsWith(TEXT("/Game")))
		{
			OutError = FString::Printf(TEXT("folder must be under /Game (got '%s')."), *Dir);
			return false;
		}

		const FName LayerName(*Layer);
		const FString AssetName = Layer + TEXT("_LayerInfo");
		const FString ObjectPath = Dir + TEXT("/") + AssetName + TEXT(".") + AssetName;

		const FScopedTransaction Transaction(LOCTEXT("AddLandscapeLayer", "UE CLI: Add Landscape Paint Layer"));
		ULandscapeLayerInfoObject* LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (LayerInfo && LayerInfo->GetLayerName() != LayerName)
		{
			OutError = FString::Printf(TEXT("'%s' exists but is for layer '%s'."), *ObjectPath, *LayerInfo->GetLayerName().ToString());
			return false;
		}
		if (!LayerInfo)
		{
			LayerInfo = UE::Landscape::CreateTargetLayerInfo(LayerName, Dir, AssetName);
		}
		if (!LayerInfo)
		{
			OutError = FString::Printf(TEXT("Could not create the layer info asset in '%s'."), *Dir);
			return false;
		}

		Landscape->Modify();
		if (Landscape->HasTargetLayer(LayerName))
		{
			Landscape->UpdateTargetLayer(LayerName, FLandscapeTargetLayerSettings(LayerInfo));
		}
		else
		{
			Landscape->AddTargetLayer(LayerName, FLandscapeTargetLayerSettings(LayerInfo));
		}
		if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
		{
			Info->Modify();
			Info->UpdateLayerInfoMap();
			if (Info->GetLayerInfoIndex(LayerInfo) == INDEX_NONE)
			{
				const int32 ByName = Info->GetLayerInfoIndex(LayerName);
				if (ByName != INDEX_NONE)
				{
					Info->Layers[ByName].LayerInfoObj = LayerInfo;
				}
				Info->CreateTargetLayerSettingsFor(LayerInfo);
			}
		}
		LayersToJson(*Landscape, Out);
		return true;
	}

	bool Paint(const FString& Ref, const FString& Layer, const FString& Tool, const FVector2D& Center, double Radius, double Strength,
		double Falloff, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		ALandscape* Landscape = FindLandscape(Ref, OutError);
		bOutNotFound = Landscape == nullptr;
		if (!Landscape)
		{
			return false;
		}
		const FString Mode = Tool.ToLower();
		if (Mode != TEXT("paint") && Mode != TEXT("erase"))
		{
			OutError = FString::Printf(TEXT("Unknown paint tool '%s' (paint | erase)."), *Tool);
			return false;
		}
		if (Radius <= 0)
		{
			OutError = TEXT("radius must be greater than 0.");
			return false;
		}
		ULandscapeLayerInfoObject* LayerInfo = FindLayerInfo(*Landscape, Layer, OutError);
		if (!LayerInfo)
		{
			bOutNotFound = OutError.StartsWith(TEXT("The landscape has no paint layer"));
			return false;
		}
		FExtent Extent;
		if (!GetExtent(*Landscape, Extent, OutError))
		{
			return false;
		}

		const FTransform Transform = Landscape->GetActorTransform();
		const FVector LocalCenter = Transform.InverseTransformPosition(FVector(Center.X, Center.Y, 0.0));
		const double LocalRadius = Radius / FMath::Max(1e-3, FMath::Abs(Transform.GetScale3D().X));
		const double Inner = LocalRadius * (1.0 - FMath::Clamp(Falloff, 0.0, 1.0));
		FExtent Region;
		Region.MinX = FMath::Clamp(FMath::FloorToInt(LocalCenter.X - LocalRadius) - 1, Extent.MinX, Extent.MaxX);
		Region.MinY = FMath::Clamp(FMath::FloorToInt(LocalCenter.Y - LocalRadius) - 1, Extent.MinY, Extent.MaxY);
		Region.MaxX = FMath::Clamp(FMath::CeilToInt(LocalCenter.X + LocalRadius) + 1, Extent.MinX, Extent.MaxX);
		Region.MaxY = FMath::Clamp(FMath::CeilToInt(LocalCenter.Y + LocalRadius) + 1, Extent.MinY, Extent.MaxY);
		if (Region.MinX >= Region.MaxX || Region.MinY >= Region.MaxY)
		{
			OutError = TEXT("The brush does not touch the landscape.");
			return false;
		}

		const int32 W = Region.Width();
		TArray<uint8> Weights;
		Weights.SetNumZeroed(W * Region.Height());
		const FScopedTransaction Transaction(LOCTEXT("PaintLandscape", "UE CLI: Paint Landscape Layer")); // before the read: reading Modify()s the weightmap textures
		FLandscapeEditDataInterface Edit(Landscape->GetLandscapeInfo(), HeightLayerGuid(*Landscape));
		Edit.GetWeightDataFast(LayerInfo, Region.MinX, Region.MinY, Region.MaxX, Region.MaxY, Weights.GetData(), W);

		const double Target = Mode == TEXT("paint") ? 255.0 : 0.0;
		const double Amount = FMath::Clamp(Strength, 0.0, 1.0);
		int32 Touched = 0;
		for (int32 Y = 0; Y < Region.Height(); ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const double Distance = FVector2D(Region.MinX + X - LocalCenter.X, Region.MinY + Y - LocalCenter.Y).Size();
				if (Distance > LocalRadius)
				{
					continue;
				}
				const double Brush = Distance <= Inner || LocalRadius <= Inner
					? 1.0
					: 0.5 * (1.0 + FMath::Cos(PI * (Distance - Inner) / (LocalRadius - Inner)));
				uint8& Weight = Weights[Y * W + X];
				Weight = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(double(Weight), Target, Amount * Brush)), 0, 255));
				++Touched;
			}
		}

		Landscape->Modify();
		TSet<ULandscapeComponent*> Components;
		if (Edit.GetComponentsInRegion(Region.MinX, Region.MinY, Region.MaxX, Region.MaxY, &Components))
		{
			for (ULandscapeComponent* Component : Components)
			{
				Component->Modify();
				Component->RequestWeightmapUpdate();
			}
		}
		// Same call as the editor's paint tool: weight-blended layers are rebalanced so the weights sum to 1.
		const bool bWeightBlend = LayerInfo->GetBlendMethod() == ELandscapeTargetLayerBlendMethod::FinalWeightBlending;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Edit.SetAlphaData(LayerInfo, Region.MinX, Region.MinY, Region.MaxX, Region.MaxY, Weights.GetData(), W,
			ELandscapeLayerPaintingRestriction::None, /*bWeightAdjust*/ bWeightBlend, /*bTotalWeightAdjust*/ false);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		Edit.Flush();
		Landscape->RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_Weightmap_All);

		Out->SetStringField(TEXT("layer"), LayerInfo->GetLayerName().ToString());
		Out->SetStringField(TEXT("tool"), Mode);
		Out->SetNumberField(TEXT("samples"), Touched);
		return true;
	}

	bool WeightAt(const FString& Ref, const FString& Layer, double X, double Y, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		ALandscape* Landscape = FindLandscape(Ref, OutError);
		bOutNotFound = Landscape == nullptr;
		if (!Landscape)
		{
			return false;
		}
		ULandscapeLayerInfoObject* LayerInfo = FindLayerInfo(*Landscape, Layer, OutError);
		if (!LayerInfo)
		{
			bOutNotFound = OutError.StartsWith(TEXT("The landscape has no paint layer"));
			return false;
		}
		FExtent Extent;
		if (!GetExtent(*Landscape, Extent, OutError))
		{
			return false;
		}
		const FVector Local = Landscape->GetActorTransform().InverseTransformPosition(FVector(X, Y, 0.0));
		const int32 VX = FMath::RoundToInt(Local.X), VY = FMath::RoundToInt(Local.Y);
		if (VX < Extent.MinX || VY < Extent.MinY || VX > Extent.MaxX || VY > Extent.MaxY)
		{
			OutError = FString::Printf(TEXT("(%.1f, %.1f) is outside the landscape."), X, Y);
			return false;
		}
		uint8 Weight = 0;
		FLandscapeEditDataInterface Edit(Landscape->GetLandscapeInfo(), HeightLayerGuid(*Landscape));
		Edit.SetShouldDirtyPackage(false);
		Edit.GetWeightDataFast(LayerInfo, VX, VY, VX, VY, &Weight, 1);
		Out->SetStringField(TEXT("layer"), LayerInfo->GetLayerName().ToString());
		Out->SetNumberField(TEXT("x"), X);
		Out->SetNumberField(TEXT("y"), Y);
		Out->SetNumberField(TEXT("weight"), Weight / 255.0);
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
