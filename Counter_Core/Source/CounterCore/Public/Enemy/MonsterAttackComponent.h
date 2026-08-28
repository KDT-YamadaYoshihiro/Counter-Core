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

	UFUNCTION(BlueprintPure, Category = "Monster|Attack")
	bool IsAttacking() const { return CurrentPhase != EMonsterAttackPhase::None && CurrentPhase != EMonsterAttackPhase::Finished; }

	UFUNCTION(BlueprintPure, Category = "Monster|Attack")
	EMonsterAttackPhase GetCurrentPhase() const { return CurrentPhase; }

	UFUNCTION(BlueprintPure, Category = "Monster|Attack")
	bool IsHitstunAllowed() const;

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

	/** アニメーション再生要求（HitActive 突入時）。 */
	UPROPERTY(BlueprintAssignable, Category = "Monster|Attack")
	FMonsterAttackAnim OnPlayAttackAnim;

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

	UPROPERTY()
	TObjectPtr<AActor> TargetActor;

	FMonsterAttackFrameData ActiveData;
	EMonsterAttackPhase CurrentPhase = EMonsterAttackPhase::None;
	float ElapsedTime = 0.f;
	bool bHitboxOn = false;
};
