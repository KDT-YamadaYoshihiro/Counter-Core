#include "Battle/BattleResultSubsystem.h"

void UBattleResultSubsystem::SubmitResult(bool bInWon, float TimeSeconds, int32 GuardCount)
{
	bHasResult = true;
	bWon = bInWon;
	ClearTimeSeconds = FMath::Max(0.f, TimeSeconds);
	GuardSuccessCount = FMath::Max(0, GuardCount);
	Rank = EvaluateRank(bWon, ClearTimeSeconds, GuardSuccessCount);
}

EResultRank UBattleResultSubsystem::EvaluateRank(bool bInWon, float TimeSeconds, int32 GuardCount) const
{
	// 仕様書「全体フロー / スコアの基準」:
	//   S: 1分30秒未満 && 20回以上のガード成功
	//   A: 2分未満 && 15回以上
	//   B: 3分未満 && 10回以上
	//   C: 上記いずれも満たさない勝利（3分以上など）
	//   D: 敗北時
	if (!bInWon)
	{
		return EResultRank::D;
	}
	if (TimeSeconds < RankS_TimeMaxSeconds && GuardCount >= RankS_GuardMin)
	{
		return EResultRank::S;
	}
	if (TimeSeconds < RankA_TimeMaxSeconds && GuardCount >= RankA_GuardMin)
	{
		return EResultRank::A;
	}
	if (TimeSeconds < RankB_TimeMaxSeconds && GuardCount >= RankB_GuardMin)
	{
		return EResultRank::B;
	}
	return EResultRank::C;
}

void UBattleResultSubsystem::Clear()
{
	bHasResult = false;
	bWon = false;
	ClearTimeSeconds = 0.f;
	GuardSuccessCount = 0;
	Rank = EResultRank::D;
}

FString UBattleResultSubsystem::GetTimeText() const
{
	const int32 TotalCentis = FMath::RoundToInt(ClearTimeSeconds * 100.f);
	const int32 Minutes = (TotalCentis / 100) / 60;
	const int32 Seconds = (TotalCentis / 100) % 60;
	const int32 Centis = TotalCentis % 100;
	return FString::Printf(TEXT("%02d:%02d.%02d"), Minutes, Seconds, Centis);
}

FString UBattleResultSubsystem::RankToString(EResultRank InRank)
{
	switch (InRank)
	{
	case EResultRank::S: return TEXT("S");
	case EResultRank::A: return TEXT("A");
	case EResultRank::B: return TEXT("B");
	case EResultRank::C: return TEXT("C");
	default:             return TEXT("D");
	}
}
