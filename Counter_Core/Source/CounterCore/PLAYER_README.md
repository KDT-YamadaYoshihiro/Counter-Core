# プレイヤー実装（C++ コンポーネント）— `feature/PlayerImpl-cpp`

仕様書 `Player` / `Battle` シートの**優先度「高」**まで。既存の移動・カメラ・ロックオン・
近接メッシュ・HP UI は温存し、C++ コンポーネントを `BP_Player` に足す方式。

## 追加コンポーネント（`Source/CounterCore/Public/Player/`）

| コンポーネント | 役割・仕様値 |
|---|---|
| `UPlayerCombatComponent` | HP **100** / 防御 **20** / 状態フラグ（通常/攻撃/被弾/気絶）/ ダメージ = 攻撃力−防御 / 被ダメ後の無敵 `PostHitInvulnTime` / のけぞり `HitReactTime` / **攻撃ゲージ**（最大 **10**・パッシブ `PassiveGaugeInterval` 秒で +1・`AddGaugeFromGuardedDamage`・`TryConsumeGauge`・`ForceGaugeMax`）/ ラッシュ与ダメ **1.2倍**（`SetRushActive`） |
| `UPlayerGuardComponent` | 盾耐久 **100** / ガード可能時間 **10s**（`MaxGuardTime`、非ガード時回復）/ クールタイム **2s**（使い切り時のみ）/ 盾自然回復 **120/s ≒ 毎F+2**（成功 **1s** 後から）/ 移動ロック（ガード中 `MaxWalkSpeed=0`）/ `HandleGuardedHit`（HP0・盾削り・ゲージ変換・ヒットストップ）/ 盾耐久0 → `Combat->BeginStun(`**10s**`)` |
| `UPlayerActionComponent` | 攻撃3種（小中大）+ 派生コンボ（`DT_PlayerAttacks` 駆動、小1→小2→小3 / 中1→中2→中3 / 大）/ ゲージ消費（小**1**・中**2**・大**4**枠、始動時）/ 敵へ威力+スタン付与 / 回避ローリング（i-frame `DodgeIFrameStart`〜`End`）/ 優先度ゲート（移動 < ガード < 攻撃 < 回避）/ 被弾で攻撃中断（`bCancelable`、大は不可）/ 近接判定は自前 `UBoxComponent` を `hand_r` に生成 |

## 入力

- 新規: `IA_P_AttackSmall/Medium/Heavy/Guard/Dodge/Heal` + `IMC_PlayerCombat`（`/Game/Project/Input/Player/`）
- **`IMC_PlayerCombat` のキーマッピングは未設定**。当面は C++ の**フォールバックポーリング**で動く（`bBindFallbackKeys`）:

| アクション | ゲームパッド | キーボード |
|---|---|---|
| 小攻撃 | X（FaceButton_Left） | J |
| 中攻撃 | Y（FaceButton_Top） | K |
| 大攻撃 | RB | L |
| 回避 | A | Space |
| 回復 | B | H |
| ガード（ホールド）| RT | 右クリック |

移動・視点・ジャンプは既存の `IA_MyMove` / `IA_MyMouseLook` / `IA_MyJump` のまま。

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

## まだ / 次フェーズ

- **プレイテスト（実機 Play）未実施** — Simulate では possess されないため要 Play。
- プレイヤー HP UI は既存 Widget のまま（HUD には未描画）。統一するなら既存 Widget を `Combat->OnHpChanged` に再バインド、または HUD にプレイヤー HP バーを追加。
- HUD は暫定デザイン（文字・単色バー）。本 UI は UMG で作り直し想定。
- IMC のキーマッピング（現状フォールバックポーリング）
- **ジャストガード**（中優先度）: `HandleGuardedHit` に `bJustGuard` 引数はあるが判定は未実装（今はゲージ2倍のフックのみ）
- **UI Widget**: 攻撃ゲージ / 盾ゲージ / ガード残りサークル（`GetGaugeNormalized` / `GetShieldNormalized` / `GetGuardTimeNormalized` をバインド）。HP は既存 UI（要 `Combat->OnHpChanged` 再バインド）
- **攻撃優先権の厳密判定**（同時攻撃フレーム検知）: 現状は「被弾 State になったら攻撃中断」で近似
- SE / エフェクト、カメラシェイク（プレイヤー側）、回復の本仕様値
- 本アニメ差し替え（攻撃×7 / 回避 / ガード / 回復 / やられ）
- `BP_CharacterBase` の旧 `TargetCharacter`（`MaxHP` 500 等）の掃除 — プレイヤー HP はコンポーネントが正
