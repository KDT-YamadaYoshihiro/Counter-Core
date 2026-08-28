#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CounterCoreHUD.generated.h"

class UPlayerCombatComponent;
class UPlayerGuardComponent;
class UMonsterCombatComponent;

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

private:
	AActor* FindEnemy() const;
	void DrawBar(float X, float Y, float W, float H, float FillFrac, float DelayFrac,
		const FLinearColor& FillColor, const FLinearColor& DelayColor);
	void DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale = 1.f);
	void DrawInGameMenu(class UBattleDirectorComponent* BD, float VW, float VH);

	void SweepLegacyWidgets();

	float EnemyHpDisplayed = -1.f;
	float PlayerHpDisplayed = -1.f;
	int32 LegacySweepsLeft = 0;
	FTimerHandle LegacySweepTimer;
	TWeakObjectPtr<AActor> CachedEnemy;
};
