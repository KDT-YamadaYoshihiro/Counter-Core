#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Enemy/MonsterTypes.h"
#include "MonsterAttackComponent.generated.h"

/** 攻撃タイムラインの現在フェーズ。 */
UENUM(BlueprintType)
enum class EMonsterAttackPhase : uint8
{
	None,
	Anticipation, // 予兆（軸合わせ中）
	Committed,    // 軸合わせ停止〜攻撃発生前
	HitActive,    // 攻撃判定 ON
	Recovery,     // 硬直
	Finished
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMonsterAttackAnim, FName, AttackId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMonsterHitboxToggle, bool, bEnable);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMonsterAttackPhaseChanged, EMonsterAttackPhase, Phase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMonsterAttackEnded);

/**
 * 仕様書 Monster「AI / コンボ」＋「攻撃詳細」のロジック部分。
 *
 * - コンボ選択: 距離・角度・背後判定・発生確率（DT: FMonsterComboData）
 * - 攻撃タイムライン駆動: 予兆→軸合わせ停止→攻撃判定 ON/OFF→硬直→終了
 *   （DT: FMonsterAttackFrameData）。各節目でデリゲートを発火。
 * - 予兆中はターゲット方向へ TurnRateDegPerSec で回頭（所有 Actor の Yaw を回す）。
 *
 * アニメ再生・コリジョン実体・移動は BP 側がデリゲートで受けて行う。
 */
UCLASS(ClassGroup = (Monster), meta = (BlueprintSpawnableComponent))
class COUNTERCORE_API UMonsterAttackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMonsterAttackComponent();

	/** 攻撃1発分のフレームデータ（行名 = AttackId）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack")
	TObjectPtr<UDataTable> AttackDataTable;

	/** コンボ定義（行名 = ComboId）。優先度順に評価される。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack")
	TObjectPtr<UDataTable> ComboDataTable;

	/** 予兆中に回頭するときの所有 Actor（未設定なら GetOwner）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack")
	TObjectPtr<AActor> RotationActor;

	/** true でコンボ発動条件（距離リング・角度ウェッジ）と攻撃フェーズをその場に可視化する。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Debug")
	bool bDrawDebug = true;

	/** true で攻撃の開始 / 判定ON / 判定OFF / 終了を画面に Print String 表示する。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Debug")
	bool bPrintAttackEvents = true;

	/** Print String の表示秒数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Debug", meta = (ClampMin = "0.5"))
	float AttackEventPrintDuration = 4.f;

	/**
	 * PG-03: true なら判定 ON/OFF を Montage の Combat Event（HitStart / HitEnd）で行う（NotifyHitStart / NotifyHitEnd）。
	 * false なら DT の秒数（HitWindows / HitActiveStart-End）。フェーズ進行・終了は常に DT の秒数。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Attack")
	bool bUseNotifyHitWindow = false;

	// --- クエリ ---

	/**
	 * 距離・角度から発動可能なコンボを1つ選ぶ。仕様: 先頭から評価し、
	 * 条件を満たしたコンボで発生確率ロール成功なら採用。失敗なら次へ。
	 * 返り値が NAME_None なら発動可能コンボ無し。
	 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Attack")
	FName SelectCombo(float DistanceToTargetM, float SignedAngleToTargetDeg) const;

	/** 指定コンボの攻撃 ID 列を返す。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Attack")
	TArray<FName> GetComboAttacks(FName ComboId) const;

	/** 単発の攻撃フレームデータを引く。bFound=false なら未定義。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Attack")
	FMonsterAttackFrameData GetAttackData(FName AttackId, bool& bFound) const;

	/** 指定コンボの定義を引く。bFound=false なら未定義。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Attack")
	FMonsterComboData GetComboData(FName ComboId, bool& bFound) const;

	UFUNCTION(BlueprintPure, Category = "Monster|Attack")
	bool IsAttacking() const { return CurrentPhase != EMonsterAttackPhase::None && CurrentPhase != EMonsterAttackPhase::Finished; }

	UFUNCTION(BlueprintPure, Category = "Monster|Attack")
	EMonsterAttackPhase GetCurrentPhase() const { return CurrentPhase; }

	UFUNCTION(BlueprintPure, Category = "Monster|Attack")
	bool IsHitstunAllowed() const;

	/** 進行中の攻撃データ。 */
	UFUNCTION(BlueprintPure, Category = "Monster|Attack")
	FMonsterAttackFrameData GetActiveData() const { return ActiveData; }

	/** PG-16: 現在の判定区間の攻撃力（区間 Damage>0 ならそれ、無ければ行の Damage）。 */
	UFUNCTION(BlueprintPure, Category = "Monster|Attack")
	int32 GetCurrentHitDamage() const;

	/** PG-16: 直前の判定 ON で「ヒット済み相手」をリセットすべきか。 */
	UFUNCTION(BlueprintPure, Category = "Monster|Attack")
	bool ShouldResetHitTargets() const { return bResetHitTargets; }

	/** PG-14: タイムラインを一時停止（溜め）。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Attack")
	void SetTimelinePaused(bool bPaused) { bTimelinePaused = bPaused; }

	UFUNCTION(BlueprintPure, Category = "Monster|Attack")
	bool IsTimelinePaused() const { return bTimelinePaused; }

	/** PG-03: Notify 駆動時の判定 ON / OFF。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Attack")
	void NotifyHitStart();
	UFUNCTION(BlueprintCallable, Category = "Monster|Attack")
	void NotifyHitEnd();

	/** PG-06: ガードされた等で、今の判定区間を次の区間まで打ち切る。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Attack")
	void EndCurrentHitWindow();

	// --- 実行 ---

	/** ターゲット（回頭先）を設定。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Attack")
	void SetTarget(AActor* InTarget) { TargetActor = InTarget; }

	/** 単発の攻撃を開始。タイムライン駆動が始まる。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Attack")
	void StartAttack(FName AttackId);

	/** 攻撃を即中断（やられ割り込み等）。Hitbox を切り、Finished にする。 */
	UFUNCTION(BlueprintCallable, Category = "Monster|Attack")
	void CancelAttack();

	// --- デリゲート ---

	/**
	 * アニメーション再生要求（攻撃開始時 = 予兆の頭）。
	 * 仕様書「攻撃詳細」: 攻撃4/5 はモーション開始が [0.0s]。攻撃1〜3 も予兆から一連で見せる。
	 * モンタージュは受け手側で攻撃タイムライン（EndTime）に尺を合わせる。
	 */
	UPROPERTY(BlueprintAssignable, Category = "Monster|Attack")
	FMonsterAttackAnim OnPlayAttackAnim;

	/** 攻撃判定 ON の瞬間（斬撃 VFX 等をここで出す）。 */
	UPROPERTY(BlueprintAssignable, Category = "Monster|Attack")
	FMonsterAttackAnim OnAttackHitActive;

	/** 攻撃判定コリジョンの ON/OFF。 */
	UPROPERTY(BlueprintAssignable, Category = "Monster|Attack")
	FMonsterHitboxToggle OnToggleHitbox;

	/** フェーズ遷移通知。 */
	UPROPERTY(BlueprintAssignable, Category = "Monster|Attack")
	FMonsterAttackPhaseChanged OnPhaseChanged;

	/** 攻撃タイムライン終了（通常 State へ戻れる）。 */
	UPROPERTY(BlueprintAssignable, Category = "Monster|Attack")
	FMonsterAttackEnded OnAttackFinished;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void SetPhase(EMonsterAttackPhase NewPhase);
	void RotateTowardTarget(float DeltaTime, float RateDegPerSec);
	void DrawDebugVisualization() const;
	void PrintAttackEvent(const TCHAR* Label, const FColor& Color) const;
	void UpdateHitbox(int32 WindowIndex);
	void GetHitRange(float& OutFirstStart, float& OutLastEnd) const;
	int32 FindTimedWindow() const;

	UPROPERTY()
	TObjectPtr<AActor> TargetActor;

	FMonsterAttackFrameData ActiveData;
	EMonsterAttackPhase CurrentPhase = EMonsterAttackPhase::None;
	float ElapsedTime = 0.f;
	bool bHitboxOn = false;
	bool bTimelinePaused = false;
	bool bResetHitTargets = true;
	int32 CurrentWindow = INDEX_NONE;   // 判定中の区間（無し = INDEX_NONE）
	int32 SuppressedWindow = INDEX_NONE;// EndCurrentHitWindow で打ち切った区間
	int32 WindowsOpened = 0;            // この攻撃で開いた区間数
	int32 NotifyWindowCounter = 0;      // Notify 駆動時の区間番号
	bool bNotifyHitOn = false;
	float AccumulatedTurnDeg = 0.f;     // PG-15
};
