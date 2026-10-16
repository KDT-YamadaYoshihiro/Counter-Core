#include "Common/CombatFeedbackLibrary.h"

#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/ChildActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

// ---------------------------------------------------------------------------
// UCombatHitStopSubsystem
// ---------------------------------------------------------------------------

void UCombatHitStopSubsystem::SetActorScale(AActor* Actor, float Scale)
{
	if (!Actor)
	{
		return;
	}
	Actor->CustomTimeDilation = Scale;
	TArray<USkeletalMeshComponent*> Meshes;
	Actor->GetComponents<USkeletalMeshComponent>(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		Mesh->GlobalAnimRateScale = Scale;
	}
}

void UCombatHitStopSubsystem::Apply(AActor* Target, const FHitStopSettings& Settings)
{
	if (!Settings.IsActive())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const double End = Now + Settings.Duration;
	const float Scale = FMath::Clamp(Settings.TimeScale, 0.f, 1.f);

	if (Settings.Scope == EHitStopScope::Global)
	{
		UWorld* World = GetWorld();
		if (!World)
		{
			return;
		}
		// 多重発生: 延長のみ。既に止まっていればスケールは弱い方（より止まる方）を維持。
		GlobalEndRealTime = FMath::Max(GlobalEndRealTime, End);
		GlobalAppliedScale = bGlobalActive ? FMath::Min(GlobalAppliedScale, Scale) : Scale;
		bGlobalActive = true;
		UGameplayStatics::SetGlobalTimeDilation(World, FMath::Max(GlobalAppliedScale, 0.0001f));
		return;
	}

	if (!Target)
	{
		return;
	}
	for (FActorStop& S : ActorStops)
	{
		if (S.Actor.Get() == Target)
		{
			S.EndRealTime = FMath::Max(S.EndRealTime, End);
			S.TimeScale = FMath::Min(S.TimeScale, Scale);
			SetActorScale(Target, S.TimeScale);
			return;
		}
	}
	FActorStop& New = ActorStops.AddDefaulted_GetRef();
	New.Actor = Target;
	New.EndRealTime = End;
	New.TimeScale = Scale;
	SetActorScale(Target, Scale);
}

bool UCombatHitStopSubsystem::IsActorInHitStop(const AActor* Target) const
{
	for (const FActorStop& S : ActorStops)
	{
		if (S.Actor.Get() == Target)
		{
			return true;
		}
	}
	return false;
}

void UCombatHitStopSubsystem::Tick(float /*DeltaTime*/)
{
	const double Now = FPlatformTime::Seconds();
	for (int32 i = ActorStops.Num() - 1; i >= 0; --i)
	{
		if (!ActorStops[i].Actor.IsValid())
		{
			ActorStops.RemoveAtSwap(i);
			continue;
		}
		if (Now >= ActorStops[i].EndRealTime)
		{
			SetActorScale(ActorStops[i].Actor.Get(), 1.f);
			ActorStops.RemoveAtSwap(i);
		}
	}
	if (bGlobalActive && Now >= GlobalEndRealTime)
	{
		bGlobalActive = false;
		if (UWorld* World = GetWorld())
		{
			// メニュー等が別のスケールを設定していたら触らない。
			if (FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(World), FMath::Max(GlobalAppliedScale, 0.0001f), 0.0005f))
			{
				UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
			}
		}
	}
}

void UCombatHitStopSubsystem::RestoreAll()
{
	for (FActorStop& S : ActorStops)
	{
		SetActorScale(S.Actor.Get(), 1.f);
	}
	ActorStops.Reset();
	bGlobalActive = false;
}

void UCombatHitStopSubsystem::Deinitialize()
{
	RestoreAll();
	Super::Deinitialize();
}

TStatId UCombatHitStopSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCombatHitStopSubsystem, STATGROUP_Tickables);
}

// ---------------------------------------------------------------------------
// UCombatFeedbackLibrary
// ---------------------------------------------------------------------------

void UCombatFeedbackLibrary::ApplyHitStop(AActor* Target, const FHitStopSettings& Settings)
{
	if (!Settings.IsActive() || !Target)
	{
		return;
	}
	if (UWorld* World = Target->GetWorld())
	{
		if (UCombatHitStopSubsystem* Sub = World->GetSubsystem<UCombatHitStopSubsystem>())
		{
			Sub->Apply(Target, Settings);
		}
	}
}

void UCombatFeedbackLibrary::SetTrailActive(AActor* Source, FName TrailComponentTag, bool bActive)
{
	if (!Source || TrailComponentTag.IsNone())
	{
		return;
	}
	TArray<AActor*> Actors;
	Actors.Add(Source);
	TArray<UChildActorComponent*> Children;
	Source->GetComponents<UChildActorComponent>(Children);
	for (UChildActorComponent* C : Children)
	{
		if (C && C->GetChildActor())
		{
			Actors.Add(C->GetChildActor());
		}
	}
	for (AActor* A : Actors)
	{
		TArray<UNiagaraComponent*> Comps;
		A->GetComponents<UNiagaraComponent>(Comps);
		for (UNiagaraComponent* N : Comps)
		{
			if (N && N->ComponentHasTag(TrailComponentTag))
			{
				if (bActive)
				{
					N->Activate(true);
				}
				else
				{
					N->Deactivate();
				}
			}
		}
	}
}

void UCombatFeedbackLibrary::PlayCombatFeedback(const UObject* WorldContext, const FCombatFeedback& Feedback,
	AActor* Source, USceneComponent* AttachTo, FVector WorldLocation)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return;
	}

	FVector Loc = WorldLocation;
	FRotator Rot = FRotator::ZeroRotator;
	if (AttachTo)
	{
		const FTransform T = AttachTo->GetSocketTransform(Feedback.AttachSocket);
		Loc = T.GetLocation();
		Rot = T.Rotator();
	}

	if (Feedback.Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(World, Feedback.Sound, Loc, Feedback.VolumeMultiplier, Feedback.PitchMultiplier);
	}

	if (Feedback.Niagara)
	{
		if (AttachTo && Feedback.bAttachNiagara)
		{
			UNiagaraComponent* N = UNiagaraFunctionLibrary::SpawnSystemAttached(Feedback.Niagara, AttachTo, Feedback.AttachSocket,
				Feedback.LocationOffset, Feedback.RotationOffset, EAttachLocation::KeepRelativeOffset, true);
			if (N)
			{
				N->SetRelativeScale3D(Feedback.Scale);
			}
		}
		else
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Feedback.Niagara,
				Loc + Rot.RotateVector(Feedback.LocationOffset), Rot + Feedback.RotationOffset, Feedback.Scale, true);
		}
	}

	if (Feedback.CameraShake && Feedback.CameraShakeScale > 0.f)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->StartCameraShake(Feedback.CameraShake, Feedback.CameraShakeScale);
			}
		}
	}

	if (Feedback.Trail != ETrailAction::None)
	{
		SetTrailActive(Source, Feedback.TrailComponentTag, Feedback.Trail == ETrailAction::Start);
	}
}
