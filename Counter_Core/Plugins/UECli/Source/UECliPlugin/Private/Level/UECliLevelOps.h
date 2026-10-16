// Copyright UE CLI. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class FJsonObject;
class FJsonValue;

/**
 * Level / actor operations against the open editor world. Spawns and deletes go
 * through UEditorActorSubsystem (undoable, level marked dirty); property edits
 * use reflection + PostEditChange.
 */
namespace UECli::LevelOps
{
	/** Every actor in the editor world as { name, label, class, location, rotation, path }. */
	TArray<TSharedPtr<FJsonValue>> ListActors(const FString& ClassFilter, const FString& NameFilter);

	/** Resolve an actor by label, then FName, then object path. */
	AActor* FindActor(const FString& Ref);

	/** Resolve an actor class from a Blueprint path (/Game/BP_X) or a native class name (PointLight). */
	UClass* ResolveActorClass(const FString& Ref, FString& OutError);

	bool SpawnActor(const FString& ClassRef, const FVector& Location, const FRotator& Rotation, const FString& Name,
		TSharedRef<FJsonObject>& OutActor, FString& OutError);

	bool DeleteActor(const FString& Ref, FString& OutError);

	bool SetActorProperty(const FString& Ref, const FString& Property, const FString& Value, FString& OutError);

	bool SetActorTransform(const FString& Ref, const TOptional<FVector>& Location,
		const TOptional<FRotator>& Rotation, const TOptional<FVector>& Scale, FString& OutError);

	/** One actor as JSON. */
	TSharedRef<FJsonObject> ActorToJson(const AActor& Actor);
}
