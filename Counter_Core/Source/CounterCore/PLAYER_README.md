# プレイヤー実装（C++ コンポーネント）— `feature/PlayerImpl-cpp`

仕様書 `Player` / `Battle` シートの**優先度「高」**まで。既存の移動・カメラ・ロックオン・
近接メッシュ・HP UI は温存し、C++ コンポーネントを `BP_Player` に足す方式。

## 追加コンポーネント（`Source/CounterCore/Public/Player/`）

| コンポーネント | 役割・仕様値 |
|---|---|
| `UPlayerCombatComponent` | HP **100** / 防御 **20** / 状態フラグ（通常/攻撃/被弾/気絶）/ ダメージ = 攻撃力−防御 / 被ダメ後の無敵 `PostHitInvulnTime` / のけぞり `HitReactTime` / **攻撃ゲージ**（最大 **10**・パッシブ `PassiveGaugeInterval` 秒で +1・`AddGaugeFromGuardedDamage`・`TryConsumeGauge`・`ForceGaugeMax`）/ ラッシュ与ダメ **1.2倍**（`SetRushActive`） |
| `UPlayerGuardComponent` | 盾耐久 **100** / ガード可能時間 **10s**（`MaxGuardTime`、非ガード時回復）/ クールタイム **2s**（使い切り時のみ）/ 盾自然回復 **120/s ≒ 毎F+2**（成功 **1s** 後から）/ 移動ロック（ガード中 `MaxWalkSpeed=0`）/ `HandleGuardedHit`（HP0・盾削り・ゲージ変換・ヒットストップ）/ 盾耐久0 → `Combat->BeginStun(`**10s**`)` |
| `UPlayerActionComponent` | 攻撃3種（小中大）+ 派生コンボ（`DT_PlayerAttacks` 駆動、小1→小2→小3 / 中1→中2→中3 / 大）/ ゲージ消費（小**1**・中**2**・大**4**枠、始動時）/ 敵へ威力+スタン付与 / 回避ローリング（i-frame `DodgeIFrameStart`〜`End`）/ 優先度ゲート（移動 < ガード < 攻撃 < 回避）/ 被弾で攻撃中断（`bCancelable`、大は不可）/ 近接判定は自前 `UBoxComponent`（`MeleeHitboxExtent` / `MeleeHitboxOffset` でプレイヤー前方に固定）。武器（`WeaponClass`）は**見た目専用**でシェイプは無効化 |

### 命中しない・連打で痙攣、の対策（`b5edf03` 以降）

- **近接判定を武器 BP に依存させない**: `BP_Weapon` のシェイプは仮アセット依存で位置・サイズが不定なため、判定は必ず自前の固定ボックス（初期値 前方 **110cm**・半径 **90×75×90**）で行う。武器は見た目のみ。
- **確実なオーバーラップ取得**: 判定 ON 時に `UpdateOverlaps()` → `GetOverlappingActors` を舐め、さらに**アクティブ中は毎フレーム**重なりを拾う（歩いて入ってきた敵も命中）。
- **連打の痙攣**: コンボ 1 段の `EndTime` より仮モンタージュが長いと毎段で途中リスタート → 再生レートを `EndTime` に合わせて調整。加えてコンボ終了後 `PostComboCooldown`（0.15s）は再始動を無視。
- **回避モーションが終わらない**: 同様に `MM_Forward_Montage` が `DodgeDuration`(0.7s) より長い → `TryDodge` で再生レートを `DodgeDuration` に合わせ、`TickDodge` 終了時に `Montage_Stop`。
- **ゲージで最初の攻撃が出ない問題**: 攻撃は開始時にゲージ消費（仕様: 小1/中2/大4）だが、素の入手源はガード成功のみ。テスト用に `InitialGauge`（初期 **3** 枠）と `PassiveGaugeInterval`（**3s** に短縮）を追加。どちらも調整プロパティ。
- **命中デバッグ**: `bPrintActionEvents` ON で「命中 → 対象 に N ダメージ（残HP）」を画面表示。非敵に重なった場合も出す。

## 入力

- 新規: `IA_P_AttackSmall/Medium/Heavy/Guard/Dodge/Heal` + `IMC_PlayerCombat`（`/Game/Project/Input/Player/`）
- **`IMC_PlayerCombat` のキーマッピングは未設定**。当面は C++ の**フォールバックポーリング**で動く（`bBindFallbackKeys`）:

| アクション | ゲームパッド | キーボード / マウス |
|---|---|---|
| 小攻撃 | X（FaceButton_Left） | **左クリック** / J |
| 中攻撃 | Y（FaceButton_Top） | K |
| 大攻撃 | RB | L |
| 回避 | A | Space |
| 回復 | B | H |
| ガード（ホールド）| RT | 右クリック |

移動・視点・ジャンプは既存の `IA_MyMove` / `IA_MyMouseLook` / `IA_MyJump` のまま。

> **旧攻撃の無効化**: 左クリックは元々 `IMC_MyDefault` の `IA_MyAttack` → 親 BP `CharacterAttack`
> （`MM_Attack_01_Montage` を毎クリック再生 → 連打で痙攣、命中は旧 `RightHand` 頼み）だった。
> `IMC_MyDefault` から `IA_MyAttack` マッピングを削除し、`IMC_PlayerCombat` で左クリックを
> `IA_P_AttackSmall` に張り替えて C++ 側へ一本化した（`84e2738`）。

## 敵側の連携（`MonsterCharacterBase`）

- 敵の攻撃ヒット → プレイヤーに `PlayerGuardComponent`/`PlayerCombatComponent` があれば直接そちらへ
  （ガード中なら `HandleGuardedHit`、通常は `TakeIncomingHit`）。無ければ従来の `ApplyDamage`。
- 敵スタン中 → プレイヤーを `SetRushActive(true)`（ゲージ MAX + 与ダメ 1.2倍）、解除で false。

## HUD（`ACounterCoreHUD`、UMG なしのキャンバス描画）

`BP_MyGameMode` と `GM_Battle` の `HUDClass` に設定済み。仕様書 UI シート準拠の簡易版:

- **敵（ボス）HP**: 画面上部中央、緑バー + **遅延ダメージの赤バー**（`DelayBarCatchupPerSec`）+ 数値 + スタンゲージ細バー
- **プレイヤー攻撃ゲージ**: 画面下中央、10 枠のセグメント（ラッシュ中はオレンジ）+ 数値
- **ガード**: 画面左中央、`>>> ガード中 <<<` の文字 + 盾ゲージ + ガード可能時間バー（＝サークルの代用）、クールタイム表示
- 気絶中は中央に「気絶！」

さらに `UPlayerGuardComponent::bShowGuardText` で、HUD 未設定でもガード中の文字が出る（`AddOnScreenDebugMessage` フォールバック）。

## ジャストガード（`UPlayerGuardComponent`）

ガード開始から `JustGuardWindow`（0.2s）以内に敵の攻撃を受けたらジャストガード:
- 攻撃ゲージ上昇 **×`JustGuardGaugeMultiplier`（2.5）**
- 盾耐久の減少を **×`JustGuardChipScale`（0.25）** に軽減
- `OnJustGuard` 発火、HUD に「JUST GUARD!」フラッシュ

## 決着判定（`UBattleDirectorComponent`、BP_Player に追加）

- 敵 HP0 → `PlayerWin`、プレイヤー HP0 → `PlayerLose`
- 決着で全体スロー（`EndSlowMoTimeScale` 0.15 を `EndSlowMoRealDuration` 1.2s）+ プレイヤー入力停止
- `OnBattleEnded(EBattleResult)` を発火（GM がリザルト遷移に使う）
- HUD に「YOU WIN / YOU LOSE」表示

## 旧 UI の除去

`ACounterCoreHUD` が BeginPlay 後に数回スイープして、`UW_GameUI` / `UW_HpGaugeOnHead` 等
（`LegacyWidgetNameContains`）をビューポートから外す（`bRemoveLegacyWidgets`）。BP 編集不要。

## デバッグ表示の一括 ON/OFF

コンソール（`~`）で:

```
cc.Debug 0   … 画面デバッグをすべて非表示
cc.Debug 1   … 表示（既定）
```

対象: プレイヤー/敵のオンスクリーン文字（`[P]` `[AI]` `[MonsterAttack]`、ガード中表示）、
攻撃判定のワイヤーフレーム、敵の攻撃範囲サークル、敵デバッグキー（U/I/O/K/L）とヒント表示。
各コンポーネントの `b***Events` / `bDrawDebug` との AND（`cc.Debug 0` なら個別設定に関係なく出ない）。

## まだ / 次フェーズ

- **プレイテスト（実機 Play）未実施** — Simulate では possess されないため要 Play。
- HUD は暫定デザイン（文字・単色バー）。本 UI は UMG で作り直し想定。
- IMC のキーマッピング（現状フォールバックポーリング）
- `GM_Battle` の `OnBattleEnded` → リザルトシーン遷移の配線
- SE / エフェクト、本アニメ、`BP_CharacterBase` の旧変数掃除
- **ジャストガード**（中優先度）: `HandleGuardedHit` に `bJustGuard` 引数はあるが判定は未実装（今はゲージ2倍のフックのみ）
- **UI Widget**: 攻撃ゲージ / 盾ゲージ / ガード残りサークル（`GetGaugeNormalized` / `GetShieldNormalized` / `GetGuardTimeNormalized` をバインド）。HP は既存 UI（要 `Combat->OnHpChanged` 再バインド）
- **攻撃優先権の厳密判定**（同時攻撃フレーム検知）: 現状は「被弾 State になったら攻撃中断」で近似
- SE / エフェクト、カメラシェイク（プレイヤー側）、回復の本仕様値
- 本アニメ差し替え（攻撃×7 / 回避 / ガード / 回復 / やられ）
- `BP_CharacterBase` の旧 `TargetCharacter`（`MaxHP` 500 等）の掃除 — プレイヤー HP はコンポーネントが正
