// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

/**
 * Niagara system editing below the asset level — the parts Python cannot reach:
 * emitters in a system, the module stacks of each emitter stage, module inputs,
 * renderers, and compiling. Stages: emitter-spawn | emitter-update | particle-spawn |
 * particle-update. Emitters are addressed by their name in the system, modules by
 * their stack name (e.g. "ScaleColor", "SpawnRate"). Edits mark the system dirty and
 * request a compile; save with the generic asset save.
 * Functions return false with bOutNotFound set when the system / emitter / module is missing.
 */
namespace UECli::NiagaraOps
{
	/** { path, emitters: [{ name, id, enabled, simTarget, renderers: [class], stages: [{ stage, modules: [{ name, script, enabled, index, inputs: [{ name, type, value?, settable, linked }] }] }] }] }. */
	bool Describe(const FString& Path, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** Create an empty system at Path (/Game/...), optionally adding copies of emitter assets. Out = like Describe. */
	bool CreateSystem(const FString& Path, const TArray<FString>& Emitters, TSharedRef<FJsonObject>& Out, FString& OutError);

	/** Add an emitter (copy of an emitter asset / template) to a system. Out = the emitter's summary. */
	bool AddEmitter(const FString& Path, const FString& EmitterAsset, const FString& Name, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	bool RemoveEmitter(const FString& Path, const FString& Emitter, FString& OutError, bool& bOutNotFound);

	/** Add a module script to a stage (Index < 0 = append). Out = the module with its inputs. */
	bool AddModule(const FString& Path, const FString& Emitter, const FString& Stage, const FString& ModuleScript, int32 Index,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	bool RemoveModule(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, FString& OutError, bool& bOutNotFound);

	/** Move a module to Index within its stage (0 = first). Out = the module with its new index. */
	bool MoveModule(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, int32 Index,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	bool SetModuleEnabled(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, bool bEnabled,
		FString& OutError, bool& bOutNotFound);

	/** Set a module input's local value (number, bool, or array for vectors / colors). Out = the input afterwards. */
	bool SetInput(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, const FString& Input,
		const TSharedPtr<FJsonValue>& Value, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/**
	 * Drive a module input with a dynamic input script (e.g. /Niagara/DynamicInputs/Random/UniformRangedFloat).
	 * Refused when the input already has one. Out = the dynamic input as a module ({ name, inputs }); set its
	 * inputs with SetInput using that name as Module.
	 */
	bool SetDynamicInput(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, const FString& Input,
		const FString& Script, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** Drive a module input from a user parameter (User.X, same type). Refused when the input already has a link. Out = the input. */
	bool LinkInput(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, const FString& Input,
		const FString& Parameter, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** Remove an input's override (dynamic input, link or local pin value) so it falls back to its default / local value. Out = the module. */
	bool ResetInput(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, const FString& Input,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** Add a renderer: sprite | mesh | ribbon | light. Out = { emitter, renderers: [class] }. */
	bool AddRenderer(const FString& Path, const FString& Emitter, const FString& Type, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** Add (Type needed: float | int | bool | vector2 | vector3 | vector4 | color | position) or set a user parameter. Out = { name, type, value }. */
	bool SetUserParameter(const FString& Path, const FString& Name, const FString& Type, const TSharedPtr<FJsonValue>& Value,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	bool RemoveUserParameter(const FString& Path, const FString& Name, FString& OutError, bool& bOutNotFound);

	/** Settings: { enabled?, simTarget? (cpu|gpu), localSpace?, determinism? }. Out = the emitter (+ localSpace, determinism). */
	bool SetEmitterSettings(const FString& Path, const FString& Emitter, const TSharedRef<FJsonObject>& Settings, TSharedRef<FJsonObject>& Out,
		FString& OutError, bool& bOutNotFound);

	/** Remove the renderer at Index (order as listed). Out = { emitter, renderers }. */
	bool RemoveRenderer(const FString& Path, const FString& Emitter, int32 Index, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);

	/** Compile and wait. Out = { succeeded, scripts: [{ emitter?, usage, status }] }. */
	bool Compile(const FString& Path, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound);
}
