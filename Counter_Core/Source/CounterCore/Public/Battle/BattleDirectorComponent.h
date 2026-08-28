#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BattleDirectorComponent.generated.h"

class UPlayerCombatComponent;
class UPlayerGuardComponent;
class UPlayerActionComponent;
class UMonsterCombatComponent;
class AHUD;

UENUM(BlueprintType)
enum class EBattleResult : uint8
{
	InProgress UMETA(DisplayName = "戦闘中"),
	PlayerWin  UMETA(DisplayName = "プレイヤー勝利"),
	PlayerLose UMETA(DisplayName = "プレイヤー敗北")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBattleEnded, EBattleResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBattleSimpleEvent);

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

	// --- 開始演出（仕様書 Battle「開始演出: プレイヤーの操作を受け付けない状態」）---

	/** true でバトル開始時に IntroDuration 秒だけ操作を止める。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|Intro")
	bool bAutoIntro = true;

	/** 開始演出の長さ（秒）。StartBattle() で早期解除できる。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|Intro", meta = (ClampMin = "0"))
	float IntroDuration = 3.f;

	// --- リザルト ---

	/**
	 * true: レベル遷移せず、決着した戦闘シーンの上にリザルト HUD を出す
	 *       （仕様書のリザルト画は背景に戦闘シーンが見えている）。
	 * false: ResultLevelName へレベル遷移する。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|Result")
	bool bShowResultInPlace = true;

	/** bShowResultInPlace のときに差し替える HUD クラス（AResultHUD を指定）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|Result")
	TSubclassOf<AHUD> ResultHUDClass;

	/** 決着後に開くレベル名（bShowResultInPlace=false のとき）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|Result")
	FName ResultLevelName;

	/** 決着からリザルト表示までの待ち（秒・実時間）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|Result", meta = (ClampMin = "0"))
	float ResultTransitionDelay = 3.f;

	UFUNCTION(BlueprintPure, Category = "Battle")
	EBattleResult GetResult() const { return Result; }

	UFUNCTION(BlueprintPure, Category = "Battle")
	bool IsBattleOver() const { return Result != EBattleResult::InProgress; }

	UFUNCTION(BlueprintPure, Category = "Battle")
	bool IsIntroPlaying() const { return bIntroActive; }

	/** 経過時間（秒）。開始演出が明けてからカウント。 */
	UFUNCTION(BlueprintPure, Category = "Battle")
	float GetElapsedTime() const { return ElapsedTime; }

	/** ガード成功回数（リザルトのスコア判定に使う）。 */
	UFUNCTION(BlueprintPure, Category = "Battle")
	int32 GetGuardSuccessCount() const { return GuardSuccessCount; }

	/** 開始演出を明けてバトル開始。 */
	UFUNCTION(BlueprintCallable, Category = "Battle")
	void StartBattle();

	// --- インゲームメニュー（仕様書 UI「メニュー」）---

	UFUNCTION(BlueprintPure, Category = "Battle|Menu")
	bool IsMenuOpen() const { return bMenuOpen; }

	/** 0=続ける 1=操作説明 2=あきらめる */
	UFUNCTION(BlueprintPure, Category = "Battle|Menu")
	int32 GetMenuSelection() const { return MenuSelection; }

	UFUNCTION(BlueprintPure, Category = "Battle|Menu")
	bool IsMenuDialogOpen() const { return bMenuDialogOpen; }

	UFUNCTION(BlueprintPure, Category = "Battle|Menu")
	bool IsMenuDialogYes() const { return bMenuDialogYes; }

	/** 操作説明ウィンドウを表示中か。 */
	UFUNCTION(BlueprintPure, Category = "Battle|Menu")
	bool IsControlsPanelOpen() const { return bControlsPanelOpen; }

	UFUNCTION(BlueprintCallable, Category = "Battle|Menu")
	void OpenMenu();
	UFUNCTION(BlueprintCallable, Category = "Battle|Menu")
	void CloseMenu();

	/** メニューを開くキー（既定: Esc / ゲームパッド Special_Right = Start）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|Menu")
	bool bAllowMenu = true;

	/** メニュー中の時間スケール（0 に近いほど停止）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|Menu", meta = (ClampMin = "0.001", ClampMax = "1"))
	float MenuTimeDilation = 0.01f;

	UPROPERTY(BlueprintAssignable, Category = "Battle|Menu")
	FBattleSimpleEvent OnMenuOpened;
	UPROPERTY(BlueprintAssignable, Category = "Battle|Menu")
	FBattleSimpleEvent OnMenuClosed;

	UPROPERTY(BlueprintAssignable, Category = "Battle")
	FBattleEnded OnBattleEnded;
	UPROPERTY(BlueprintAssignable, Category = "Battle")
	FBattleSimpleEvent OnBattleStarted;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void ResolveRefs();
	void EndBattle(EBattleResult NewResult);
	void SetActorsFrozen(bool bFrozen);
	void ShowResult();
	void PollMenuInput();
	void ConfirmMenuSelection();
	void GiveUp();

	UFUNCTION() void HandlePlayerDied();
	UFUNCTION() void HandleEnemyDied();
	UFUNCTION() void HandleGuardSuccess();

	EBattleResult Result = EBattleResult::InProgress;
	bool bIntroActive = false;
	bool bActorsFrozen = false;
	float IntroTimer = 0.f;
	float ElapsedTime = 0.f;
	int32 GuardSuccessCount = 0;

	// インゲームメニュー
	bool bMenuOpen = false;
	bool bMenuDialogOpen = false;
	bool bMenuDialogYes = false;
	bool bControlsPanelOpen = false;
	int32 MenuSelection = 0;

	UPROPERTY() TObjectPtr<UPlayerCombatComponent> PlayerCombat;
	UPROPERTY() TObjectPtr<UPlayerGuardComponent> PlayerGuard;
	UPROPERTY() TObjectPtr<UPlayerActionComponent> PlayerAction;
	UPROPERTY() TObjectPtr<UMonsterCombatComponent> EnemyCombat;
	UPROPERTY() TObjectPtr<AActor> EnemyActor;

	FTimerHandle ResolveTimer;
	FTimerHandle SlowMoTimer;
	FTimerHandle ResultTimer;
};
