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
| `AMonsterCharacterBase` | 上記を束ねる敵キャラ本体。内蔵の軽量ステートマシン + **行動パターン実行**。武器生成・アタッチ / 攻撃判定 / モンタージュ再生 / ダメージ授受まで C++ 側で完結（`BP_Enemy` は reparent + ディテール設定だけで動く） |

### 行動パターン（仕様書 Monster シート「行動パターン」を順番どおり実行）

`ActionLoop`（コンボ ID の並び）を**先頭から順に**実行する。各ステップ:

- **`()` 付きステップ**（`DT_MonsterCombos.bSkipIfConditionUnmet=true`、= Combo3 背後攻撃）:
  その場で条件（背後にいるか）を判定し、**未達なら移動せずスキップ**して次のステップへ。
- **通常ステップ**（Combo0/1/2/4）:
  そのコンボの `MaxDistanceM` / `MaxAngleDeg` を満たすまで移動（回頭は常時）。
  満たしたら**発生確率**（`TriggerChancePercent`）を判定 → 成功で発動、**失敗なら次のステップへ**（仕様「発生失敗時、次の攻撃へ遷移」）。
- コンボ発動後、**コンボ中は移動しない**。1発目で条件成立 → 以降のコンボ内攻撃は条件無視で続行。
- コンボ完了 → 次のステップ。**やられ割り込みで中断 → 次のステップ**（ただし攻撃5 = `bNoHitstunChain` は連鎖せず攻撃継続）。
- ループが一周したら `LoopRestTime` 秒だけ待機を挟む。
- `bPrintAIEvents`（既定 true）で「step N: ComboX → …」を画面と `LogTemp` に出力。

`ActionLoop` は仕様書 50 行目の並び（11 ステップ）を設定済み:
`Combo3, Combo1, Combo1, Combo0, Combo1, Combo2, Combo3, Combo0, Combo1, Combo3, Combo4`
（`(3)` が 3 箇所。チームの旧 `DT_EnemyAttackSequence` は 10 ステップで内容が違うが、正は仕様書）。

### 武器・攻撃判定・可視化（C++ が面倒を見る）

- `WeaponClass`（`BP_Enemy` で `BP_Weapon` を指定）を `WeaponSocket`（既定 `hand_r`）に `UChildActorComponent` で生成・アタッチ。
  位置がズレるなら `BP_Enemy` の `WeaponActor` コンポーネントの相対トランスフォームで微調整。
- 攻撃判定は **武器（`BP_Weapon`）内のシェイプ（`AttackHitBox`）を優先使用**。武器が無ければ内蔵 `Hitbox`（`UBoxComponent`）にフォールバック。
  判定は攻撃タイムラインの HitActive 区間だけ ON、1スイング1ヒット。ON 中はワイヤーフレーム表示。
- `UMonsterAttackComponent::bDrawDebug`（既定 true）: プレイ中、足元に
  - 各コンボの発動条件（最大距離リング + 角度ウェッジ、条件成立で緑／不成立で灰、背後条件は後方ウェッジ）
  - 進行中の攻撃の接触距離・接触角度、フェーズ名（予兆／発生前／判定／硬直）、経過秒、ダメージ
  を常時描画する。
- `UMonsterAttackComponent::bPrintAttackEvents`（既定 true）: 攻撃の
  **開始 →（判定ON → 判定OFF）→ 終了 / 中断** を画面左上に Print String 表示（`AttackId : ラベル (t=経過秒)`）。
  表示秒数は `AttackEventPrintDuration`（既定 4 秒）。同内容は `LogTemp` にも出力。

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
  `Attack.AttackDataTable = DT_MonsterAttacks` / `Attack.ComboDataTable = DT_MonsterCombos` /
  `WeaponClass = BP_Weapon` /
  `AttackMontages` は全攻撃（Attack01〜Attack05_2）に `MM_Attack_01_Montage` を**仮**設定（本アセット待ち）。

> `BP_CharacterBase` を今後チームが更新しても敵には反映されない。共有したい処理が出たら
> `MonsterCombatComponent` / `MonsterAttackComponent` 相当をコンポーネントとして
> `BP_CharacterBase` 系にも足す方針に切り替える。

## まだ人手が必要（プレースホルダー可 / ロジックは未設定でも動く）

1. **AnimMontage（本アセット差し替え）**: 現在は全攻撃が `MM_Attack_01_Montage` の**仮**。
   本モンタージュができたら `BP_Enemy` の `Attack Montages` を攻撃ごとに差し替え。
   `Reaction Montages`（`Hitstun` / `Stun` / `Dead`）は未設定＝リアクションが鳴らないだけ。
2. **武器の握り位置**: `hand_r` ボーン直付けなので少しズレる可能性あり。
   `BP_Enemy` → `WeaponActor` コンポーネントの相対トランスフォームで調整。
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
