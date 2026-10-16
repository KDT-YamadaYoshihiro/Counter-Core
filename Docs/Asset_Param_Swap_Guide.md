# 素材・数値の差し替え手順（プランナー向け）

C++ に素材パスや調整値は直書きしていない。差し替えはすべてエディタ（BP のクラスのデフォルト値）か DataTable で行う。

## 1. 素材の差し替え場所

| 対象 | 場所 | 項目 |
|---|---|---|
| モンスターの攻撃モーション | `DT_MonsterAttacks` の各行 | `Montage`（未設定なら BP_Enemy の `AttackMontages[行名]`） |
| モンスターの攻撃 VFX | `DT_MonsterAttacks` の各行 | `AttackVFX`（未設定なら BP_Enemy の `AttackVFX[行名]`） |
| モンスターのメッシュ・AnimBP | `BP_Enemy` > Mesh | Skeletal Mesh / Anim Class |
| モンスターの待機・走り | `BP_Enemy` | `LocomotionIdleAnim` / `LocomotionRunAnim` |
| モンスターの被弾・スタン・被ガード | `BP_Enemy` | `ReactionMontages` / `GuardedReactionMontage` |
| プレイヤーの攻撃モーション | `DT_PlayerAttacks` の各行 | Montage 列（「Sync」ボタンで Montage と行を 1:1 に揃えられる） |
| プレイヤーのガード成功・回避・回復・死亡 | `BP_PlayerCharacter`（プレイヤー BP） | 各 `*Montage` 項目 |
| 操作説明 | `DT_ControlGuide` | 行ごとに表示文言・アイコン |
| HUD のゲージ枠 | `BP_CounterCoreHUD` | ゲージ枠テクスチャと 9 スライスの Margin |
| メニュー SE | `BattleDirector`（レベル上の BP） | 決定・キャンセル・カーソル SE |

## 2. 数値の調整場所

| 対象 | 場所 |
|---|---|
| 攻撃ごとの判定時間・ダメージ・多段ヒット区間（`HitWindows`）・溜め（`WindupHoldTime`）・回頭上限（`MaxTrackingAngleDeg`）・ヒットストップ（`HitStop`） | `DT_MonsterAttacks` / `DT_PlayerAttacks` の各行 |
| ガード可能角度、回復量・回復薬の数、回避距離 | プレイヤー BP のクラスのデフォルト値 |
| 回復のコスト方式 | プレイヤー BP の `HealCostMode`（初期値: 回復薬のみ） |
| 判定タイミングの決め方 | `bUseNotifyHitWindow`（オフ: DT の秒数 / オン: AnimNotify `AnimNotify_CombatEvent`） |
| 死亡時の演出（ラグドール・消えるまでの時間） | `BP_Enemy` の `bRagdollOnDeath` ほか |

## 3. 新しい素材に差し替える流れ

1. Designer: 素材（FBX/テクスチャ/Niagara）を `/Game/` 以下にインポートする
2. Planner: 上の表の場所を開き、項目に新しいアセットを指定する
3. Notify 駆動にする場合: Montage に `AnimNotify_CombatEvent` を置き、`bUseNotifyHitWindow` をオンにする
4. 保存して PIE で確認する（C++ のビルドは不要）
