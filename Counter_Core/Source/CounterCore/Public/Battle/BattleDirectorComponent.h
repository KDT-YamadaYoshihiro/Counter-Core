#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BattleDirectorComponent.generated.h"

class UPlayerCombatComponent;
class UMonsterCombatComponent;

UENUM(BlueprintType)
enum class EBattleResult : uint8
{
	InProgress UMETA(DisplayName = "戦闘中"),
	PlayerWin  UMETA(DisplayName = "プレイヤー勝利"),
	PlayerLose UMETA(DisplayName = "プレイヤー敗北")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBattleEnded, EBattleResult, Result);

/**
 * 仕様書 Battle シート「決着判定 / 終了演出」。
 * 敵 HP0 → プレイヤー勝利、プレイヤー HP0 → 敗北。決着で入力を止め、長めのヒットストップ、
 * OnBattleEnded を発火（GM がリザルト遷移に使う）。
 *
 * BP_Player に足すか、レベルの専用アクター / GM に足す。
 */
UCLASS(ClassGroup = (Battle), meta = (BlueprintSpawnableComponent))
class COUNTERCORE_API UBattleDirectorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBattleDirectorComponent();

	/** 決着時に全体スロー（終了演出のヒットストップ）をかける時間（実時間・秒）。0 で無効。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle", meta = (ClampMin = "0"))
	float EndSlowMoRealDuration = 1.2f;

	/** 決着時のグローバル時間スケール。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle", meta = (ClampMin = "0.01", ClampMax = "1"))
	float EndSlowMoTimeScale = 0.15f;

	/** 決着時にプレイヤー入力を止めるか。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	bool bDisablePlayerInputOnEnd = true;

	UFUNCTION(BlueprintPure, Category = "Battle")
	EBattleResult GetResult() const { return Result; }

	UFUNCTION(BlueprintPure, Category = "Battle")
	bool IsBattleOver() const { return Result != EBattleResult::InProgress; }

	UPROPERTY(BlueprintAssignable, Category = "Battle")
	FBattleEnded OnBattleEnded;

protected:
	virtual void BeginPlay() override;

private:
	void ResolveRefs();
	void EndBattle(EBattleResult NewResult);

	UFUNCTION() void HandlePlayerDied();
	UFUNCTION() void HandleEnemyDied();

	EBattleResult Result = EBattleResult::InProgress;

	UPROPERTY() TObjectPtr<UPlayerCombatComponent> PlayerCombat;
	UPROPERTY() TObjectPtr<UMonsterCombatComponent> EnemyCombat;

	FTimerHandle ResolveTimer;
	FTimerHandle SlowMoTimer;
};
