#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ResultHUD.generated.h"

class UBattleResultSubsystem;

UENUM()
enum class EResultChoice : uint8
{
	None,
	Retry,   // A: 再挑戦 → バトルシーン
	Title,   // B: タイトルへ
	Quit     // X: ゲーム終了
};

/**
 * 仕様書「全体フロー / リザルト」の画面（UMG なしのキャンバス描画）。
 * WIN:  "VICTORY" 上中央 / 左に大きな黄色ランク（"スコア"）/ 中央右に "タイム" MM:SS.ss /
 *       右下に「A 再挑戦   B タイトルへ   X ゲーム終了」
 * LOSE: "LOSS" 中央（暗赤）。はじめは LOSS だけ → LossMenuDelaySeconds 後に操作受付＋右下ヒント。
 * A/B/X 押下で確認ダイアログ（はい / いいえ）。
 * GM_Result の HUDClass に設定。データは UBattleResultSubsystem から読む。
 */
UCLASS()
class COUNTERCORE_API AResultHUD : public AHUD
{
	GENERATED_BODY()

public:
	AResultHUD();

	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	/** A「再挑戦」で開くレベル。仕様: A = バトルシーン。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|HUD")
	FName RetryLevelName = FName("LV_Ingame");

	/** B「タイトルへ」で開くレベル。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|HUD")
	FName TitleLevelName = FName("LV_Title");

	/** 敗北時、「LOSS」だけ出してから操作を受け付けるまでの遅延（秒）。仕様: 3秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|HUD", meta = (ClampMin = "0"))
	float LossInputDelaySeconds = 3.f;

	/** 勝利時に操作を受け付けるまでの遅延（秒、演出用）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|HUD", meta = (ClampMin = "0"))
	float WinInputDelaySeconds = 0.8f;

	/** 画面全体の暗幕アルファ（0=透明, 1=不透明）。仕様の画は背景がうっすら見える。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|HUD", meta = (ClampMin = "0", ClampMax = "1"))
	float DimAlpha = 0.4f;

	/** 既存の WBP_ClearResult をビューポートから外す（C++ HUD に一本化）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|HUD")
	bool bRemoveLegacyResultWidget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|HUD")
	TArray<FString> LegacyWidgetNameContains = { TEXT("ClearResult"), TEXT("Result") };

private:
	UBattleResultSubsystem* GetResultSys() const;
	APlayerController* GetPC() const;

	void UpdateState();
	void HandleInput();
	void OpenDialog(EResultChoice Choice);
	void Execute();
	void SweepLegacyWidget();

	void DrawStr(const FString& Text, float X, float Y, int32 PixelSize, const FLinearColor& Color,
		bool bCenterX, bool bCenterY, bool bBold = false);
	float MeasureWidth(const FString& Text, int32 PixelSize, bool bBold) const;

	bool bInputArmed = false;
	bool bDialogOpen = false;
	bool bDialogYes = false;
	EResultChoice DialogChoice = EResultChoice::None;
	float Elapsed = 0.f;
	bool bExecuting = false;

	FTimerHandle SweepTimer;
	int32 SweepCount = 0;
};
