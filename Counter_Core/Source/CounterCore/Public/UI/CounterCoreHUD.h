#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Engine/DataTable.h"
#include "Layout/Margin.h"
#include "CounterCoreHUD.generated.h"

class UPlayerCombatComponent;
class UPlayerGuardComponent;
class UMonsterCombatComponent;
class UTexture2D;

/** ゲージ用ベース画像1枚分の設定。WBP を使わず HUD Canvas に直接描画するための定義。
 * Canvas 描画は UMG（Slate）より必ず後ろに回る（Unreal の仕様上、常に UMG が前面）ため、
 * 数値テキストを画像より前に出したい場合は画像側もこちらの Canvas 描画に統一する。 */
USTRUCT(BlueprintType)
struct FGaugeImageConfig
{
	GENERATED_BODY()

	/** 表示するテクスチャ。未設定（None）なら描画しない。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UTexture2D> Texture = nullptr;

	/** 画面サイズに対する基準位置（0-1、左上原点。例: (0.5, 1.0) = 画面下辺中央）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector2D AnchorFraction = FVector2D(0.5f, 0.5f);

	/** AnchorFraction の位置からの追加ピクセルオフセット（画像左上基準）。位置調整はここで行う。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector2D PixelOffset = FVector2D::ZeroVector;

	/** 描画サイズ（ピクセル）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector2D Size = FVector2D(200.f, 60.f);

	/** PG-27: Texture を 9-slice で描くときの端の幅（テクスチャに対する割合 0-0.5）。全て 0 なら通常の引き伸ばし。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FMargin NineSliceMargin = FMargin(0.f);

	/** PG-27: ゲージの上に重ねる枠画像（Size と同じ矩形、FrameNineSliceMargin で 9-slice）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UTexture2D> FrameTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FMargin FrameNineSliceMargin = FMargin(0.f);

	/** PG-27: 装飾画像（ゲージ左上基準の DecorationOffset / DecorationSize）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UTexture2D> DecorationTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector2D DecorationOffset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector2D DecorationSize = FVector2D(64.f, 64.f);
};

/** PG-23: 操作説明の 1 行（DT_ControlGuide）。 */
USTRUCT(BlueprintType)
struct FControlGuideRow : public FTableRowBase
{
	GENERATED_BODY()

	/** ボタン名（例: 「RT / 右クリック」）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ControlGuide")
	FText ButtonName;

	/** 操作名（例: 「ガード」）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ControlGuide")
	FText ActionName;

	/** ボタンアイコン（任意）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ControlGuide")
	TObjectPtr<UTexture2D> ButtonIcon = nullptr;
};

/**
 * 仕様書 UI シートの簡易 HUD（UMG なしのキャンバス描画）。
 * - プレイヤー攻撃ゲージ（10 枠、画面下中央）
 * - ガード中の文字表示 + 盾ゲージ + ガード残り時間バー（＝サークルの代用）
 * - 敵（ボス）HP: 緑バー + 遅延ダメージの赤バー
 *
 * GameMode の HUDClass に設定して使う。
 */
UCLASS()
class COUNTERCORE_API ACounterCoreHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD") bool bShowPlayerGauge = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD") bool bShowPlayerHp = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD") bool bShowGuard = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD") bool bShowEnemyHp = true;

	/** true で旧 UI ウィジェット（UW_GameUI / UW_HpGaugeOnHead 等）をビューポートから外す。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD") bool bRemoveLegacyWidgets = true;

	/** 旧 UI とみなすウィジェットクラス名（部分一致）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	TArray<FString> LegacyWidgetNameContains = { TEXT("UW_GameUI"), TEXT("HpGaugeOnHead"), TEXT("GameUI") };

	/** 遅延ダメージ（赤バー）が実 HP に追いつく速さ（割合/秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD", meta = (ClampMin = "0.01"))
	float DelayBarCatchupPerSec = 0.35f;

	/** レイアウトを調整した基準解像度。
	 * Canvas 描画は生ピクセル、UMG は DPI スケール済みなので、そのままだと解像度ごとにズレる。
	 * この解像度での DPI スケールとの比を全ピクセル値に掛けて UMG と歩調を合わせる
	 * （＝この解像度では倍率 1.0 で、調整済みの見た目が変わらない）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	FIntPoint DesignResolution = FIntPoint(1280, 720);

	/** 各数値ラベルの表示位置（対応するバーの基準位置からのピクセルオフセット）。
	 * WBP 側に置いたゲージ画像と文字が被る場合はここで調整する（再ビルド不要）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Label Offset")
	FVector2D PlayerHpLabelOffset = FVector2D(0.f, -27.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Label Offset")
	FVector2D PlayerGaugeLabelOffset = FVector2D(0.f, -29.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Label Offset")
	FVector2D EnemyHpLabelOffset = FVector2D(0.f, -29.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Label Offset")
	FVector2D ShieldLabelOffset = FVector2D(0.f, -9.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Label Offset")
	FVector2D GuardTimeLabelOffset = FVector2D(0.f, -9.f);

	/** 各ゲージのベース画像。テクスチャ・位置・サイズはここで直接調整する（WBP は使わない）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Gauge Images")
	FGaugeImageConfig PlayerHpGaugeImage = FGaugeImageConfig{ nullptr, FVector2D(0.5f, 1.f), FVector2D(-300.f, -90.f), FVector2D(600.f, 60.f) };
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Gauge Images")
	FGaugeImageConfig EnemyHpGaugeImage = FGaugeImageConfig{ nullptr, FVector2D(0.5f, 0.f), FVector2D(-400.f, 20.f), FVector2D(800.f, 60.f) };
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Gauge Images")
	FGaugeImageConfig PlayerGaugeImage = FGaugeImageConfig{ nullptr, FVector2D(0.5f, 1.f), FVector2D(-160.f, -150.f), FVector2D(320.f, 50.f) };
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Gauge Images")
	FGaugeImageConfig HealPotionImage = FGaugeImageConfig{ nullptr, FVector2D(1.f, 1.f), FVector2D(-120.f, -120.f), FVector2D(80.f, 80.f) };

	// --- 回復薬残数（PG-22）---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Potion") bool bShowPotionCount = true;

	/** 表示形式。{Count} / {Max} が置換される。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Potion")
	FText PotionCountFormat = FText::FromString(TEXT("x {Count}"));

	/** HealPotionImage の基準位置からのオフセット。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Potion")
	FVector2D PotionCountOffset = FVector2D(56.f, 52.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Potion", meta = (ClampMin = "0.1"))
	float PotionCountScale = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Potion")
	FLinearColor PotionCountColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Potion")
	FLinearColor PotionEmptyColor = FLinearColor(0.6f, 0.6f, 0.6f, 1.f);

	// --- 操作説明（PG-23）---

	/** 操作表（行構造 FControlGuideRow）。未設定なら表は出さない。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|ControlGuide")
	TObjectPtr<UDataTable> ControlGuideTable;

	/** コントローラー画像。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|ControlGuide")
	FGaugeImageConfig ControllerImage = FGaugeImageConfig{ nullptr, FVector2D(0.5f, 0.5f), FVector2D(40.f, -180.f), FVector2D(480.f, 320.f) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|ControlGuide")
	FText ControlGuideTitle = FText::FromString(TEXT("操作説明"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|ControlGuide")
	FText ControlGuideBackHint = FText::FromString(TEXT("戻る: Esc / B"));

	/** 表の左上（画面比 0-1）・行間隔（画面高さ比）・操作名列の X オフセット（px）・文字スケール。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|ControlGuide")
	FVector2D ControlGuideTableOrigin = FVector2D(0.12f, 0.25f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|ControlGuide")
	float ControlGuideRowSpacing = 0.07f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|ControlGuide")
	float ControlGuideActionColumnX = 260.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|ControlGuide")
	float ControlGuideTextScale = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|ControlGuide")
	FVector2D ControlGuideIconSize = FVector2D(32.f, 32.f);

private:
	AActor* FindEnemy() const;
	void DrawBar(float X, float Y, float W, float H, float FillFrac, float DelayFrac,
		const FLinearColor& FillColor, const FLinearColor& DelayColor);
	void DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale = 1.f);
	void DrawGaugeImage(const FGaugeImageConfig& Cfg, float VW, float VH);
	void DrawNineSlice(UTexture2D* Tex, float X, float Y, float W, float H, const FMargin& Margin);
	void DrawControlGuide(float VW, float VH);
	void DrawInGameMenu(class UBattleDirectorComponent* BD, float VW, float VH);

	void SweepLegacyWidgets();

	/** DesignResolution 基準の描画倍率。DrawHUD の先頭で毎フレーム更新する。 */
	float UIScale = 1.f;

	float EnemyHpDisplayed = -1.f;
	float PlayerHpDisplayed = -1.f;
	int32 LegacySweepsLeft = 0;
	FTimerHandle LegacySweepTimer;
	TWeakObjectPtr<AActor> CachedEnemy;
};
