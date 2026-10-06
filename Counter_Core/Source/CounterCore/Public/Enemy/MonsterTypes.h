#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Common/CombatFeedbackTypes.h"
#include "MonsterTypes.generated.h"

class UAnimMontage;
class UNiagaraSystem;

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
	Dead    UMETA(DisplayName = "死亡"),
	GetUp   UMETA(DisplayName = "立ち上がり")
};

/** 攻撃中の向きを誰が決めるか（PG-15）。 */
UENUM(BlueprintType)
enum class EMonsterRotationAuthority : uint8
{
	Game       UMETA(DisplayName = "ゲーム側（TurnRate で軸合わせ）"),
	RootMotion UMETA(DisplayName = "RootMotion（ゲーム側は回さない）")
};

/** 多段判定の 1 区間（PG-16）。時刻は攻撃開始からの秒。 */
USTRUCT(BlueprintType)
struct FMonsterHitWindow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	float Start = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	float End = 0.f;

	/** この区間の攻撃力。0 = 行の Damage。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	int32 Damage = 0;

	/** true: 前の区間で当てた相手にも再ヒットする。false: この攻撃中に未ヒットの相手だけ。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack")
	bool bAllowRehit = true;
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

	/** この攻撃で再生する Montage。素材差し替えはここ。未設定なら AMonsterCharacterBase::AttackMontages[行名]。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack|Asset")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	/** 判定発生時の斬撃 VFX。未設定なら AMonsterCharacterBase::AttackVFX[行名]。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack|Asset")
	TObjectPtr<UNiagaraSystem> AttackVFX = nullptr;

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

	/** PG-14: WindupEnd Notify で Montage を止めて溜める秒数（0 = 溜めなし）。この間タイムラインも止まる。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	float WindupHoldTime = 0.f;

	/** PG-15: 向きの決定権。RootMotion なら予兆中もゲーム側では回さない（振り返り攻撃など）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack")
	EMonsterRotationAuthority RotationAuthority = EMonsterRotationAuthority::Game;

	/** PG-15: この攻撃中にゲーム側で回頭できる合計角度（deg）。0 = 無制限。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack", meta = (ClampMin = "0"))
	float MaxTrackingAngleDeg = 0.f;

	/** PG-16: 多段判定。空なら HitActiveStart / HitActiveEnd の 1 区間。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack")
	TArray<FMonsterHitWindow> HitWindows;

	/** PG-19: 命中時ヒットストップ。bEnabled=false なら AMonsterCharacterBase::HitStop（既定）を使う。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack")
	FHitStopSettings HitStop = FHitStopSettings{ false, 0.09f, 0.02f, EHitStopScope::Actor };

	/** PG-18: 攻撃開始時の演出（風切り SE / トレイル ON など）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack|Feedback")
	FCombatFeedback SwingFeedback;

	/** PG-18: 命中時の演出。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack|Feedback")
	FCombatFeedback HitFeedback;
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

	/**
	 * 行動パターンでこのコンボが () 付き（条件付き）のとき true。
	 * 仕様: 条件未達なら「移動して間合いを詰める」ことはせず、その場でスキップして次のステップへ。
	 * false のコンボは、まず移動で間合い・角度を満たしてから発生確率判定を行う。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Combo")
	bool bSkipIfConditionUnmet = false;
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
