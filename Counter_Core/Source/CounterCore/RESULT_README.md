# リザルト画面 — `feature/PlayerImpl-cpp`

仕様書「全体フロー / リザルト」。UMG なしのキャンバス描画 + GameInstance サブシステムで実装。

## データの受け渡し: `UBattleResultSubsystem`（GameInstance サブシステム）

レベル遷移をまたいで結果を運ぶ。設定不要で常に存在。

| フィールド | 内容 |
|---|---|
| `bWon` / `bHasResult` | 勝敗・結果があるか |
| `ClearTimeSeconds` | バトルタイム（`BattleDirector.GetElapsedTime`） |
| `GuardSuccessCount` | ガード成功回数（`PlayerGuardComponent.OnGuardSuccess` を数える） |
| `Rank` | `EResultRank` S/A/B/C/D |

`EvaluateRank`（仕様書の基準、閾値は `DefaultGame.ini` で調整可）:

| ランク | 条件 |
|---|---|
| S | タイム < `RankS_TimeMaxSeconds`(90s) かつ ガード `RankS_GuardMin`(20) 回以上 |
| A | < 120s かつ 15 回以上 |
| B | < 180s かつ 10 回以上 |
| C | 上記を満たさない**勝利** |
| D | 敗北 |

## 書き込み: `UBattleDirectorComponent`（BP_Player）

- `ResolveRefs` で `PlayerGuardComponent` を掴み `OnGuardSuccess` → `GuardSuccessCount++`
- `EndBattle`: `Subsystem->SubmitResult(win, ElapsedTime, GuardSuccessCount)` → 
  `ResultTransitionDelay`(3s、終了演出のあいだ) 後に `ShowResult()`
- `ShowResult()`: **`bShowResultInPlace=true`（既定）なら `PC->ClientSetHUD(ResultHUDClass)`**
  でレベル遷移せずに戦闘シーンの上へリザルト HUD を出す（仕様書の画は背景に戦闘シーンが
  見えているため）。false のときだけ `ResultLevelName` へ `OpenLevel`。
  `ResultHUDClass` は BP_Player の BattleDirector に `AResultHUD` を設定済み。

## 表示 + 操作: `AResultHUD`（仕様書の 図53 / 図43 に合わせる）

- 画面全体にうっすら暗幕（`DimAlpha` 0.4、不透明にしない＝奥のシーンが見える）
- **WIN**: `VICTORY` 上中央（白）/ 左に「スコア」小ラベル＋**巨大な黄色いランク文字**（S/A/B/C）/
  中央右に「タイム」＋`MM:SS`（`.cc` は小さめ）/ その下に「ガード成功 N」小
- **LOSE**: `LOSS` 中央（暗い赤）。はじめ LOSS だけ → `LossInputDelaySeconds`(3s) 後に
  操作受付＋「タイム / スコア D」小表示
- 右下（操作受付後）: `A 再挑戦   B タイトルへ   X ゲーム終了`（1 行ヒント。縦メニューは無し）
- **A / B / X 押下で確認ダイアログ**（「〜しますか？」＋ はい/いいえ、既定いいえ、左右で選択、A 確定 / B 取消）
- 実行: A→`RetryLevelName`(`LV_Ingame`) / B→`TitleLevelName`(`LV_Title`) / X→`QuitGame`
- 既存の `WBP_ClearResult` が出ていればビューポートから外す（`bRemoveLegacyResultWidget`）

## 注意 / 既存 BP との重複

`GM_Battle` の BP 側にも結果ロジック（`E_BattleScore` / `DetermineFinalRank` /
`OnBattleClear` → `WBP_ClearResult` 生成）が残っている。今回の C++ フローはそれとは
独立で、`BattleDirector`（新 C++ コンポーネント）の勝敗検知から
`LV_Result` + `AResultHUD` へ進む。`GM_Battle` 側が `LV_Ingame` 上で
`WBP_ClearResult` をオーバーレイ表示していると、遷移直前に数秒チラつく可能性がある。
不要なら `GM_Battle` の該当ノードを削除（BP グラフ編集）。
