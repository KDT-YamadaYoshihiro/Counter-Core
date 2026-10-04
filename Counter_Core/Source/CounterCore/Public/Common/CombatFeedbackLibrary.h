#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/WorldSubsystem.h"
#include "Common/CombatFeedbackTypes.h"
#include "CombatFeedbackLibrary.generated.h"

class USceneComponent;

/**
 * ヒットストップの実行管理（T0-1 / T4-2）。実時間で終了させ、
 * 同じ対象への多重発生は「終了時刻を延ばすだけ」にして重ね掛けしない。
 */
UCLASS()
class COUNTERCORE_API UCombatHitStopSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	void Apply(AActor* Target, const FHitStopSettings& Settings);

	UFUNCTION(BlueprintPure, Category = "Combat|HitStop")
	bool IsActorInHitStop(const AActor* Target) const;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

private:
	struct FActorStop
	{
		TWeakObjectPtr<AActor> Actor;
		double EndRealTime = 0.0;
		float TimeScale = 1.f;
	};

	static void SetActorScale(AActor* Actor, float Scale);
	void RestoreAll();

	TArray<FActorStop> ActorStops;
	double GlobalEndRealTime = 0.0;
	float GlobalAppliedScale = 1.f;
	bool bGlobalActive = false;
};

/** 演出（SE / VFX / シェイク / トレイル）とヒットストップの共通入口（T0-1 / T0-2 / T4-1）。 */
UCLASS()
class COUNTERCORE_API UCombatFeedbackLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 演出を再生する。AttachTo があればそこを基準（bAttachNiagara で追従）、無ければ WorldLocation。
	 * トレイルは Source とその子アクター内の TrailComponentTag 付き NiagaraComponent を ON/OFF。
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Feedback", meta = (WorldContext = "WorldContext"))
	static void PlayCombatFeedback(const UObject* WorldContext, const FCombatFeedback& Feedback, AActor* Source,
		USceneComponent* AttachTo, FVector WorldLocation);

	/** Source（と子アクター）内の Tag 付き NiagaraComponent を ON/OFF。 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Feedback")
	static void SetTrailActive(AActor* Source, FName TrailComponentTag, bool bActive);

	/** ヒットストップ。Settings.IsActive() が false なら何もしない。 */
	UFUNCTION(BlueprintCallable, Category = "Combat|HitStop")
	static void ApplyHitStop(AActor* Target, const FHitStopSettings& Settings);
};
