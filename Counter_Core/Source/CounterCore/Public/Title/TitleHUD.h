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

private:
	ATitleSceneController* GetController();
	void DrawStr(const FString& Text, float X, float Y, int32 PixelSize, const FLinearColor& Color, bool bCenterX);

	UPROPERTY(Transient) TObjectPtr<ATitleSceneController> Cached;
};
