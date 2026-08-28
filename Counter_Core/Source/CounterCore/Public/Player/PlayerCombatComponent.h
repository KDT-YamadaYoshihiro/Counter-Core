#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/PlayerTypes.h"
#include "PlayerCombatComponent.generated.h"

class UAnimMontage;
class UCameraShakeBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlayerDamaged, const FPlayerDamageResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPlayerHpChanged, int32, Hp, int32, MaxHp);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPlayerGaugeChanged, int32, Gauge, int32, MaxGauge);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPlayerCombatStateChanged, EPlayerCombatState, OldState, EPlayerCombatState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPlayerCombatSimpleEvent);

/**
 * 仕様書 Player / Battle シートの「ステータス / 被弾 / 攻撃ゲージ / ラッシュ」まわりを
 * まとめたコンポーネント。既存 BP_Player にそのまま Add でき、BP からは関数呼び出しと
 * デリゲート購読で使える。移動・カメラ・ロックオン・近接判定は既存 BP のまま。
 */
UCLASS(ClassGroup = (Player), meta = (BlueprintSpawnableComponent))
class COUNTERCORE_API UPlayerCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerCombatComponent();

	// --- 設定（仕様書 Player シート）---

	/** 最大 HP。仕様: 100。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Status", meta = (ClampMin = "1"))
	int32 MaxHp = 100;

	/** 防御力。仕様: 20。実ダメージ = 敵攻撃力 - 防御力。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Status", meta = (ClampMin = "0"))
	int32 Defence = 20;

	/** 被ダメージ後の無敵時間（秒）。仕様: 「無敵時間: 被ダメージ後」（数値は調整用）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Status", meta = (ClampMin = "0"))
	float PostHitInvulnTime = 0.5f;

	/** 被弾（のけぞり）で行動不能になる時間（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Status", meta = (ClampMin = "0"))
	float HitReactTime = 0.4f;

	/** 被弾モーション（仕様: やられ / 被弾モーション）。代用: MM_HitReact_Front_Med_01_Montage。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|FX")
	TObjectPtr<UAnimMontage> HitReactMontage;

	/** 死亡（敗北）モーション。代用: MM_Death_Front_01_Montage。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|FX")
	TObjectPtr<UAnimMontage> DeathMontage;

	/** 気絶モーション。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|FX")
	TObjectPtr<UAnimMontage> StunMontage;

	/** 被弾時のカメラシェイク。代用: BP_CameraShake_Hit_Player。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|FX")
	TSubclassOf<UCameraShakeBase> DamagedCameraShake;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|FX", meta = (ClampMin = "0", ClampMax = "2"))
	float CameraShakeScale = 0.3f;

	// --- 攻撃ゲージ（仕様書 Player「攻撃」/ UI「攻撃ゲージ」）---

	/** 最大攻撃ゲージ（枠）。仕様: 10 枠。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Gauge", meta = (ClampMin = "1"))
	int32 MaxGauge = 10;

	/** パッシブでゲージが 1 枠増える間隔（秒）。仕様 UI: 「一定時間ごとに1枠獲得」。0 で無効。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Gauge", meta = (ClampMin = "0"))
	float PassiveGaugeInterval = 6.f;

	/** ガード成功時、防いだダメージ何ポイントごとに 1 枠へ変換するか。仕様 Battle:「敵ダメージ量に応じて」。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Gauge", meta = (ClampMin = "1"))
	int32 GuardDamagePerGauge = 15;

	// --- ラッシュ（仕様書 Battle「スタン値100連動」）---

	/** ラッシュ中のプレイヤー与ダメージ倍率。仕様: 1.2。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Rush", meta = (ClampMin = "0"))
	float RushDamageMultiplier = 1.2f;

	// --- 実行時状態 ---

	UPROPERTY(BlueprintReadOnly, Category = "Player|Status")
	int32 Hp = 100;

	UPROPERTY(BlueprintReadOnly, Category = "Player|Gauge")
	int32 Gauge = 0;

	/** ラッシュ中か（敵スタン中）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Player|Rush")
	bool bRushActive = false;

	// --- クエリ ---

	UFUNCTION(BlueprintPure, Category = "Player|Status")
	EPlayerCombatState GetCombatState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Player|Status")
	bool IsAlive() const { return Hp > 0; }

	UFUNCTION(BlueprintPure, Category = "Player|Status")
	bool IsInvulnerable() const { return bInvulnerable || InvulnTimer > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Player|Status")
	float GetHpNormalized() const { return MaxHp > 0 ? FMath::Clamp((float)Hp / MaxHp, 0.f, 1.f) : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Player|Gauge")
	float GetGaugeNormalized() const { return MaxGauge > 0 ? FMath::Clamp((float)Gauge / MaxGauge, 0.f, 1.f) : 0.f; }

	/** ダメージ計算: max(0, AttackPower - Defence)。ラッシュ倍率は与ダメージ側。 */
	UFUNCTION(BlueprintPure, Category = "Player|Status")
	int32 CalculateIncomingDamage(int32 AttackPower) const;

	// --- 実行 ---

	/**
	 * 敵の攻撃を受ける。プレイヤーのやられコリジョンが敵の攻撃コリジョンに触れた瞬間に呼ぶ。
	 * bGuarded=true なら HP ダメージ 0（盾処理は UPlayerGuardComponent 側）。
	 */
	UFUNCTION(BlueprintCallable, Category = "Player|Status")
	FPlayerDamageResult TakeIncomingHit(int32 AttackPower, bool bGuarded);

	/** 外部（無敵時間の付与）。回避のローリング中などに呼ぶ。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Status")
	void SetInvulnerable(bool bNewInvulnerable) { bInvulnerable = bNewInvulnerable; }

	/** 状態遷移。攻撃コンポーネント / ガードコンポーネントから呼ぶ。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Status")
	void SetCombatState(EPlayerCombatState NewState);

	/** 気絶させる（仕様: 盾耐久0 → 10秒。UPlayerGuardComponent から）。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Status")
	void BeginStun(float Duration);

	/** HP を回復（回復アクション用）。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Status")
	void Heal(int32 Amount);

	// --- 攻撃ゲージ ---

	UFUNCTION(BlueprintCallable, Category = "Player|Gauge")
	void AddGauge(int32 Frames);

	/** ガードで防いだダメージをゲージへ変換して加算。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Gauge")
	void AddGaugeFromGuardedDamage(int32 BlockedDamage, float Multiplier = 1.f);

	/** cost 枠を消費できるなら消費して true。攻撃発動判定に使う。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Gauge")
	bool TryConsumeGauge(int32 Cost);

	/** ゲージを最大にする（仕様 Battle: 敵スタン100 でラッシュ突入時）。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Gauge")
	void ForceGaugeMax();

	/** ラッシュの開始 / 終了（敵スタンの監視側から）。開始時にゲージ MAX。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Rush")
	void SetRushActive(bool bActive);

	// --- デリゲート ---

	UPROPERTY(BlueprintAssignable, Category = "Player") FPlayerDamaged OnDamaged;
	UPROPERTY(BlueprintAssignable, Category = "Player") FPlayerHpChanged OnHpChanged;
	UPROPERTY(BlueprintAssignable, Category = "Player") FPlayerGaugeChanged OnGaugeChanged;
	UPROPERTY(BlueprintAssignable, Category = "Player") FPlayerCombatStateChanged OnStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "Player") FPlayerCombatSimpleEvent OnStunned;
	UPROPERTY(BlueprintAssignable, Category = "Player") FPlayerCombatSimpleEvent OnStunRecovered;
	UPROPERTY(BlueprintAssignable, Category = "Player") FPlayerCombatSimpleEvent OnDied;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void SetGauge(int32 NewGauge);
	void EndStun();
	void EndHitReact();
	void PlayMontage(UAnimMontage* Montage) const;
	void PlayDamagedShake() const;

	EPlayerCombatState State = EPlayerCombatState::Normal;

	bool bInvulnerable = false;   // 回避中など、明示的な無敵
	float InvulnTimer = 0.f;      // 被弾後の無敵タイマー
	float HitReactTimer = 0.f;
	float StunTimer = 0.f;
	float PassiveGaugeTimer = 0.f;
};
