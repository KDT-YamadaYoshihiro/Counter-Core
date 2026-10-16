#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "CombatFeedbackTypes.generated.h"

class USoundBase;
class UNiagaraSystem;
class UCameraShakeBase;

/** ヒットストップをかける範囲。 */
UENUM(BlueprintType)
enum class EHitStopScope : uint8
{
	Actor  UMETA(DisplayName = "対象アクターのみ（Anim / CustomTimeDilation）"),
	Global UMETA(DisplayName = "ワールド全体（GlobalTimeDilation）")
};

/**
 * ヒットストップ設定（T0-1）。プレイヤー攻撃 / モンスター攻撃 / ガード成功で共通。
 * 実行は UCombatFeedbackLibrary::ApplyHitStop（多重発生時は延長のみ・重ね掛けしない）。
 */
USTRUCT(BlueprintType)
struct FHitStopSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HitStop")
	bool bEnabled = true;

	/** 停止する実時間（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HitStop", meta = (ClampMin = "0"))
	float Duration = 0.09f;

	/** 停止中の時間スケール（0 に近いほど完全停止）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HitStop", meta = (ClampMin = "0", ClampMax = "1"))
	float TimeScale = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HitStop")
	EHitStopScope Scope = EHitStopScope::Actor;

	bool IsActive() const { return bEnabled && Duration > 0.f; }
};

/** 演出イベントでのトレイル操作。 */
UENUM(BlueprintType)
enum class ETrailAction : uint8
{
	None  UMETA(DisplayName = "変更なし"),
	Start UMETA(DisplayName = "ON"),
	Stop  UMETA(DisplayName = "OFF")
};

/**
 * 演出イベント1回分（T0-2）: SE / Niagara / カメラシェイク / トレイル ON-OFF。
 * DataTable 行やコンポーネントの UPROPERTY に置き、UCombatFeedbackLibrary::PlayCombatFeedback で再生する。
 * 未設定の項目は何もしない。
 */
USTRUCT(BlueprintType)
struct FCombatFeedback
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Sound")
	TObjectPtr<USoundBase> Sound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Sound", meta = (ClampMin = "0"))
	float VolumeMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Sound", meta = (ClampMin = "0.01"))
	float PitchMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|VFX")
	TObjectPtr<UNiagaraSystem> Niagara = nullptr;

	/** true ならアタッチ先コンポーネントに追従、false ならその場にスポーン。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|VFX")
	bool bAttachNiagara = false;

	/** アタッチ先のソケット名（空 = コンポーネント原点）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|VFX")
	FName AttachSocket = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|VFX")
	FVector LocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|VFX")
	FRotator RotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|VFX")
	FVector Scale = FVector::OneVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Camera")
	TSubclassOf<UCameraShakeBase> CameraShake;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Camera", meta = (ClampMin = "0", ClampMax = "2"))
	float CameraShakeScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Trail")
	ETrailAction Trail = ETrailAction::None;

	/** トレイルとして ON/OFF する NiagaraComponent のコンポーネントタグ（武器アクター内も探す）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feedback|Trail")
	FName TrailComponentTag = FName(TEXT("Trail"));
};

/**
 * AnimNotify_CombatEvent の EventName と照合する名前（T0-3）。
 * Montage 側の Notify 名に合わせてエディタで変更できる。
 */
USTRUCT(BlueprintType)
struct FCombatEventNames
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatEvent")
	FName HitStart = FName(TEXT("HitStart"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatEvent")
	FName HitEnd = FName(TEXT("HitEnd"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatEvent")
	FName AttackEnd = FName(TEXT("AttackEnd"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatEvent")
	FName DeathEnd = FName(TEXT("DeathEnd"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatEvent")
	FName WindupEnd = FName(TEXT("WindupEnd"));
};
