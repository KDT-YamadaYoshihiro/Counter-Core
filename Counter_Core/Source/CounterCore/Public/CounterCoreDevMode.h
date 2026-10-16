#pragma once

#include "CoreMinimal.h"

class AHUD;
class UCanvas;

/**
 * 開発者モード（動作確認用）。F1 で ON / OFF。ON の間だけ数字キー等でデバッグ操作ができる。
 *
 * 製品版（Shipping ビルド）では実装がコンパイルされず、IsActive() は常に false、
 * Tick / Draw は何もしない。ON にする手段もキー処理も存在しない。
 */
namespace CounterCoreDevMode
{
	/** 開発者モードが ON か。Shipping では常に false。 */
	COUNTERCORE_API bool IsActive();

	/** 入力処理（F1 トグルと各デバッグ操作）。HUD から毎フレーム呼ぶ。 */
	COUNTERCORE_API void Tick(AHUD* Hud);

	/** 開発者モードのパネル描画。HUD から毎フレーム呼ぶ。 */
	COUNTERCORE_API void Draw(AHUD* Hud, UCanvas* Canvas);
}
