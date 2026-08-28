#include "CounterCoreDebug.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarCounterCoreDebug(
	TEXT("cc.Debug"),
	1,
	TEXT("Counter-Core の画面デバッグ表示（オンスクリーン文字 / 判定ワイヤーフレーム / AI ログ）。\n")
	TEXT("  0: すべて非表示\n")
	TEXT("  1: 表示（既定）"),
	ECVF_Default);

namespace CounterCoreDebug
{
	bool IsOnScreenDebugEnabled()
	{
		return CVarCounterCoreDebug.GetValueOnAnyThread() != 0;
	}
}
