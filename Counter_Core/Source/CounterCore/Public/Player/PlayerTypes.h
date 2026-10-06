#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Common/CombatFeedbackTypes.h"
#include "PlayerTypes.generated.h"

class UAnimMontage;

/**
 * プレイヤーの状態フラグ。仕様書 Player シート「状態フラグ」: 通常 / 攻撃 / 被弾 / 気絶。
 */
UENUM(BlueprintType)
enum class EPlayerCombatState : uint8
{
	Normal UMETA(DisplayName = "通常"),
	Attack UMETA(DisplayName = "攻撃"),
	Hit    UMETA(DisplayName = "被弾"),
	Stun   UMETA(DisplayName = "気絶"),
	Dead   UMETA(DisplayName = "死亡")
};

/**
 * プレイヤーの行動種別。仕様書 Player シート「アクション」。
 * 優先度: 移動 < ガード < 攻撃 < 回避。
 */
UENUM(BlueprintType)
enum class EPlayerActionType : uint8
{
	None   UMETA(DisplayName = "なし"),
	Move   UMETA(DisplayName = "移動"),
	Guard  UMETA(DisplayName = "ガード"),
	Attack UMETA(DisplayName = "攻撃"),
	Dodge  UMETA(DisplayName = "回避"),
	Heal   UMETA(DisplayName = "回復")
};

/** フレーム換算時の端数処理（PG-09）。 */
UENUM(BlueprintType)
enum class EFrameRounding : uint8
{
	Floor UMETA(DisplayName = "切り捨て"),
	Ceil  UMETA(DisplayName = "切り上げ"),
	Round UMETA(DisplayName = "四捨五入")
};

/** 回避で入力が無いときの方向（PG-20）。 */
UENUM(BlueprintType)
enum class EDodgeNoInputDirection : uint8
{
	Backward UMETA(DisplayName = "後方"),
	Forward  UMETA(DisplayName = "前方")
};

/** 回避入力の方向基準（PG-20）。 */
UENUM(BlueprintType)
enum class EDodgeInputBasis : uint8
{
	Camera    UMETA(DisplayName = "カメラ基準"),
	Character UMETA(DisplayName = "キャラクター基準")
};

/** ロックオン中の回避の向き（PG-20）。 */
UENUM(BlueprintType)
enum class ELockOnDodgeFacing : uint8
{
	FaceDodgeDirection UMETA(DisplayName = "回避方向を向く"),
	KeepFacingTarget   UMETA(DisplayName = "ターゲットを向いたまま")
};

/** 回復のコスト（PG-22）。 */
UENUM(BlueprintType)
enum class EHealCostMode : uint8
{
	Potion         UMETA(DisplayName = "回復薬のみ"),
	Gauge          UMETA(DisplayName = "ゲージのみ"),
	PotionAndGauge UMETA(DisplayName = "回復薬＋ゲージ")
};

/**
 * 攻撃の段（小 / 中 / 大）。仕様書 Player シート「攻撃」。
 */
UENUM(BlueprintType)
enum class EPlayerAttackTier : uint8
{
	Small UMETA(DisplayName = "小攻撃"),
	Medium UMETA(DisplayName = "中攻撃"),
	Heavy  UMETA(DisplayName = "大攻撃")
};

/**
 * プレイヤーの攻撃1発分。仕様書 Player シート「攻撃」。行名 = AttackId。
 * 時刻はいずれも「その攻撃の開始からの秒数」。
 */
USTRUCT(BlueprintType)
struct FPlayerAttackRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	FName AttackId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	EPlayerAttackTier Tier = EPlayerAttackTier::Small;

	/** コンボ始動時に消費するゲージ（枠）。派生（2 発目以降）は 0。仕様: 小1 / 中2 / 大4。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack", meta = (ClampMin = "0"))
	int32 GaugeCost = 0;

	/** 攻撃力。実ダメージ = 攻撃力 - 敵防御力。仕様: 小50/55/60・中75/85・大160。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack", meta = (ClampMin = "0"))
	int32 Power = 50;

	/** この一撃が敵に与えるスタン値。仕様: 小5・中15・大50。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack", meta = (ClampMin = "0"))
	int32 StunValue = 5;

	/** 次の派生攻撃の行名（空 = コンボ終了）。仕様: 小1→小2、中1→中2→中3。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	FName NextComboId;

	/** この時刻以降に攻撃入力が入ると次の派生へ（コンボ受付開始）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack", meta = (ClampMin = "0"))
	float ComboWindowStart = 0.3f;

	/** [攻撃判定 ON] 秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack", meta = (ClampMin = "0"))
	float HitActiveStart = 0.15f;

	/** [攻撃判定 OFF] 秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack", meta = (ClampMin = "0"))
	float HitActiveEnd = 0.4f;

	/** [終了] 通常状態へ戻る秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack", meta = (ClampMin = "0"))
	float EndTime = 0.6f;

	/** 命中時のヒットストップ（PG-19）。仕様: 小・中 なし / 大 あり。空振り時はかからない。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	FHitStopSettings HitStop = FHitStopSettings{ false, 0.11f, 0.02f, EHitStopScope::Actor };

	/**
	 * この Montage フレーム以降、コンボ受付済みなら EndTime を待たず次段へ（PG-08）。-1 で無効。
	 * フレームは UPlayerActionComponent::FrameRate で秒換算。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	int32 InterruptibleStartFrame = -1;

	/** Montage の再生速度（PG-09）。0 = EndTime に収まるよう自動調整（従来動作）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack", meta = (ClampMin = "0"))
	float PlayRate = 0.f;

	/** 攻撃開始時の演出（素振り SE / トレイル ON など）。PG-18。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack|Feedback")
	FCombatFeedback SwingFeedback;

	/** 命中時の演出（ヒット SE / VFX / シェイク）。PG-18。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack|Feedback")
	FCombatFeedback HitFeedback;

	/** 被弾で中断できるか。仕様: 大攻撃は「発動後キャンセル不可」。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	bool bCancelable = true;

	/** 再生するモンタージュ（未設定なら再生しないだけ）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	TObjectPtr<UAnimMontage> Montage = nullptr;
};

/** 被弾1回の結果。 */
USTRUCT(BlueprintType)
struct FPlayerDamageResult
{
	GENERATED_BODY()

	/** 実際に HP から引かれた量。 */
	UPROPERTY(BlueprintReadOnly, Category = "Player")
	int32 AppliedDamage = 0;

	/** ガードで防がれたか。 */
	UPROPERTY(BlueprintReadOnly, Category = "Player")
	bool bGuarded = false;

	/** ジャストガードだったか（将来用）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Player")
	bool bJustGuard = false;

	/** 無敵時間などで完全に無効化されたか。 */
	UPROPERTY(BlueprintReadOnly, Category = "Player")
	bool bNullified = false;

	/** この一撃で HP0（敗北）に達したか。 */
	UPROPERTY(BlueprintReadOnly, Category = "Player")
	bool bWasLethal = false;
};
