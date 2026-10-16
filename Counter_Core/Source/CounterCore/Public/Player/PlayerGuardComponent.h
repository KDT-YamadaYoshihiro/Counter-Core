#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Common/CombatFeedbackTypes.h"
#include "PlayerGuardComponent.generated.h"

class UPlayerCombatComponent;
class UAnimMontage;
class UAnimSequenceBase;
class UAnimInstance;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlayerGuardStateChanged, bool, bGuarding);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPlayerShieldChanged, float, Durability, float, MaxDurability);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPlayerGuardTimeChanged, float, Remaining, float, Max);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPlayerGuardSimpleEvent);

/**
 * 仕様書 Player シート「ガード」/ Battle「ガード成功時（分岐A）」。
 * 盾耐久・ガード可能時間・クールタイム・自然回復・気絶を管理する。
 * RT ホールドで StartGuard / 離して StopGuard を呼ぶ（入力バインドは UPlayerActionComponent）。
 */
UCLASS(ClassGroup = (Player), meta = (BlueprintSpawnableComponent))
class COUNTERCORE_API UPlayerGuardComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerGuardComponent();

	// --- 設定（仕様書 Player「ガード」。ガード可能時間・自然回復量は調整可能）---

	/** 盾耐久の最大値。仕様: 100。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard", meta = (ClampMin = "1"))
	float MaxShieldDurability = 100.f;

	/** ガード可能時間（秒）。仕様: 10 秒。使い切ると強制解除＋クールタイム。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard", meta = (ClampMin = "0.1"))
	float MaxGuardTime = 10.f;

	/** クールタイム（秒）。仕様: 2 秒（ガード可能時間を使い切った場合のみ発動）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard", meta = (ClampMin = "0"))
	float GuardCooldown = 2.f;

	/** 非ガード時にガード可能時間が回復する速度（秒/秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard", meta = (ClampMin = "0"))
	float GuardTimeRegenPerSec = 2.f;

	/** 盾耐久の自然回復速度（/秒）。仕様: ガード成功1秒後、毎フレーム +2（≈120/秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard", meta = (ClampMin = "0"))
	float ShieldRegenPerSec = 120.f;

	/** ガード成功後、盾耐久の自然回復が始まるまでの遅延（秒）。仕様: 1 秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard", meta = (ClampMin = "0"))
	float ShieldRegenDelay = 1.f;

	/** 盾耐久 0（気絶）の継続時間（秒）。仕様 Player「気絶」: 10 秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard", meta = (ClampMin = "0"))
	float StunDuration = 10.f;

	/** ガード成功時のヒットストップ（PG-06 / PG-19）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard")
	FHitStopSettings GuardHitStop;

	/** ガード成功時に 1 回再生する Montage（PG-06）。未設定なら再生しない。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard")
	TObjectPtr<UAnimMontage> GuardSuccessMontage;

	/** ガード開始時に 1 回再生する Montage。未設定なら即ループへ。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard|Anim")
	TObjectPtr<UAnimMontage> GuardStartMontage;

	/** ガード中にループ再生するアニメ（AnimSequence 可）。開始 Montage / 成功 Montage の再生後に自動で再開。未設定なら再生しない。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard|Anim")
	TObjectPtr<UAnimSequenceBase> GuardLoopAnim;

	/** ガード解除時に 1 回再生する Montage。未設定なら再生しない。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard|Anim")
	TObjectPtr<UAnimMontage> GuardEndMontage;

	/** GuardLoopAnim を流す AnimBP のスロット名（AnimGraph に Slot ノードが必要）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard|Anim")
	FName GuardAnimSlot = TEXT("DefaultSlot");

	/** ガードアニメのブレンド時間（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard|Anim", meta = (ClampMin = "0"))
	float GuardAnimBlendTime = 0.1f;

	/** ガード可能角度（正面 0 度からの片側、deg）。180 = 全方位。範囲外は通常被弾（PG-06）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard", meta = (ClampMin = "0", ClampMax = "180"))
	float GuardableAngleDeg = 180.f;

	/** ガード成功時の演出（SE / VFX / シェイク）。PG-18。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard")
	FCombatFeedback GuardSuccessFeedback;

	/** ジャストガード時に追加で再生する演出。PG-18。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|JustGuard")
	FCombatFeedback JustGuardFeedback;

	// --- ジャストガード（仕様書 優先順位「ジャストガード」）---

	/** ガード開始からこの秒数以内に敵の攻撃を受けたらジャストガード。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|JustGuard", meta = (ClampMin = "0"))
	float JustGuardWindow = 0.2f;

	/** ジャストガード時の攻撃ゲージ上昇倍率（通常ガードの何倍か）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|JustGuard", meta = (ClampMin = "1"))
	float JustGuardGaugeMultiplier = 2.5f;

	/** ジャストガード時に盾耐久の減少を抑える割合（0 = 削られない、1 = 通常どおり）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|JustGuard", meta = (ClampMin = "0", ClampMax = "1"))
	float JustGuardChipScale = 0.25f;

	/** true でガード中は移動速度を 0 にする。仕様: 「移動しながらのガード: 不可」。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard")
	bool bLockMovementWhileGuarding = true;

	/** true でガード中 / クールタイム を画面に文字表示（HUD 未設定でも分かるように）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Guard")
	bool bShowGuardText = true;

	// --- 実行時状態 ---

	UPROPERTY(BlueprintReadOnly, Category = "Player|Guard")
	float ShieldDurability = 100.f;

	UPROPERTY(BlueprintReadOnly, Category = "Player|Guard")
	float GuardTimeRemaining = 10.f;

	// --- クエリ ---

	UFUNCTION(BlueprintPure, Category = "Player|Guard")
	bool IsGuarding() const { return bGuarding; }

	UFUNCTION(BlueprintPure, Category = "Player|Guard")
	bool IsOnCooldown() const { return CooldownTimer > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Player|Guard")
	bool CanStartGuard() const;

	UFUNCTION(BlueprintPure, Category = "Player|Guard")
	float GetShieldNormalized() const { return MaxShieldDurability > 0.f ? FMath::Clamp(ShieldDurability / MaxShieldDurability, 0.f, 1.f) : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Player|Guard")
	float GetGuardTimeNormalized() const { return MaxGuardTime > 0.f ? FMath::Clamp(GuardTimeRemaining / MaxGuardTime, 0.f, 1.f) : 0.f; }

	// --- 実行 ---

	/** ガード開始（RT 押下）。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Guard")
	void StartGuard();

	/** ガード終了（RT 離す）。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Guard")
	void StopGuard();

	/**
	 * ガード中に敵の攻撃を受けたときの処理（仕様 Battle 分岐A）。
	 * 盾耐久から ShieldChipValue を減算し、EnemyAttackPower をゲージへ変換、HP ダメージ 0、ヒットストップ。
	 * 盾耐久 0 で気絶。戻り値: ガードが成立したか（false なら通常被弾で処理すべき）。
	 */
	UFUNCTION(BlueprintCallable, Category = "Player|Guard")
	bool HandleGuardedHit(int32 EnemyAttackPower, int32 ShieldChipValue, bool bJustGuard, AActor* Attacker = nullptr);

	/** Attacker の方向が GuardableAngleDeg 内か（Attacker 未指定なら true）。 */
	UFUNCTION(BlueprintPure, Category = "Player|Guard")
	bool IsWithinGuardAngle(const AActor* Attacker) const;

	// --- デリゲート ---

	UPROPERTY(BlueprintAssignable, Category = "Player|Guard") FPlayerGuardStateChanged OnGuardStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "Player|Guard") FPlayerShieldChanged OnShieldChanged;
	UPROPERTY(BlueprintAssignable, Category = "Player|Guard") FPlayerGuardTimeChanged OnGuardTimeChanged;
	UPROPERTY(BlueprintAssignable, Category = "Player|Guard") FPlayerGuardSimpleEvent OnGuardBroken; // 盾耐久0
	UPROPERTY(BlueprintAssignable, Category = "Player|Guard") FPlayerGuardSimpleEvent OnGuardSuccess;
	UPROPERTY(BlueprintAssignable, Category = "Player|Guard") FPlayerGuardSimpleEvent OnJustGuard;

	/** 直近のガードがジャストガードだったか（HUD 表示用、成功後しばらく true）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Player|Guard")
	float LastJustGuardTime = -100.f;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void SetGuarding(bool bNewGuarding);
	void ForceReleaseWithCooldown();
	UPlayerCombatComponent* GetCombat() const;
	void ApplyGuardHitStop();
	UAnimInstance* GetOwnerAnimInstance() const;
	void UpdateGuardLoopAnim();

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveGuardLoopMontage;

	UPROPERTY()
	TWeakObjectPtr<UPlayerCombatComponent> CachedCombat;

	bool bGuarding = false;
	bool bGuardHeld = false;       // 入力が押されているか（クールタイム明けに再ガードするため）
	float CooldownTimer = 0.f;
	float ShieldRegenDelayTimer = 0.f;
	float SavedMaxWalkSpeed = -1.f;
	float TimeSinceGuardStart = 0.f;
};
