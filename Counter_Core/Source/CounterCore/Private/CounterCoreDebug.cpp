#include "CounterCoreDebug.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Misc/DelayedAutoRegister.h"

// パッケージした exe では既定オフ（製品画面にデバッグ文字を出さない）。
// エディタでは従来どおり既定オン。どちらも `cc.Debug 0/1` で実行中に切り替えられる。
static TAutoConsoleVariable<int32> CVarCounterCoreDebug(
	TEXT("cc.Debug"),
#if WITH_EDITOR
	1,
#else
	0,
#endif
	TEXT("Counter-Core の画面デバッグ表示（オンスクリーン文字 / 判定ワイヤーフレーム / AI ログ）。\n")
	TEXT("  0: すべて非表示（パッケージ版の既定）\n")
	TEXT("  1: 表示（エディタの既定）"),
	ECVF_Default);

namespace CounterCoreDebug
{
	bool IsOnScreenDebugEnabled()
	{
		return CVarCounterCoreDebug.GetValueOnAnyThread() != 0;
	}
}

#if !WITH_EDITOR
// パッケージ版では、プロジェクト外のエンジン警告（VSM オーバーフロー等）も画面に出さない。
// cc.Debug を実行中に変えれば戻せるよう、変更コールバックでも追従させる。
//
// 画面表示には系統が 2 つあり、両方止めないと消えない:
//   bEnableOnScreenDebugMessages … AddOnScreenDebugMessage 系（プロジェクトのデバッグ文字）
//   GAreScreenMessagesEnabled    … DrawMapWarnings 系（OnGetOnScreenMessages 経由のエンジン警告）
static void ApplyEngineScreenMessageState()
{
	const bool bShow = CounterCoreDebug::IsOnScreenDebugEnabled();
	if (GEngine)
	{
		GEngine->bEnableOnScreenDebugMessages = bShow;
	}
	GAreScreenMessagesEnabled = bShow;
}

static FDelayedAutoRegisterHelper GCounterCoreScreenMessageInit(
	EDelayedRegisterRunPhase::EndOfEngineInit,
	[]
	{
		ApplyEngineScreenMessageState();
		CVarCounterCoreDebug.AsVariable()->SetOnChangedCallback(
			FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { ApplyEngineScreenMessageState(); }));
	});
#endif
