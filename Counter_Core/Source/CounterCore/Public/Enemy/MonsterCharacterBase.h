#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Enemy/MonsterTypes.h"
#include "MonsterCharacterBase.generated.h"

class UMonsterCombatComponent;
class UMonsterAttackComponent;
class UBoxComponent;
class UAnimMontage;
class UPrimitiveComponent;
class UChildActorComponent;
class UShapeComponent;
class UCameraShakeBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMonsterStateChanged, EMonsterState, OldState, EMonsterState, NewState);

/**
 * 仕様書（Monster / 攻撃詳細）どおりのボス敵の C++ 実装。
 *
 * 内蔵の軽量ステートマシン（EMonsterState）で 待機/移動/攻撃/やられ/スタン/死亡 を回し、
 * ロジックの実体は UMonsterCombatComponent（被ダメージ・スタン・死亡）と
 * UMonsterAttackComponent（コンボ選択・攻撃タイムライン）に委譲する。
 *
 * アニメ・コリジョン実体・VFX は BlueprintNativeEvent / デリゲートで BP 側へ。
 * BP_Enemy をこのクラスに reparent して使うか、コンポーネントだけ流用する。
 */
UCLASS(Blueprintable)
class COUNTERCORE_API AMonsterCharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	AMonsterCharacterBase();

	// --- 設定 ---

	/** プレイヤーを発見する距離（cm）。仕様の Idle DetectionRadius=1000 相当。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|AI", meta = (ClampMin = "0"))
	float DetectionRange = 1000.f;

	/** 行動ループ内でどのコンボの条件も満たせないときに前進を続ける下限距離（cm）。到達しても攻撃条件次第。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|AI", meta = (ClampMin = "0"))
	float EngageRange = 300.f;

	/**
	 * AI の行動ループ（コンボ ID を仕様書の並び順どおりに）。
	 * 仕様: 待機→(3)→移動→1→移動→1→移動→0→移動→1→移動→2→(3)→移動→0→移動→1→移動→(3)→移動→4。
	 * () 付き（DT_MonsterCombos の bSkipIfConditionUnmet=true）は条件未達ならその場でスキップ。
	 * それ以外は移動で間合いを詰めてから発生確率判定。空なら攻撃しない。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|AI")
	TArray<FName> ActionLoop;

	/** 行動ループが一周したときに待機で挟む秒数（仕様の「待機」）。0 で無し。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|AI", meta = (ClampMin = "0"))
	float LoopRestTime = 0.5f;

	/** true で行動ステップ（どのコンボを評価中か・スキップ・発生確率）を画面に Print String 表示。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Debug")
	bool bPrintAIEvents = true;

	/** true でプレイヤー0のキー U=スタン / I=やられ / O=死亡 でこの敵のステートを強制発火。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Debug")
	bool bEnableDebugKeys = true;

	/** 移動速度（cm/s）。CharacterMovement の MaxWalkSpeed に反映。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|AI", meta = (ClampMin = "0"))
	float ChaseSpeed = 350.f;

	// --- 代用アニメ（未設定でもロジックは動く）---

	/** 攻撃 ID → 再生するモンタージュ。未設定なら再生しないだけ。代用: Mannequin の攻撃アニメを割り当て。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX")
	TMap<FName, TObjectPtr<UAnimMontage>> AttackMontages;

	/** 状態 → リアクション用モンタージュ（やられ/スタン/死亡）。未設定なら再生しないだけ。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX")
	TMap<EMonsterState, TObjectPtr<UAnimMontage>> ReactionMontages;

	/** 死亡時、死亡モンタージュの代わりにメッシュをラグドール化する（物理アセットが必要）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX")
	bool bRagdollOnDeath = true;

	/** 攻撃ヒット / 被弾 / やられ 時にヒットストップ（自分の時間を数フレーム止める）。仕様書 Battle。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX")
	bool bHitStopEnabled = true;

	/** ヒットストップの実時間（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX", meta = (ClampMin = "0"))
	float HitStopDuration = 0.09f;

	/** ヒットストップ中の時間スケール（0 に近いほど完全停止）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX", meta = (ClampMin = "0", ClampMax = "1"))
	float HitStopTimeScale = 0.02f;

	/** 敵の攻撃がプレイヤーに当たったときのカメラシェイク。仕様書 Battle。代用: BP_CameraShake_Hit_Player。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX")
	TSubclassOf<UCameraShakeBase> AttackHitCameraShake;

	/** 敵が被弾・やられたときのカメラシェイク。代用: BP_CameraShake_Hit_Enemy。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX")
	TSubclassOf<UCameraShakeBase> DamagedCameraShake;

	/** カメラシェイクの最大強度になる距離（cm）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX", meta = (ClampMin = "0"))
	float CameraShakeInnerRadius = 2000.f;

	/** カメラシェイクが届く距離（cm）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX", meta = (ClampMin = "0"))
	float CameraShakeOuterRadius = 10000.f;

	/** 攻撃判定ボックスの大きさ（武器を使わない場合のフォールバック用）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX")
	FVector HitboxExtent = FVector(60.f, 60.f, 60.f);

	/** 内蔵フォールバック攻撃判定をアタッチするソケット（空ならメッシュ原点）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX")
	FName HitboxSocket = NAME_None;

	/** 手に持たせる武器アクター（例: BP_Weapon）。設定すると武器内の判定ボックスを攻撃判定として使う。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX")
	TSubclassOf<AActor> WeaponClass;

	/** 武器をアタッチするメッシュのソケット / ボーン名。UE5 マネキンなら "hand_r"。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX")
	FName WeaponSocket = FName("hand_r");

	// --- コンポーネント ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Monster")
	TObjectPtr<UMonsterCombatComponent> Combat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Monster")
	TObjectPtr<UMonsterAttackComponent> Attack;

	/** 内蔵フォールバック攻撃判定ボックス。武器未設定時のみ使用。HitActive 中だけ Overlap 有効。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Monster")
	TObjectPtr<UBoxComponent> Hitbox;

	/** 手に持たせた武器（WeaponClass から生成）。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Monster")
	TObjectPtr<UChildActorComponent> WeaponActor;

	// --- ステート ---

	UFUNCTION(BlueprintPure, Category = "Monster|State")
	EMonsterState GetMonsterState() const { return State; }

	/** 状態遷移。優先度が低い遷移は無視される（Dead > Stun > Hitstun > Attack > Run > Idle）。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|State")
	void RequestState(EMonsterState NewState);

	/** 優先度を無視して強制遷移。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|State")
	void ForceState(EMonsterState NewState);

	UPROPERTY(BlueprintAssignable, Category = "Monster|State")
	FMonsterStateChanged OnStateChanged;

	// --- ターゲット ---

	UFUNCTION(BlueprintCallable, Category = "Monster")
	void SetTarget(AActor* InTarget);

	UFUNCTION(BlueprintPure, Category = "Monster")
	AActor* GetTarget() const { return TargetActor; }

	UFUNCTION(BlueprintPure, Category = "Monster")
	float GetDistanceToTargetCm() const;

	/** 正面 0 度、右+ / 左- の符号付き角度（deg）。 */
	UFUNCTION(BlueprintPure, Category = "Monster")
	float GetSignedAngleToTargetDeg() const;

	// --- 見た目フック（C++ 既定あり。BP で override 可能）---

	/** アニメモンタージュ再生。既定: AttackMontages[AttackId] を再生。 */
	UFUNCTION(BlueprintNativeEvent, Category = "Monster|FX")
	void PlayAttackMontage(FName AttackId);
	virtual void PlayAttackMontage_Implementation(FName AttackId);

	/** リアクション再生。既定: ReactionMontages[NewState] を再生。 */
	UFUNCTION(BlueprintNativeEvent, Category = "Monster|FX")
	void PlayReaction(EMonsterState NewState);
	virtual void PlayReaction_Implementation(EMonsterState NewState);

	/** プレイヤーへダメージを与える。既定: 現在の攻撃 Damage で ApplyDamage。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Combat")
	void DealDamageToTarget(int32 AttackPower);

	/** ヒットストップを発火（自分の CustomTimeDilation とアニメ速度を一時的に落とす）。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|FX")
	void ApplyHitStop();

	// --- デバッグ発火（キー U / I / O、または BP から直接）---

	UFUNCTION(BlueprintCallable, Category = "Monster|Debug")
	void DebugTriggerStun();

	UFUNCTION(BlueprintCallable, Category = "Monster|Debug")
	void DebugTriggerHitstun();

	UFUNCTION(BlueprintCallable, Category = "Monster|Debug")
	void DebugTriggerDead();

	/** K 相当: ガード成功の被弾をシミュレート。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Debug")
	void DebugGuardedHit();

	/** L 相当: プレイヤーの通常攻撃ヒット（HP ダメージ + スタン蓄積）をシミュレート。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Debug")
	void DebugPlayerHit();

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator,
		AActor* DamageCauser) override;

	// ステート実装
	void EnterState(EMonsterState NewState);
	void TickIdle(float Dt);
	void TickRun(float Dt);
	void TickAttack(float Dt);
	void TickHitstun(float Dt);

	// 行動パターン（仕様書どおり順番に実行）
	void BeginActionStep();          // ActionLoop[ActionLoopIndex] を評価
	void AdvanceActionStep();        // 次のステップへ（末尾なら先頭に戻り待機を挟む）
	void StartCurrentCombo();        // 現在ステップのコンボで攻撃 State へ
	void LaunchNextAttackInCombo();  // コンボ内の次の一手を撃つ
	void ResumeAfterHitstun();       // やられ硬直明けの分岐（攻撃5は連鎖しない）
	bool EvaluateComboCondition(const FMonsterComboData& C) const; // 距離・角度・背後
	bool RollComboProbability(const FMonsterComboData& C) const;
	void PrintAI(const FString& Msg, const FColor& Color) const;
	void PollDebugKeys(); // U / I / O / K / L
	void EndHitStop();
	void PlayCameraShake(TSubclassOf<UCameraShakeBase> ShakeClass) const;

	FTimerHandle HitStopTimerHandle;

	// 攻撃コンポーネントのイベント
	UFUNCTION()
	void HandleAttackFinished();
	UFUNCTION()
	void HandleCombatStateRequest(EMonsterState Requested);
	UFUNCTION()
	void HandleToggleHitbox(bool bEnable);
	UFUNCTION()
	void HandlePlayAttackAnim(FName AttackId);

	UFUNCTION()
	void OnHitboxOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

private:
	static int32 StatePriority(EMonsterState S);
	int32 CurrentAttackPower() const;

	/** 攻撃判定に使う実体を返す（武器内のシェイプ優先、無ければ内蔵 Hitbox）。 */
	UPrimitiveComponent* ResolveAttackHitbox() const;

	/** BeginPlay で解決した攻撃判定コンポーネント。 */
	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> ActiveHitbox;

	UPROPERTY()
	TObjectPtr<AActor> TargetActor;

	EMonsterState State = EMonsterState::Idle;

	// 行動パターンの現在位置
	int32 ActionLoopIndex = 0;
	FName CurrentComboId = NAME_None;
	FMonsterComboData CurrentComboData;
	bool bMovingToEngageCombo = false; // Run 中: 現在コンボの間合いを詰めている
	float LoopRestTimer = 0.f;         // 一周後の待機

	// 進行中コンボ
	TArray<FName> CurrentComboAttacks;
	int32 ComboIndex = 0;

	// Hitstun
	float HitstunTimer = 0.f;
	FVector HitstunKnockbackDir = FVector::ZeroVector;
	bool bInterruptedAttackNoChain = false; // 中断された攻撃が「やられ連鎖しない」（攻撃5）

	// この攻撃で既にヒットさせた相手（多段ヒット防止、判定ONごとにクリア）
	UPROPERTY()
	TSet<TObjectPtr<AActor>> HitActorsThisSwing;
};
