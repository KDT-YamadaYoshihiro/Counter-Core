// Copyright UE CLI. All rights reserved.

#include "Level/UECliLevelOps.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UECliLog.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UECliLevelOps"

namespace UECli::LevelOps
{
	namespace
	{
		UEditorActorSubsystem* ActorSubsystem()
		{
			return GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
		}

		UWorld* EditorWorld()
		{
			return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		}

		FString VectorToString(const FVector& V)
		{
			return FString::Printf(TEXT("%.1f %.1f %.1f"), V.X, V.Y, V.Z);
		}
	}

	TSharedRef<FJsonObject> ActorToJson(const AActor& Actor)
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Actor.GetName());
		Json->SetStringField(TEXT("label"), Actor.GetActorNameOrLabel());
		Json->SetStringField(TEXT("class"), Actor.GetClass()->GetName());
		Json->SetStringField(TEXT("path"), Actor.GetPathName());

		TArray<TSharedPtr<FJsonValue>> Location;
		Location.Add(MakeShared<FJsonValueNumber>(Actor.GetActorLocation().X));
		Location.Add(MakeShared<FJsonValueNumber>(Actor.GetActorLocation().Y));
		Location.Add(MakeShared<FJsonValueNumber>(Actor.GetActorLocation().Z));
		Json->SetArrayField(TEXT("location"), Location);

		const FRotator Rot = Actor.GetActorRotation();
		TArray<TSharedPtr<FJsonValue>> Rotation;
		Rotation.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
		Rotation.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
		Rotation.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
		Json->SetArrayField(TEXT("rotation"), Rotation);

		return Json;
	}

	TArray<TSharedPtr<FJsonValue>> ListActors(const FString& ClassFilter, const FString& NameFilter)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		UEditorActorSubsystem* Subsystem = ActorSubsystem();
		if (!Subsystem)
		{
			return Result;
		}

		TArray<AActor*> Actors = Subsystem->GetAllLevelActors();
		Actors.Sort([](const AActor& A, const AActor& B) { return A.GetName() < B.GetName(); });

		for (const AActor* Actor : Actors)
		{
			if (!Actor)
			{
				continue;
			}
			if (!ClassFilter.IsEmpty() && !Actor->GetClass()->GetName().Contains(ClassFilter))
			{
				continue;
			}
			if (!NameFilter.IsEmpty() && !Actor->GetName().Contains(NameFilter) && !Actor->GetActorNameOrLabel().Contains(NameFilter))
			{
				continue;
			}
			Result.Add(MakeShared<FJsonValueObject>(ActorToJson(*Actor)));
		}

		return Result;
	}

	AActor* FindActor(const FString& Ref)
	{
		UEditorActorSubsystem* Subsystem = ActorSubsystem();
		if (!Subsystem)
		{
			return nullptr;
		}

		for (AActor* Actor : Subsystem->GetAllLevelActors())
		{
			if (Actor && (Actor->GetActorNameOrLabel() == Ref || Actor->GetName() == Ref || Actor->GetPathName() == Ref))
			{
				return Actor;
			}
		}
		return nullptr;
	}

	UClass* ResolveActorClass(const FString& Ref, FString& OutError)
	{
		UClass* ResolvedClass = nullptr;

		if (Ref.StartsWith(TEXT("/")))
		{
			FString ObjectPath = Ref;
			if (!ObjectPath.Contains(TEXT(".")))
			{
				FString AssetName;
				Ref.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				ObjectPath = Ref + TEXT(".") + AssetName;
			}

			if (UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath))
			{
				ResolvedClass = Blueprint->GeneratedClass;
			}
			else
			{
				ResolvedClass = LoadObject<UClass>(nullptr, *(ObjectPath + TEXT("_C")));
			}
		}
		else
		{
			ResolvedClass = FindFirstObject<UClass>(*Ref, EFindFirstObjectOptions::NativeFirst);
		}

		if (!ResolvedClass)
		{
			OutError = FString::Printf(TEXT("Could not resolve a class from '%s'."), *Ref);
			return nullptr;
		}
		if (!ResolvedClass->IsChildOf(AActor::StaticClass()))
		{
			OutError = FString::Printf(TEXT("'%s' is not an Actor class."), *Ref);
			return nullptr;
		}
		return ResolvedClass;
	}

	bool SpawnActor(const FString& ClassRef, const FVector& Location, const FRotator& Rotation, const FString& Name,
		TSharedRef<FJsonObject>& OutActor, FString& OutError)
	{
		UEditorActorSubsystem* Subsystem = ActorSubsystem();
		if (!Subsystem)
		{
			OutError = TEXT("no editor world");
			return false;
		}

		UClass* ActorClass = ResolveActorClass(ClassRef, OutError);
		if (!ActorClass)
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("SpawnActor", "UE CLI: Spawn Actor"));
		AActor* Spawned = Subsystem->SpawnActorFromClass(ActorClass, Location, Rotation, /*bTransient*/ false);
		if (!Spawned)
		{
			OutError = FString::Printf(TEXT("Failed to spawn '%s'."), *ClassRef);
			return false;
		}

		if (!Name.IsEmpty())
		{
			Spawned->SetActorLabel(Name);
		}

		OutActor = ActorToJson(*Spawned);
		return true;
	}

	bool DeleteActor(const FString& Ref, FString& OutError)
	{
		UEditorActorSubsystem* Subsystem = ActorSubsystem();
		AActor* Actor = FindActor(Ref);
		if (!Subsystem || !Actor)
		{
			OutError = FString::Printf(TEXT("No actor '%s' in the level."), *Ref);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("DeleteActor", "UE CLI: Delete Actor"));
		if (!Subsystem->DestroyActor(Actor))
		{
			OutError = FString::Printf(TEXT("Failed to delete '%s'."), *Ref);
			return false;
		}
		return true;
	}

	bool SetActorProperty(const FString& Ref, const FString& Property, const FString& Value, FString& OutError)
	{
		AActor* Actor = FindActor(Ref);
		if (!Actor)
		{
			OutError = FString::Printf(TEXT("No actor '%s' in the level."), *Ref);
			return false;
		}

		FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*Property));
		if (!Prop)
		{
			// Also try the root component (common for Location/Mobility/etc.).
			if (USceneComponent* Root = Actor->GetRootComponent())
			{
				Prop = Root->GetClass()->FindPropertyByName(FName(*Property));
				if (Prop)
				{
					const FScopedTransaction Transaction(LOCTEXT("SetActorProperty", "UE CLI: Set Actor Property"));
					Root->Modify();
					void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Root);
					if (!Prop->ImportText_Direct(*Value, ValuePtr, Root, PPF_None))
					{
						OutError = FString::Printf(TEXT("Could not parse '%s' as a value for '%s'."), *Value, *Property);
						return false;
					}
					Root->PostEditChange();
					Actor->PostEditChange();
					return true;
				}
			}

			OutError = FString::Printf(TEXT("Class '%s' has no property '%s'."), *Actor->GetClass()->GetName(), *Property);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("SetActorProperty", "UE CLI: Set Actor Property"));
		Actor->Modify();
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
		if (!Prop->ImportText_Direct(*Value, ValuePtr, Actor, PPF_None))
		{
			OutError = FString::Printf(TEXT("Could not parse '%s' as a value for '%s'."), *Value, *Property);
			return false;
		}

		FPropertyChangedEvent ChangedEvent(Prop);
		Actor->PostEditChangeProperty(ChangedEvent);
		return true;
	}

	bool SetActorTransform(const FString& Ref, const TOptional<FVector>& Location,
		const TOptional<FRotator>& Rotation, const TOptional<FVector>& Scale, FString& OutError)
	{
		AActor* Actor = FindActor(Ref);
		if (!Actor)
		{
			OutError = FString::Printf(TEXT("No actor '%s' in the level."), *Ref);
			return false;
		}

		FTransform NewTransform = Actor->GetActorTransform();
		if (Location.IsSet())
		{
			NewTransform.SetLocation(Location.GetValue());
		}
		if (Rotation.IsSet())
		{
			NewTransform.SetRotation(Rotation.GetValue().Quaternion());
		}
		if (Scale.IsSet())
		{
			NewTransform.SetScale3D(Scale.GetValue());
		}

		const FScopedTransaction Transaction(LOCTEXT("SetActorTransform", "UE CLI: Set Actor Transform"));
		Actor->Modify();
		Actor->SetActorTransform(NewTransform);
		Actor->PostEditChange();
		UE_LOG(LogUECli, Verbose, TEXT("moved %s to %s"), *Actor->GetName(), *VectorToString(NewTransform.GetLocation()));
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
