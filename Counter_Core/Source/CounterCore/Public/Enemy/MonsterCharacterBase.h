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

	/** これ以上近づいたら移動をやめて攻撃判断に入る距離（cm）。Run DetectionRadius=70 相当。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|AI", meta = (ClampMin = "0"))
	float EngageRange = 300.f;

	/** AI の行動ループ（コンボ ID の並び）。空なら SelectCombo に一任。仕様: 待機→(3)→…→4 のループ。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|AI")
	TArray<FName> ActionLoop;

	/** 移動速度（cm/s）。CharacterMovement の MaxWalkSpeed に反映。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|AI", meta = (ClampMin = "0"))
	float ChaseSpeed = 350.f;

	// --- 代用アニメ（未設定でもロジックは動く）---

	/** 攻撃 ID → 再生するモンタージュ。未設定なら再生しないだけ。代用: Mannequin の攻撃アニメを割り当て。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX")
	TMap<FName, TObjectPtr<UAnimMontage>> AttackMontages;

	/** 状態 → リアクション用モンタージュ（やられ/スタン/死亡）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|FX")
	TMap<EMonsterState, TObjectPtr<UAnimMontage>> ReactionMontages;

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
	void AdvanceCombo();
	int32 CurrentAttackPower() const;

	/** 攻撃判定に使う実体を返す（武器内のシェイプ優先、無ければ内蔵 Hitbox）。 */
	UPrimitiveComponent* ResolveAttackHitbox() const;

	/** BeginPlay で解決した攻撃判定コンポーネント。 */
	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> ActiveHitbox;

	UPROPERTY()
	TObjectPtr<AActor> TargetActor;

	EMonsterState State = EMonsterState::Idle;

	// 進行中コンボ
	TArray<FName> CurrentComboAttacks;
	int32 ComboIndex = 0;
	int32 ActionLoopIndex = 0;

	// Hitstun
	float HitstunTimer = 0.f;
	FVector HitstunKnockbackDir = FVector::ZeroVector;

	// この攻撃で既にヒットさせた相手（多段ヒット防止、判定ONごとにクリア）
	UPROPERTY()
	TSet<TObjectPtr<AActor>> HitActorsThisSwing;
};
