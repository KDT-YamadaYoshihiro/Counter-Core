#pragma once

#include "CoreMinimal.h"

/**
 * 画面デバッグ表示（オンスクリーンメッセージ、判定ワイヤーフレーム、AI ログ等）の
 * 一括 ON/OFF。
 *
 * コンソール変数: cc.Debug 0  … すべて非表示
 *                 cc.Debug 1  … 表示（既定）
 *
 * 各コンポーネントの b***Events / bDrawDebug と AND を取る
 * （＝ここが 0 なら個別設定に関係なく出ない）。
 */
namespace CounterCoreDebug
{
	/** cc.Debug が有効か。各デバッグ描画の入口で参照する。 */
	COUNTERCORE_API bool IsOnScreenDebugEnabled();
}
