#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MonsterTypes.generated.h"

/**
 * 敵（モンスター）の状態。仕様書 Monster シート「状態フラグ」:
 * 待機 / 移動 / 攻撃 / やられ / スタン / 死亡。
 * 既存 BP の E_EnemyState と同じ並び。
 */
UENUM(BlueprintType)
enum class EMonsterState : uint8
{
	Idle    UMETA(DisplayName = "待機"),
	Run     UMETA(DisplayName = "移動"),
	Attack  UMETA(DisplayName = "攻撃"),
	Hitstun UMETA(DisplayName = "やられ"),
	Stun    UMETA(DisplayName = "スタン"),
	Dead    UMETA(DisplayName = "死亡")
};

/**
 * 攻撃の1発分。仕様書「攻撃詳細」シートのタイムラインを数値化したもの。
 * 時刻はいずれも「この攻撃の開始からの秒数」。
 */
USTRUCT(BlueprintType)
struct FMonsterAttackFrameData : public FTableRowBase
{
	GENERATED_BODY()

	/** 攻撃の識別名（例: Attack01）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack")
	FName AttackId;

	/** 接触判定に入るプレイヤーとの距離（m）。0 = 距離条件なし。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	float ContactDistanceM = 0.f;

	/** 接触判定に入る角度（正面 0 度からの片側許容角、deg）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0", ClampMax = "180"))
	float ContactAngleDeg = 180.f;

	/** [予兆] 開始からこの秒数まで、TurnRate で軸合わせを続ける。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	float AnticipationTime = 0.f;

	/** [軸合わせ停止] この秒数で回転を止め、攻撃地点を固定する。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	float TurnStopTime = 0.f;

	/** [攻撃判定 ON] 当たり判定を有効化する秒数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	float HitActiveStart = 0.f;

	/** [攻撃判定 OFF] 当たり判定を無効化する秒数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	float HitActiveEnd = 0.f;

	/** この攻撃の攻撃力。実ダメージ = Damage - プレイヤー防御力。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	int32 Damage = 0;

	/** [硬直] 攻撃判定 OFF 後、硬直に入る秒数（＝ HitActiveEnd と同じことが多い）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	float RecoveryStart = 0.f;

	/** [終了] 通常 State に戻る秒数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	float EndTime = 0.f;

	/** 予兆中の軸合わせ速度（deg/秒）。仕様: 通常 180、攻撃5 は 90。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	float TurnRateDegPerSec = 180.f;

	/** true の間は「やられ判定無効」（攻撃5の1段目）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack")
	bool bImmuneToHitstun = false;

	/** true なら、この攻撃はやられ割り込みでも次の攻撃へ遷移しない（攻撃5）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack")
	bool bNoHitstunChain = false;
};

/**
 * コンボ1つ分。仕様書 Monster シート「コンボ」表。
 */
USTRUCT(BlueprintType)
struct FMonsterComboData : public FTableRowBase
{
	GENERATED_BODY()

	/** コンボ識別名（例: Combo0）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Combo")
	FName ComboId;

	/** 構成する攻撃 ID の並び（DT_MonsterAttacks の行名）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Combo")
	TArray<FName> AttackSequence;

	/** 発生確率（%）。0-100。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Combo", meta = (ClampMin = "0", ClampMax = "100"))
	float TriggerChancePercent = 100.f;

	/** 発動に必要なプレイヤーとの最大距離（m）。0 = 距離条件なし。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Combo", meta = (ClampMin = "0"))
	float MaxDistanceM = 0.f;

	/** 発動に必要な最大角度（正面 0 度からの片側、deg）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Combo", meta = (ClampMin = "0", ClampMax = "180"))
	float MaxAngleDeg = 180.f;

	/** true なら「プレイヤーが背後にいるとき」だけ発動（コンボ3）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Combo")
	bool bRequireTargetBehind = false;
};

/**
 * 敵の実行時ステータス。仕様書 Monster シート「ステータス」。
 * 既存 S_EnemyStatus の MaxHP / MaxStun が bool になっているバグを int32 で修正。
 */
USTRUCT(BlueprintType)
struct FMonsterStatus
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Status", meta = (ClampMin = "0"))
	int32 Hp = 500;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Status", meta = (ClampMin = "1"))
	int32 MaxHp = 500;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Status", meta = (ClampMin = "0"))
	int32 Defence = 40;

	/** スタン値 0-MaxStun。MaxStun 到達でスタン。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Status", meta = (ClampMin = "0"))
	int32 Stun = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Status", meta = (ClampMin = "1"))
	int32 MaxStun = 100;
};
