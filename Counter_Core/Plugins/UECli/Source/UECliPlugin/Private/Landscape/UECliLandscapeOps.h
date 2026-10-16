// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Landscape creation, heightmap import and sculpting in the open editor level.
 * Heights are edited in the landscape's first edit layer (UE 5.7 landscapes always
 * use edit layers) and every edit is one undoable transaction. A landscape is
 * referenced by actor label or name; empty = the only landscape in the level.
 * Functions return false with bOutNotFound set when the landscape does not exist.
 */
namespace UECli::LandscapeOps
{
	/**
	 * Body: { location?[3], rotation?[3], scale?[3] (default 100,100,100), componentsX?, componentsY? (default 8),
	 * sectionsPerComponent? (1|2), quadsPerSection? (7|15|31|63|127|255, default 63), heightmap? (16-bit PNG
	 * or .r16, resampled to the landscape size), material?, label?, gridSize? (World Partition streaming proxy
	 * size in components, default 2; ignored outside World Partition) }. Out = landscape info.
	 */
	bool Create(const TSharedRef<FJsonObject>& Body, TSharedRef<FJsonObject>& Out, FString& OutError);

	/** { name, label, location, scale, componentCount[2], resolution[2], quadsPerSection, sectionsPerComponent, minZ, maxZ, material? }. */
	bool Describe(const FString& Ref, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** Replace every height from a 16-bit PNG / .r16 file (resampled to the landscape resolution). */
	bool ImportHeightmap(const FString& Ref, const FString& File, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/**
	 * Brush edit around a world-space XY center. Tool: raise | lower (Strength = world units at the
	 * center), flatten (towards TargetHeight, world Z; Strength 0..1 = blend), smooth (Strength 0..1).
	 * noise (Strength = world-unit amplitude of fractal Perlin noise; Wavelength = world units per noise cell,
	 * 0 = Radius / 4). Falloff 0..1 is the soft outer fraction of the radius. Out = { tool, samples, minZ, maxZ }.
	 */
	bool Sculpt(const FString& Ref, const FString& Tool, const FVector2D& Center, double Radius, double Strength,
		TOptional<double> TargetHeight, double Falloff, double Wavelength, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** A straight ramp / road from From to To (world XYZ): heights along it follow the line, Width wide with a soft Falloff edge. Out = like Sculpt. */
	bool Ramp(const FString& Ref, const FVector& From, const FVector& To, double Width, double Falloff, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** A road through Points (world XYZ, 2+), smoothed with a Catmull-Rom curve: heights follow the curve's Z. Out = like Sculpt. */
	bool Road(const FString& Ref, const TArray<FVector>& Points, double Width, double Falloff, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** Delete a landscape together with its World Partition streaming proxies. Out = { deleted: actor count }. */
	bool Delete(const FString& Ref, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** World Z of the landscape surface at a world XY (from the height data, not collision). */
	bool HeightAt(const FString& Ref, double X, double Y, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/**
	 * Write the landscape's heightmap (or, with Layer, that paint layer's weightmap) to File (.png 16-bit gray for
	 * heights / 8-bit for weights, or .r16 / .raw). File must be inside the project. Out = { file, layer?, width, height }.
	 */
	bool Export(const FString& Ref, const FString& File, const FString& Layer, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** Replace a paint layer's weights from a grayscale image (8/16-bit PNG, .r16; resampled). Out = { layer, sourceWidth, sourceHeight, resampled }. */
	bool ImportLayer(const FString& Ref, const FString& Layer, const FString& File, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** Paint layers of a landscape. Out = { landscape, layers: [{ name, layerInfo? }] }. */
	bool Layers(const FString& Ref, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/**
	 * Add a paint layer: reuses <Folder>/<Layer>_LayerInfo if it exists, else creates that layer info asset
	 * (unsaved), and registers it as a target layer of the landscape. Out = like Layers.
	 */
	bool AddLayer(const FString& Ref, const FString& Layer, const FString& Folder, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/**
	 * Brush-paint a layer's weight around a world XY: Tool paint (towards 1) | erase (towards 0); Strength 0..1;
	 * Falloff like Sculpt. The material decides how weights look. Out = { layer, tool, samples }.
	 */
	bool Paint(const FString& Ref, const FString& Layer, const FString& Tool, const FVector2D& Center, double Radius, double Strength,
		double Falloff, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** A layer's weight (0..1) at the landscape vertex nearest a world XY. Out = { layer, x, y, weight }. */
	bool WeightAt(const FString& Ref, const FString& Layer, double X, double Y, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);
}
