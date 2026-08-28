#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Enemy/MonsterTypes.h"
#include "MonsterCombatComponent.generated.h"

/** 被弾1回の結果。BP 側でエフェクト・ゲージ更新などに使う。 */
USTRUCT(BlueprintType)
struct FMonsterDamageResult
{
	GENERATED_BODY()

	/** 実際に HP から引かれた量。 */
	UPROPERTY(BlueprintReadOnly, Category = "Monster")
	int32 AppliedDamage = 0;

	/** この一撃で死亡したか。 */
	UPROPERTY(BlueprintReadOnly, Category = "Monster")
	bool bWasLethal = false;

	/** この一撃でスタン閾値に到達したか。 */
	UPROPERTY(BlueprintReadOnly, Category = "Monster")
	bool bReachedStun = false;

	/** プレイヤーのガードが成立した一撃だったか（＝やられ）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Monster")
	bool bGuarded = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMonsterStateRequest, EMonsterState, RequestedState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMonsterDamaged, const FMonsterDamageResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMonsterSimpleEvent);

/**
 * 仕様書の「ステータス / スタン / 被ダメージ」まわりの、State グラフに依らない
 * ロジックをまとめたコンポーネント。既存 BP_Enemy にそのまま Add でき、
 * BP からは関数呼び出しとデリゲート購読だけで使える。
 *
 * - ダメージ計算（攻撃力 - 防御力、ラッシュ倍率）
 * - スタン値の蓄積・閾値判定・15秒自然解除
 * - やられ（ガード成立）→ ノックバック + スタン +10 → 復帰
 * - HP0 → 死亡
 * 具体的なアニメ再生・コリジョン・移動は BP 側（デリゲートで受ける）。
 */
UCLASS(ClassGroup = (Monster), meta = (BlueprintSpawnableComponent))
class COUNTERCORE_API UMonsterCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMonsterCombatComponent();

	// --- 設定値（仕様書 Monster / Battle シート）---

	/** スタン継続時間（秒）。仕様: 15秒で自然解除。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Stun", meta = (ClampMin = "0"))
	float StunDuration = 15.f;

	/** やられ（ガード成立）時に加算されるスタン値。仕様: +10。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Stun", meta = (ClampMin = "0"))
	int32 GuardStaggerStunGain = 10;

	/** やられ時の後方ノックバック距離（m）。仕様: 0.2M。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Hitstun", meta = (ClampMin = "0"))
	float HitstunKnockbackM = 0.2f;

	/** やられの硬直時間（秒）。仕様: [0.4s] 硬直終了。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Hitstun", meta = (ClampMin = "0"))
	float HitstunDuration = 0.4f;

	/** プレイヤーの通常被弾でスタン値に加算する量（デザイン調整用）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Stun", meta = (ClampMin = "0"))
	int32 HitStunGain = 0;

	/** ラッシュ状態のダメージ倍率。仕様書 Battle: 1.2。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Battle", meta = (ClampMin = "0"))
	float RushDamageMultiplier = 1.2f;

	// --- 実行時状態 ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Status")
	FMonsterStatus Status;

	/** 現在ラッシュ中か（プレイヤー側が設定）。 */
	UPROPERTY(BlueprintReadWrite, Category = "Monster|Battle")
	bool bTargetInRush = false;

	// --- クエリ ---

	/** 実ダメージ = max(0, AttackPower - 防御力) にラッシュ倍率をかけた値。 */
	UFUNCTION(BlueprintPure, Category = "Monster|Combat")
	int32 CalculateIncomingDamage(int32 AttackPower) const;

	UFUNCTION(BlueprintPure, Category = "Monster|Status")
	bool IsAlive() const { return Status.Hp > 0; }

	UFUNCTION(BlueprintPure, Category = "Monster|Stun")
	bool IsStunThresholdReached() const { return Status.Stun >= Status.MaxStun; }

	UFUNCTION(BlueprintPure, Category = "Monster|Stun")
	float GetStunNormalized() const;

	// --- 実行 ---

	/**
	 * 被弾を処理する。BP の OnWeaponHit / ReceiveAnyDamage から呼ぶ。
	 * bGuardedByPlayer = true なら「やられ」（HP ダメージ 0、スタン +GuardStaggerStunGain、
	 * Hitstun 遷移要求）。false なら HP を引き、必要なら Stun / Dead を要求。
	 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Combat")
	FMonsterDamageResult HandleIncomingHit(int32 AttackPower, bool bGuardedByPlayer);

	/** スタン値を加算し、閾値到達なら Stun 遷移を要求。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Stun")
	void AddStun(int32 Amount);

	/** スタン State の OnEnter から呼ぶ。StunDuration 後に自動で ExitStun。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Stun")
	void BeginStun();

	/** スタン解除（タイマー満了 or 外部から）。スタン値を 0 に戻し Idle を要求。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Stun")
	void EndStun();

	/** HP を全回復（デバッグ / リトライ用）。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Status")
	void ResetStatus();

	// --- デリゲート（BP が購読）---

	/** 状態遷移の要求。BP の ChangeState / RequestStateChange に繋ぐ。 */
	UPROPERTY(BlueprintAssignable, Category = "Monster")
	FMonsterStateRequest OnStateChangeRequested;

	/** 被弾時。エフェクト・カメラシェイク・HP ゲージ更新など。 */
	UPROPERTY(BlueprintAssignable, Category = "Monster")
	FMonsterDamaged OnDamaged;

	/** スタン突入時。 */
	UPROPERTY(BlueprintAssignable, Category = "Monster")
	FMonsterSimpleEvent OnStunned;

	/** スタン解除時。 */
	UPROPERTY(BlueprintAssignable, Category = "Monster")
	FMonsterSimpleEvent OnStunRecovered;

	/** 死亡時。 */
	UPROPERTY(BlueprintAssignable, Category = "Monster")
	FMonsterSimpleEvent OnDied;

protected:
	virtual void BeginPlay() override;

private:
	FTimerHandle StunTimerHandle;
};
