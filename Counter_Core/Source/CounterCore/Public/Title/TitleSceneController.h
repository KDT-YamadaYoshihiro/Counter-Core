#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TitleSceneController.generated.h"

class ACameraActor;
class APlayerController;

/**
 * 仕様書「全体フロー / タイトル画面の動き」:
 *   「ドローンでカメラがぐるぐる背景モデルを映して回ってるイメージ」
 *
 * タイトル UI（1枚絵 + PUSH A BUTTON）と A ボタン→フェード→遷移は既存の
 * WBP_Title が担当しているので、このアクターは「周回カメラ」だけを担当する。
 * LV_Title に 1 つ置き、アクター位置を周回中心（背景の中心）にする。
 */
UCLASS()
class COUNTERCORE_API ATitleSceneController : public AActor
{
	GENERATED_BODY()

public:
	ATitleSceneController();

	/** 周回半径（cm）。ステージを外から見渡す想定で既定は大きめ。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Camera", meta = (ClampMin = "0"))
	float OrbitRadius = 5500.f;

	/** 周回中心からのカメラ高さ（cm）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Camera")
	float OrbitHeight = 2400.f;

	/** 周回速度（度/秒）。プラスで時計回り。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Camera")
	float OrbitDegreesPerSecond = 5.f;

	/** 注視点を周回中心からどれだけ上に取るか（cm）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Camera")
	float LookAtHeightOffset = 200.f;

	/** カメラ FOV。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Camera", meta = (ClampMin = "5", ClampMax = "170"))
	float CameraFOV = 72.f;

	/** 開始角度（度）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Camera")
	float StartAngleDegrees = 0.f;

	/** ゆっくり上下に揺らす振幅（cm、0 で無効）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Camera", meta = (ClampMin = "0"))
	float BobAmplitude = 150.f;

	/** 上下揺れの周期（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Camera", meta = (ClampMin = "0.1"))
	float BobPeriod = 14.f;

	// --- ゲーム終了（☰ / Esc / Start）---

	/** ☰ / Esc / Start で終了確認を出せるようにする。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Quit")
	bool bAllowQuit = true;

	UFUNCTION(BlueprintPure, Category = "Title|Quit")
	bool IsQuitPromptOpen() const { return bQuitPromptOpen; }

	UFUNCTION(BlueprintPure, Category = "Title|Quit")
	bool IsQuitPromptYes() const { return bQuitPromptYes; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void SetupCamera();
	void UpdateOrbit(float DeltaSeconds);
	void PollQuitInput();
	void SetTitleWidgetHidden(bool bWantHidden);
	APlayerController* GetPC() const;

	UPROPERTY(Transient) TObjectPtr<ACameraActor> OrbitCam;
	UPROPERTY(Transient) TObjectPtr<class UUserWidget> CachedTitleWidget;

	FVector PivotWorld = FVector::ZeroVector;
	float Angle = 0.f;
	float Elapsed = 0.f;
	bool bQuitPromptOpen = false;
	bool bQuitPromptYes = false;
	FTimerHandle SetupRetryTimer;
};
