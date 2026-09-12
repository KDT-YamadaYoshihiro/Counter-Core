#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
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

private:
	AActor* FindEnemy() const;
	void DrawBar(float X, float Y, float W, float H, float FillFrac, float DelayFrac,
		const FLinearColor& FillColor, const FLinearColor& DelayColor);
	void DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale = 1.f);
	void DrawGaugeImage(const FGaugeImageConfig& Cfg, float VW, float VH);
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
