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

## 書き込み: `UBattleDirectorComponent`（BP_Player に既存）

- `ResolveRefs` で `PlayerGuardComponent` を掴み `OnGuardSuccess` → `GuardSuccessCount++`
- `EndBattle` で `Subsystem->SubmitResult(win, ElapsedTime, GuardSuccessCount)` → その後
  `ResultLevelName`(=`LV_Result`) を `OpenLevel`（`ResultTransitionDelay` 3s）

## 表示 + 操作: `AResultHUD`（`GM_Result` の HUDClass に設定済み）

- 背景不透明 + 勝敗（`YOU WIN` / `LOSS`）
- スコア（ランク大文字）/ `TIME MM:SS.ss` / `GUARD 回数`（勝敗どちらでも表示）
- **敗北時**: はじめ「LOSS」のみ → `LossMenuDelaySeconds`(3s) 後に選択項目（仕様通り）
- 選択項目: 再挑戦 / タイトルへ / ゲーム終了。上下で移動、
  - **A / Enter**: 現在の選択で決定（＝仕様「A バトルシーン」）
  - **B**: タイトルへ（直接）
  - **X**: ゲーム終了（直接）
- 決定すると **確認ダイアログ**（はい / いいえ、既定「いいえ」）。左右で選び A で確定
- 実行: 再挑戦→`RetryLevelName`(`LV_Ingame`) / タイトル→`TitleLevelName`(`LV_Title`) / 終了→`QuitGame`
- 既存の `WBP_ClearResult` はビューポートから自動的に外す（`bRemoveLegacyResultWidget`）

## 注意 / 既存 BP との重複

`GM_Battle` の BP 側にも結果ロジック（`E_BattleScore` / `DetermineFinalRank` /
`OnBattleClear` → `WBP_ClearResult` 生成）が残っている。今回の C++ フローはそれとは
独立で、`BattleDirector`（新 C++ コンポーネント）の勝敗検知から
`LV_Result` + `AResultHUD` へ進む。`GM_Battle` 側が `LV_Ingame` 上で
`WBP_ClearResult` をオーバーレイ表示していると、遷移直前に数秒チラつく可能性がある。
不要なら `GM_Battle` の該当ノードを削除（BP グラフ編集）。
