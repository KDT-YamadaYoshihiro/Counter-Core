#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/PlayerTypes.h"
#include "PlayerActionComponent.generated.h"

class UPlayerCombatComponent;
class UPlayerGuardComponent;
class UInputAction;
class UInputMappingContext;
class UPrimitiveComponent;
class UAnimMontage;
struct FInputActionValue;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlayerActionChanged, EPlayerActionType, NewAction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlayerAttackStarted, FName, AttackId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPlayerActionSimpleEvent);

/**
 * 仕様書 Player シート「攻撃 / 回避 / アクション遷移 / 優先度」。
 * 攻撃3種（小中大）+ 派生コンボ、回避ローリング（無敵）、優先度ゲート
 * （移動 < ガード < 攻撃 < 回避）、被弾での攻撃中断を管理する。
 *
 * 攻撃の命中判定は既存 BP の「RightHand」コンポーネント（近接コリジョン）を流用し、
 * HitActive 区間だけ Overlap を有効化してここで拾う。
 */
UCLASS(ClassGroup = (Player), meta = (BlueprintSpawnableComponent))
class COUNTERCORE_API UPlayerActionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerActionComponent();

	// --- 設定 ---

	/** プレイヤー攻撃データ（行名 = AttackId）。DT_PlayerAttacks。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	TObjectPtr<UDataTable> AttackDataTable;

	/** 各段の始動攻撃 ID。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	FName SmallStartId = FName("Small_1");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	FName MediumStartId = FName("Medium_1");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	FName HeavyStartId = FName("Heavy");

	/** 近接判定に使うコンポーネント名（既存 BP の "RightHand"）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	FName MeleeHitboxComponentName = FName("RightHand");

	// --- 回避（仕様: ローリング / 無敵時間あり）---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Dodge")
	TObjectPtr<UAnimMontage> DodgeMontage;

	/** 回避の全体時間（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Dodge", meta = (ClampMin = "0.05"))
	float DodgeDuration = 0.7f;

	/** 回避の無敵開始・終了（秒、回避開始から）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Dodge", meta = (ClampMin = "0"))
	float DodgeIFrameStart = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Dodge", meta = (ClampMin = "0"))
	float DodgeIFrameEnd = 0.45f;

	/** 回避の移動距離（cm）。入力方向、無ければ後方。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Dodge", meta = (ClampMin = "0"))
	float DodgeDistance = 400.f;

	// --- 回復（仕様のアクション一覧にあり。数値は調整用）---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal")
	TObjectPtr<UAnimMontage> HealMontage;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal", meta = (ClampMin = "0"))
	int32 HealAmount = 30;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal", meta = (ClampMin = "0"))
	int32 HealGaugeCost = 3;

	// --- 攻撃ヒットストップ実時間（bHitStop の攻撃で使用）---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack", meta = (ClampMin = "0"))
	float HitStopDuration = 0.11f;

	/** 画面デバッグ表示。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Debug")
	bool bPrintActionEvents = true;

	// --- Enhanced Input ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Input")
	TObjectPtr<UInputMappingContext> InputMapping;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Input", meta = (ClampMin = "0"))
	int32 InputMappingPriority = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Input")
	TObjectPtr<UInputAction> IA_AttackSmall;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Input")
	TObjectPtr<UInputAction> IA_AttackMedium;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Input")
	TObjectPtr<UInputAction> IA_AttackHeavy;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Input")
	TObjectPtr<UInputAction> IA_Guard;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Input")
	TObjectPtr<UInputAction> IA_Dodge;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Input")
	TObjectPtr<UInputAction> IA_Heal;

	// --- クエリ ---

	UFUNCTION(BlueprintPure, Category = "Player|Action")
	EPlayerActionType GetCurrentAction() const { return CurrentAction; }

	UFUNCTION(BlueprintPure, Category = "Player|Action")
	bool IsAttacking() const { return CurrentAction == EPlayerActionType::Attack; }

	/** 優先度ゲート（移動 < ガード < 攻撃 < 回避）+ 状態チェック。 */
	UFUNCTION(BlueprintPure, Category = "Player|Action")
	bool CanStartAction(EPlayerActionType Action) const;

	// --- 実行（BP からも呼べる）---

	UFUNCTION(BlueprintCallable, Category = "Player|Action")
	void TryAttack(EPlayerAttackTier Tier);

	UFUNCTION(BlueprintCallable, Category = "Player|Action")
	void TryDodge();

	UFUNCTION(BlueprintCallable, Category = "Player|Action")
	void TryHeal();

	/** 攻撃を即中断（仕様: 敵と相打ち → プレイヤー側の攻撃を強制中断）。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Action")
	void CancelAttack();

	/** 移動入力方向をここに供給しておくと回避の方向決めに使う（BP の Move から）。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Action")
	void SetMoveInput(FVector2D Input) { LastMoveInput = Input; }

	// --- デリゲート ---

	UPROPERTY(BlueprintAssignable, Category = "Player|Action") FPlayerActionChanged OnActionChanged;
	UPROPERTY(BlueprintAssignable, Category = "Player|Action") FPlayerAttackStarted OnAttackStarted;
	UPROPERTY(BlueprintAssignable, Category = "Player|Action") FPlayerActionSimpleEvent OnDodgeStarted;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// 入力
	void BindInput();
	void OnAttackSmall(const FInputActionValue&) { TryAttack(EPlayerAttackTier::Small); }
	void OnAttackMedium(const FInputActionValue&) { TryAttack(EPlayerAttackTier::Medium); }
	void OnAttackHeavy(const FInputActionValue&) { TryAttack(EPlayerAttackTier::Heavy); }
	void OnDodgeInput(const FInputActionValue&) { TryDodge(); }
	void OnHealInput(const FInputActionValue&) { TryHeal(); }
	void OnGuardStarted(const FInputActionValue&);
	void OnGuardCompleted(const FInputActionValue&);
	void OnMoveInput(const FInputActionValue& Value);

	// アクション
	void SetCurrentAction(EPlayerActionType New);
	void StartAttackRow(FName AttackId);
	void FinishAttack();
	void TickAttack(float Dt);
	void TickDodge(float Dt);
	bool GetAttackRow(FName AttackId, FPlayerAttackRow& OutRow) const;
	FName StartIdForTier(EPlayerAttackTier Tier) const;

	// 命中
	void SetMeleeHitboxActive(bool bActive);
	UFUNCTION()
	void OnMeleeOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void ApplyHitStop(float Duration);
	void PrintAction(const FString& Msg, const FColor& Color) const;

	UPlayerCombatComponent* GetCombat() const;
	UPlayerGuardComponent* GetGuard() const;

	UPROPERTY() TObjectPtr<UPlayerCombatComponent> Combat;
	UPROPERTY() TObjectPtr<UPlayerGuardComponent> Guard;
	UPROPERTY() TObjectPtr<UPrimitiveComponent> MeleeHitbox;

	UFUNCTION()
	void HandleCombatStateChanged(EPlayerCombatState OldState, EPlayerCombatState NewState);

	EPlayerActionType CurrentAction = EPlayerActionType::None;

	// 進行中の攻撃
	FName CurrentAttackId = NAME_None;
	FPlayerAttackRow CurrentAttackRow;
	float AttackElapsed = 0.f;
	bool bMeleeActive = false;
	bool bComboQueued = false;              // ウィンドウ中に次入力があった
	EPlayerAttackTier QueuedTier = EPlayerAttackTier::Small;
	UPROPERTY() TSet<TObjectPtr<AActor>> HitActorsThisSwing;

	// 回避
	float DodgeElapsed = 0.f;
	bool bDodgeIFrame = false;
	FVector DodgeDir = FVector::ZeroVector;

	FVector2D LastMoveInput = FVector2D::ZeroVector;
	FTimerHandle HitStopTimerHandle;
};
