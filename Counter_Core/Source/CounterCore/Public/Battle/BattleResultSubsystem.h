#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BattleResultSubsystem.generated.h"

/** 仕様書「全体フロー / リザルト」スコア: S > A > B > C、敗北は D。 */
UENUM(BlueprintType)
enum class EResultRank : uint8
{
	S,
	A,
	B,
	C,
	D
};

/**
 * バトルの結果（勝敗 / タイム / ガード成功回数 / ランク）をレベル遷移をまたいで運ぶ。
 * バトル側（UBattleDirectorComponent）が SubmitResult で書き込み、リザルト HUD が読む。
 * GameInstance サブシステムなので設定不要で常に存在する。
 */
UCLASS(Config = Game)
class COUNTERCORE_API UBattleResultSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// --- 結果（SubmitResult で設定）---

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bHasResult = false;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bWon = false;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	float ClearTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 GuardSuccessCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	EResultRank Rank = EResultRank::D;

	// --- ランク閾値（仕様書の基準。DefaultGame.ini で調整可）---

	/** S: このタイム未満（秒）かつ GuardMin 回以上。仕様: 1分30秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Result|RankThreshold")
	float RankS_TimeMaxSeconds = 90.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Result|RankThreshold")
	int32 RankS_GuardMin = 20;

	/** A: 仕様: 2分未満 && 15回以上。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Result|RankThreshold")
	float RankA_TimeMaxSeconds = 120.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Result|RankThreshold")
	int32 RankA_GuardMin = 15;

	/** B: 仕様: 3分未満 && 10回以上。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Result|RankThreshold")
	float RankB_TimeMaxSeconds = 180.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Result|RankThreshold")
	int32 RankB_GuardMin = 10;

	// --- API ---

	/** バトル結果を記録し、ランクを確定する。 */
	UFUNCTION(BlueprintCallable, Category = "Result")
	void SubmitResult(bool bInWon, float TimeSeconds, int32 GuardCount);

	/** 勝敗・タイム・ガード回数からランクを判定（仕様書の基準）。 */
	UFUNCTION(BlueprintPure, Category = "Result")
	EResultRank EvaluateRank(bool bInWon, float TimeSeconds, int32 GuardCount) const;

	UFUNCTION(BlueprintCallable, Category = "Result")
	void Clear();

	/** "MM:SS.ss" 形式（仕様 UI: .00 秒単位、最大 99分99.99秒）。 */
	UFUNCTION(BlueprintPure, Category = "Result")
	FString GetTimeText() const;

	UFUNCTION(BlueprintPure, Category = "Result")
	static FString RankToString(EResultRank InRank);
};
