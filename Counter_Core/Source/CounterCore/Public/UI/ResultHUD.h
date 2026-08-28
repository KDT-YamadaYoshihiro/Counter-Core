#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ResultHUD.generated.h"

class UBattleResultSubsystem;

UENUM()
enum class EResultMenuItem : uint8
{
	Retry,   // 再挑戦 → バトルシーン
	Title,   // タイトルへ
	Quit     // ゲーム終了
};

/**
 * 仕様書「全体フロー / リザルト」の画面（UMG なしのキャンバス描画）。
 * - 勝敗結果 / スコア(ランク S>A>B>C、敗北 D) / タイム / ガード成功回数
 * - 敗北時: はじめ「LOSS」のみ → 3秒後に選択項目
 * - 選択: 再挑戦 / タイトルへ / ゲーム終了（A で決定、B=タイトル・X=終了の直接指定）
 * - 決定でダイアログ（はい / いいえ）を表示
 * GM_Result の HUDClass に設定する。データは UBattleResultSubsystem から読む。
 */
UCLASS()
class COUNTERCORE_API AResultHUD : public AHUD
{
	GENERATED_BODY()

public:
	AResultHUD();

	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	/** 「再挑戦」で開くレベル。仕様: A = バトルシーン。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|HUD")
	FName RetryLevelName = FName("LV_Ingame");

	/** 「タイトルへ」で開くレベル。仕様: B = タイトル。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|HUD")
	FName TitleLevelName = FName("LV_Title");

	/** 敗北時、「LOSS」だけ出してから選択項目を出すまでの遅延（秒）。仕様: 3秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|HUD", meta = (ClampMin = "0"))
	float LossMenuDelaySeconds = 3.f;

	/** 勝利時に選択項目を出すまでの遅延（秒、演出用）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|HUD", meta = (ClampMin = "0"))
	float WinMenuDelaySeconds = 1.f;

	/** 既存の WBP_ClearResult をビューポートから外す（C++ HUD に一本化）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|HUD")
	bool bRemoveLegacyResultWidget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|HUD")
	TArray<FString> LegacyWidgetNameContains = { TEXT("ClearResult"), TEXT("Result") };

private:
	UBattleResultSubsystem* GetResultSys() const;
	APlayerController* GetPC() const;

	void UpdateState();
	void HandleMenuInput();
	void HandleDialogInput();
	void OpenDialogFor(EResultMenuItem Item);
	void ExecuteSelected();
	void SweepLegacyWidget();

	void DrawTextCentered(const FString& Text, float CX, float CY, float Scale, const FLinearColor& Color);
	void DrawTextLeft(const FString& Text, float X, float CY, float Scale, const FLinearColor& Color);
	float MenuDelay() const;

	bool bMenuVisible = false;
	bool bDialogOpen = false;
	bool bDialogYes = false;                 // ダイアログの選択（はい/いいえ）
	EResultMenuItem Selected = EResultMenuItem::Retry;
	EResultMenuItem DialogItem = EResultMenuItem::Retry;
	float Elapsed = 0.f;
	bool bExecuting = false;

	FTimerHandle SweepTimer;
	int32 SweepCount = 0;
};
