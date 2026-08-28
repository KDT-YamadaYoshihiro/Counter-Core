#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
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
	Stun   UMETA(DisplayName = "気絶")
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
	Dodge  UMETA(DisplayName = "回避")
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

	/** 命中時にヒットストップをかけるか。仕様: 小・中 なし / 大 あり。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	bool bHitStop = false;

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
