#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/PlayerTypes.h"
#include "Common/AnimNotify_CombatEvent.h"
#include "PlayerActionComponent.generated.h"

class UPlayerCombatComponent;
class UPlayerGuardComponent;
class UInputAction;
class UInputMappingContext;
class UPrimitiveComponent;
class UAnimMontage;
class UChildActorComponent;
class UShapeComponent;
class UCameraShakeBase;
class UAnimInstance;
struct FInputActionValue;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlayerActionChanged, EPlayerActionType, NewAction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlayerAttackStarted, FName, AttackId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPlayerActionSimpleEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPlayerPotionChanged, int32, Count, int32, MaxCount);

/**
 * 仕様書 Player シート「攻撃 / 回避 / アクション遷移 / 優先度」。
 * 攻撃3種（小中大）+ 派生コンボ、回避ローリング（無敵）、優先度ゲート
 * （移動 < ガード < 攻撃 < 回避）、被弾での攻撃中断を管理する。
 *
 * 攻撃の命中判定は既存 BP の「RightHand」コンポーネント（近接コリジョン）を流用し、
 * HitActive 区間だけ Overlap を有効化してここで拾う。
 */
UCLASS(ClassGroup = (Player), meta = (BlueprintSpawnableComponent))
class COUNTERCORE_API UPlayerActionComponent : public UActorComponent, public ICombatEventReceiver
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

	/** 手に持たせる武器アクター（敵と同じ BP_Weapon）。設定すると武器内の判定シェイプを攻撃判定に使う。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	TSubclassOf<AActor> WeaponClass;

	/** 武器 / 近接判定ボックスをアタッチするメッシュのソケット / ボーン名。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	FName WeaponSocket = FName(TEXT("剣追加用ソケット"));

	/** 近接判定ボックスの半径（extent）。武器の見た目とは独立に、確実に当てるための固定サイズ。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	FVector MeleeHitboxExtent = FVector(90.f, 75.f, 90.f);

	/** 近接判定ボックスのプレイヤー(root)からの相対位置。前方リーチ。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	FVector MeleeHitboxOffset = FVector(110.f, 0.f, 0.f);

	/** 既存 BP の近接コンポーネント名。見つかったら常時 NoCollision にして旧処理を止める。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	FName LegacyMeleeComponentName = FName("RightHand");

	/** 攻撃がヒットしたときのカメラシェイク。代用: BP_CameraShake_Hit_Enemy。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	TSubclassOf<UCameraShakeBase> AttackHitCameraShake;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack", meta = (ClampMin = "0", ClampMax = "2"))
	float CameraShakeScale = 0.25f;

	/** DT のフレーム値（InterruptibleStartFrame）を秒へ換算するフレームレート（PG-08）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack|Timing", meta = (ClampMin = "1"))
	float FrameRate = 30.f;

	/** SyncAttackTableToMontages での端数処理（PG-09）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack|Timing")
	EFrameRounding InterruptibleFrameRounding = EFrameRounding::Floor;

	/** SyncAttackTableToMontages: InterruptibleStartFrame = 総フレーム × この割合（PG-09）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack|Timing", meta = (ClampMin = "0", ClampMax = "1"))
	float AutoInterruptibleRatio = 0.6f;

	/** SyncAttackTableToMontages: PlayRate が 0（自動）の行に書き込む再生速度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack|Timing", meta = (ClampMin = "0.01"))
	float SyncPlayRate = 1.f;

	/** 攻撃を強制終了するときの Montage ブレンドアウト時間（PG-10）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack|Timing", meta = (ClampMin = "0"))
	float AttackBlendOutTime = 0.1f;

	/**
	 * true: 攻撃判定を Montage の Combat Event Notify（HitStart / HitEnd / AttackEnd）で切り替える（PG-03）。
	 * false: DT の秒数（HitActiveStart / HitActiveEnd / EndTime）で切り替える。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack|Timing")
	bool bUseNotifyHitWindow = false;

	/** Combat Event Notify の名前。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack|Timing")
	FCombatEventNames CombatEventNames;

	/** 命中時のヒットストップを被弾した敵にもかける（PG-19）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack")
	bool bHitStopAlsoOnTarget = true;

	/** エディタ用（PG-09）: AttackDataTable の各行を Montage 尺に 1:1 同期。
	 * EndTime = 尺 / PlayRate、InterruptibleStartFrame = 総フレーム × AutoInterruptibleRatio（端数は InterruptibleFrameRounding）。 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Player|Attack|Timing")
	void SyncAttackTableToMontages();

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

	/** 回避開始時に入力方向へ即回転する（PG-20）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Dodge")
	bool bFaceDodgeDirection = true;

	/** 入力が無いときの回避方向（PG-20）。向きは変えない。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Dodge")
	EDodgeNoInputDirection DodgeNoInputDirection = EDodgeNoInputDirection::Backward;

	/** 移動入力をどの向き基準でワールド方向にするか（PG-20）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Dodge")
	EDodgeInputBasis DodgeInputBasis = EDodgeInputBasis::Camera;

	/** この大きさ未満の入力は「入力なし」扱い（PG-20）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Dodge", meta = (ClampMin = "0", ClampMax = "1"))
	float DodgeInputDeadZone = 0.2f;

	/** ロックオン中の回避の向き（PG-20）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Dodge")
	ELockOnDodgeFacing LockOnDodgeFacing = ELockOnDodgeFacing::FaceDodgeDirection;

	/** 回避開始時の演出（PG-18）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Dodge")
	FCombatFeedback DodgeFeedback;

	// --- 回復（PG-05 / PG-22）---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal")
	TObjectPtr<UAnimMontage> HealMontage;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal", meta = (ClampMin = "0"))
	int32 HealAmount = 30;

	/** 回復のコスト。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal")
	EHealCostMode HealCostMode = EHealCostMode::Potion;

	/** HealCostMode がゲージを含むときに消費するゲージ（枠）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal", meta = (ClampMin = "0"))
	int32 HealGaugeCost = 3;

	/** 回復薬の初期所持数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal", meta = (ClampMin = "0"))
	int32 InitialPotionCount = 3;

	/** 回復薬の最大所持数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal", meta = (ClampMin = "0"))
	int32 MaxPotionCount = 3;

	/** 回復動作の全体時間（秒）。0 = HealMontage の尺（Montage も無ければ即時）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal", meta = (ClampMin = "0"))
	float HealDuration = 0.f;

	/** 回復開始から HP が増えるまでの秒。これより前に中断されると回復しない（コストは消費済み）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal", meta = (ClampMin = "0"))
	float HealApplyTime = 0.f;

	/** 回復中に許可する操作（PG-05）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal")
	bool bAllowMoveWhileHealing = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal")
	bool bAllowDodgeWhileHealing = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal")
	bool bAllowGuardWhileHealing = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal")
	bool bAllowAttackWhileHealing = false;

	/** 回復時の演出（PG-18）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Heal")
	FCombatFeedback HealFeedback;

	/** コンボ終了後、次の攻撃を始められるまでの間（秒）。連打での即リスタート痙攣防止。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attack", meta = (ClampMin = "0"))
	float PostComboCooldown = 0.15f;

	/** 画面デバッグ表示。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Debug")
	bool bPrintActionEvents = true;

	// --- Enhanced Input ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Input")
	TObjectPtr<UInputMappingContext> InputMapping;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Input", meta = (ClampMin = "0"))
	int32 InputMappingPriority = 1;

	/** true で IMC 未設定でも既定キー（RT/A/X/Y/RB/B、キーボード J/K/L/Space/H/右クリック）を直接バインド。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Input")
	bool bBindFallbackKeys = true;

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

	/** PG-10: 現在の攻撃段を終了（判定OFF・RootMotion/Montage 停止・バッファ消去）。何度呼んでも 1 回だけ効く。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Action")
	void EndCurrentAttackStep(bool bStopMontage = true);

	UFUNCTION(BlueprintPure, Category = "Player|Heal")
	int32 GetPotionCount() const { return PotionCount; }

	UFUNCTION(BlueprintPure, Category = "Player|Heal")
	bool IsHealing() const { return CurrentAction == EPlayerActionType::Heal; }

	/** 回復薬を増減（MaxPotionCount でクランプ）。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Heal")
	void AddPotion(int32 Delta);

	/** 移動入力方向をここに供給しておくと回避の方向決めに使う（BP の Move から）。 */
	UFUNCTION(BlueprintCallable, Category = "Player|Action")
	void SetMoveInput(FVector2D Input) { LastMoveInput = Input; }

	// --- デリゲート ---

	UPROPERTY(BlueprintAssignable, Category = "Player|Action") FPlayerActionChanged OnActionChanged;
	UPROPERTY(BlueprintAssignable, Category = "Player|Action") FPlayerAttackStarted OnAttackStarted;
	UPROPERTY(BlueprintAssignable, Category = "Player|Action") FPlayerActionSimpleEvent OnDodgeStarted;
	UPROPERTY(BlueprintAssignable, Category = "Player|Heal") FPlayerPotionChanged OnPotionCountChanged;

	// ICombatEventReceiver
	virtual void ReceiveCombatEvent_Implementation(FName EventName) override;

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

	/** IMC 未設定時のフォールバック（キーポーリング）。 */
	void PollFallbackInput();

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
	void SweepMeleeOverlaps();
	UFUNCTION()
	void OnMeleeOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void TickHeal(float Dt);
	void EndHeal(bool bInterrupted);
	void SetMoveInputIgnored(bool bIgnore);
	UAnimInstance* GetAnimInstance() const;
	void PrintAction(const FString& Msg, const FColor& Color) const;

	UPlayerCombatComponent* GetCombat() const;
	UPlayerGuardComponent* GetGuard() const;

	void PlayAttackHitShake() const;

	UPROPERTY() TObjectPtr<UPlayerCombatComponent> Combat;
	UPROPERTY() TObjectPtr<UPlayerGuardComponent> Guard;
	UPROPERTY() TObjectPtr<UChildActorComponent> WeaponActor;
	UPROPERTY() TObjectPtr<UPrimitiveComponent> MeleeHitbox;

	UFUNCTION()
	void HandleCombatStateChanged(EPlayerCombatState OldState, EPlayerCombatState NewState);

	EPlayerActionType CurrentAction = EPlayerActionType::None;

	// 進行中の攻撃
	FName CurrentAttackId = NAME_None;
	FPlayerAttackRow CurrentAttackRow;
	float AttackElapsed = 0.f;
	float ComboCooldownTimer = 0.f;        // コンボ後の再始動待ち
	bool bMeleeActive = false;
	bool bComboQueued = false;              // ウィンドウ中に次入力があった
	bool bAttackStepActive = false;         // EndCurrentAttackStep の二重実行防止
	float CurrentPlayRate = 1.f;            // 現在の攻撃 Montage の再生速度

	// 回復
	int32 PotionCount = 0;
	float HealElapsed = 0.f;
	float HealTotal = 0.f;
	bool bHealApplied = false;
	bool bMoveInputIgnored = false;
	EPlayerAttackTier QueuedTier = EPlayerAttackTier::Small;
	UPROPERTY() TSet<TObjectPtr<AActor>> HitActorsThisSwing;

	// 回避
	float DodgeElapsed = 0.f;
	bool bDodgeIFrame = false;
	FVector DodgeDir = FVector::ZeroVector;

	FVector2D LastMoveInput = FVector2D::ZeroVector;
};
