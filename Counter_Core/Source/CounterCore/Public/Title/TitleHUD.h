#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TitleHUD.generated.h"

class ATitleSceneController;

/**
 * タイトル画面用の最小 HUD。UI 本体（ロゴ / PUSH A BUTTON）は WBP_Title が担当。
 * ここでは左上の ☰ アイコンと「ゲーム終了」ヒント、終了確認ダイアログだけを描く。
 * GM_Title の HUDClass に設定する。
 */
UCLASS()
class COUNTERCORE_API ATitleHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	/** レイアウトを調整した基準解像度。WBP_Title（DPI スケール済み）と Canvas 描画の
	 * 拡大率を揃えるために使う。詳細は ACounterCoreHUD::DesignResolution と同じ。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	FIntPoint DesignResolution = FIntPoint(1280, 720);

	/** PG-21: 開始プロンプト（ゲームパッド接続で切替）を HUD で描く。WBP_Title 側で描くなら false。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Start")
	bool bDrawStartPrompt = false;

	/** プロンプト中央の画面比位置（0-1）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Start")
	FVector2D StartPromptAnchor = FVector2D(0.5f, 0.8f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Start", meta = (ClampMin = "6"))
	int32 StartPromptPixelSize = 32;

	/** 画像の描画サイズ（px、基準解像度）。画像があれば文字の代わりに描く。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Start")
	FVector2D StartPromptImageSize = FVector2D(360.f, 64.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Start")
	FLinearColor StartPromptColor = FLinearColor::White;

	/** 点滅周期（秒、0 で点滅なし）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Start", meta = (ClampMin = "0"))
	float StartPromptBlinkPeriod = 1.2f;

private:
	ATitleSceneController* GetController();
	void DrawStr(const FString& Text, float X, float Y, int32 PixelSize, const FLinearColor& Color, bool bCenterX);

	UPROPERTY(Transient) TObjectPtr<ATitleSceneController> Cached;

	/** DesignResolution 基準の描画倍率。DrawHUD の先頭で毎フレーム更新する。 */
	float UIScale = 1.f;
};
