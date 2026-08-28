# CounterCore モジュール（敵AI / 戦闘ロジック）

`feature/EnemyState-cpp` で追加。仕様書 `Monster` / `攻撃詳細` シートのボス敵を **C++ で実装**した。
これによりこのプロジェクトは **C++ プロジェクト**になった（各自 Visual Studio 2022 + C++ ワークロードが必要）。

## このモジュールが持つもの

| クラス / 型 | 役割 |
|---|---|
| `EMonsterState` | 待機 / 移動 / 攻撃 / やられ / スタン / 死亡 |
| `FMonsterStatus` | HP / MaxHp / Defence / Stun / MaxStun（すべて `int32`。旧 `S_EnemyStatus` の `MaxHP`/`MaxStun` が bool だったバグを修正した版） |
| `FMonsterAttackFrameData` | 攻撃1発分のフレームデータ（`DT_MonsterAttacks` の行構造体） |
| `FMonsterComboData` | コンボ1つ分（`DT_MonsterCombos` の行構造体） |
| `UMonsterCombatComponent` | 被ダメージ計算 / スタン蓄積 / 15秒自然解除 / やられ / 死亡。結果はデリゲートで通知 |
| `UMonsterAttackComponent` | コンボ選択（距離 / 角度 / 背後 / 発生確率）+ 攻撃タイムライン（予兆→軸合わせ停止→判定ON/OFF→硬直→終了）+ 予兆中の軸合わせ回頭 |
| `AMonsterCharacterBase` | 上記を束ねる敵キャラ本体。内蔵の軽量ステートマシンで全部回す。**アニメ / 判定コリジョン / ダメージ授受まで C++ 側で完結**（`BP_Enemy` は reparent + ディテール設定だけで動く） |

## `BP_Enemy` の現状（このブランチで実施済み）

- 親クラスを `BP_CharacterBase`（BP）→ `MonsterCharacterBase`（C++）に **reparent 済み**。
  - `BP_CharacterBase` はプレイヤー入力・タッチ IF 用の共有ベースで、敵AIは元々その機能を使っていない
    （敵は独自ステートマシンで動いていた）。敵に必要だったのは **スケルタルメッシュ / AnimBP /
    カプセル / 移動設定** だけなので、それらを `BP_Enemy` に直接設定し直した:
    - Mesh: `SKM_Quinn_Simple`、AnimClass: `ABP_MyCharacter`、Mesh 相対 `(0,0,-89) / (0,270,0)`
    - Capsule `R35 / H90`、`MaxWalkSpeed=350`、`OrientRotationToMovement=false`（回頭は C++ が管理）
- 旧 `EventGraph` / `ChangeState` グラフは **削除済み**（C++ ステートマシンと二重実行になるため）。
  旧サブステート BP（`BP_Enemy_Idle` / `_Run` / `_Attack` / `_Stun` / `_Dead`）と `StateMap` は**未使用**。
- ディテール設定済み: `DetectionRange=1000` / `EngageRange=300` / `ChaseSpeed=350` /
  `ActionLoop = [Combo3, Combo1, Combo1, Combo0, Combo1, Combo2, Combo3, Combo0, Combo1, Combo3, Combo4]` /
  `Combat.Status`（HP500 / 防御40 / スタン上限100）/
  `Attack.AttackDataTable = DT_MonsterAttacks` / `Attack.ComboDataTable = DT_MonsterCombos`。

> `BP_CharacterBase` を今後チームが更新しても敵には反映されない。共有したい処理が出たら
> `MonsterCombatComponent` / `MonsterAttackComponent` 相当をコンポーネントとして
> `BP_CharacterBase` 系にも足す方針に切り替える。

## まだ人手が必要（プレースホルダー可 / ロジックは未設定でも動く）

1. **AnimMontage**: `BP_Enemy` のディテール →
   - `Attack Montages`（`Attack01`…`Attack05_2` → モンタージュ）
   - `Reaction Montages`（`Hitstun` / `Stun` / `Dead` → モンタージュ）
   未設定なら「アニメが鳴らないだけ」でロジックは進む。代用は Mannequin 同梱アニメで可。
2. **攻撃判定ボックスの位置合わせ**: C++ が `Hitbox`（`UBoxComponent`）を自動生成する。
   `Hitbox Socket` / `Hitbox Extent` を武器・腕に合わせて調整。判定の ON/OFF・多段ヒット防止は C++ 側が管理。
3. **プレイヤー側の配線**:
   - ガード成立時 → 敵の `Combat->HandleIncomingHit(power, bGuardedByPlayer=true)` を呼ぶ（→ やられ + スタン +10）
   - プレイヤーの通常攻撃ヒット → 敵に `UGameplayStatics::ApplyDamage` するだけ（C++ の `TakeDamage` が処理）
   - 攻撃ゲージ: `Combat->OnStunned` を購読してスタン時に全回復
4. **HP / スタンゲージ Widget**: `Combat->Status.Hp` / `Combat->GetStunNormalized()` をバインド。`OnDamaged` / `OnStunned` で更新。
5. **バトル終了フック**: `Combat->OnDied` を `GM_Battle` に繋いでリザルトへ。
6. **数値調整・プレイテスト**。

## ビルド

エディタを閉じて:

```
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" CounterCoreEditor Win64 Development -Project="<...>\CounterCore.uproject" -WaitMutex
```

またはエディタ起動中に Live Coding（Ctrl+Alt+F11）。

> 補足: リポジトリに `git config core.quotepath false` を設定済み（日本語ファイル名で UBT が落ちるのを回避）。
