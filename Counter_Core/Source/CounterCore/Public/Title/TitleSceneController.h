#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "TitleSceneController.generated.h"

class ACameraActor;
class APlayerController;
class UTexture2D;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTitleGamepadChanged, bool, bGamepadConnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTitleStartRequested);

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

	// --- 開始プロンプト（PG-21）---

	/** ゲームパッド接続時 / 未接続時の文言。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Start")
	FText GamepadPromptText = FText::FromString(TEXT("PUSH A BUTTON"));
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Start")
	FText KeyboardPromptText = FText::FromString(TEXT("PRESS ENTER"));

	/** ゲームパッド接続時 / 未接続時の画像（任意。WBP から GetStartPromptImage で参照）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Start")
	TObjectPtr<UTexture2D> GamepadPromptImage;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Start")
	TObjectPtr<UTexture2D> KeyboardPromptImage;

	/** 開始入力として受け付けるキー（ゲームパッド / キーボード）。接続中の入力系のキーだけ有効。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Start")
	TArray<FKey> GamepadStartKeys = { EKeys::Gamepad_FaceButton_Bottom };
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Start")
	TArray<FKey> KeyboardStartKeys = { EKeys::Enter, EKeys::SpaceBar };

	/**
	 * true: このアクターが開始入力を拾い、StartLevelName へ 1 回だけ遷移する。
	 * false: 遷移は WBP_Title 側（OnStartRequested は引き続き 1 回だけ通知）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Start")
	bool bHandleStartInput = false;

	/** bHandleStartInput のときに開くレベル。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Start")
	FName StartLevelName;

	/** 開始入力から遷移までの待ち（秒。SE / フェード用）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Start", meta = (ClampMin = "0"))
	float StartTransitionDelay = 0.5f;

	/** 開始時の SE（PG-18 UI）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title|Start")
	TObjectPtr<USoundBase> StartSound;

	UFUNCTION(BlueprintPure, Category = "Title|Start")
	bool IsGamepadConnected() const { return bGamepadConnected; }

	UFUNCTION(BlueprintPure, Category = "Title|Start")
	FText GetStartPromptText() const { return bGamepadConnected ? GamepadPromptText : KeyboardPromptText; }

	UFUNCTION(BlueprintPure, Category = "Title|Start")
	UTexture2D* GetStartPromptImage() const { return bGamepadConnected ? GamepadPromptImage : KeyboardPromptImage; }

	UFUNCTION(BlueprintPure, Category = "Title|Start")
	bool HasStartBeenRequested() const { return bStartRequested; }

	/** 開始を要求（1 回だけ有効）。WBP から呼んでもよい。 */
	UFUNCTION(BlueprintCallable, Category = "Title|Start")
	bool RequestStart();

	/** 接続状態が変わった（起動時も 1 回）。 */
	UPROPERTY(BlueprintAssignable, Category = "Title|Start")
	FTitleGamepadChanged OnGamepadConnectionChanged;

	UPROPERTY(BlueprintAssignable, Category = "Title|Start")
	FTitleStartRequested OnStartRequested;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void SetupCamera();
	void UpdateOrbit(float DeltaSeconds);
	void PollQuitInput();
	void UpdateGamepadState(bool bForceBroadcast);
	void PollStartInput();
	void OpenStartLevel();
	void SetTitleWidgetHidden(bool bWantHidden);
	APlayerController* GetPC() const;

	UPROPERTY(Transient) TObjectPtr<ACameraActor> OrbitCam;
	UPROPERTY(Transient) TObjectPtr<class UUserWidget> CachedTitleWidget;

	FVector PivotWorld = FVector::ZeroVector;
	float Angle = 0.f;
	float Elapsed = 0.f;
	bool bQuitPromptOpen = false;
	bool bQuitPromptYes = false;
	bool bGamepadConnected = false;
	bool bStartRequested = false;
	FTimerHandle SetupRetryTimer;
	FTimerHandle StartTimer;
};
