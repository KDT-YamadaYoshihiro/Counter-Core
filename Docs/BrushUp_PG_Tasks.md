# ブラッシュアップ プログラマー仕様 タスク分解

出典: `チーム制作_ブラッシュアップ仕様書_スケジュール.xlsx` / プログラマー仕様（PG-02〜PG-27）
方針: 素材・数値は C++ にハードコードしない。`UPROPERTY(EditAnywhere)` か DataTable 行で変更可能にする。
凡例: [C++] コード / [DT] DataTable 項目追加 / [Ed] エディタ作業（UE CLI 必須） / [?] 仕様の確認事項

## フェーズ0: 土台（他タスクの前提）

| ID | タスク | 種別 | 対象 |
|---|---|---|---|
| T0-1 | 共通 HitStop 設定構造体 `FHitStopSettings`（Duration / TimeScale / 対象=Anim or Global）を作り、プレイヤー・モンスター・ガードの3重実装を統一 | C++ | PlayerActionComponent / MonsterCharacterBase / PlayerGuardComponent |
| T0-2 | 演出イベント構造体 `FCombatFeedback`（SE / Niagara / CameraShake / Trail ON-OFF）を作り、DT 行・コンポーネントから参照 | C++ | 新規 `Common/CombatFeedbackTypes.h` |
| T0-3 | 汎用 AnimNotify クラス（`UAnimNotify_CombatEvent`: EventName 指定）を用意し、Notify 名で HitStart/HitEnd/AttackEnd/DeathEnd/WindupEnd を通知 | C++ | 新規 |

## フェーズ1: アセット差し替え（PG-02, PG-03）

| ID | タスク | 種別 |
|---|---|---|
| T1-1 | PG-02 再アップモーションのインポート（Skeleton/スケール/軸/RootMotion/ループ確認） | Ed |
| T1-2 | PG-02 旧参照の差し替え・Redirector 整理（DT_PlayerAttacks / DT_MonsterAttacks / 各 Montage） | Ed |
| T1-3 | PG-03 新 Montage へ T0-3 の Notify を配置。DT の秒数とどちらを正にするか決定 [?] | Ed |

## フェーズ2: プレイヤー

| ID | 仕様 | タスク | 種別 | 変更可能にする値 |
|---|---|---|---|---|
| T2-1 | PG-04 | HP0 → `EPlayerCombatState::Dead` 追加、死亡 Montage 1回、入力/移動/攻撃/ガード/判定停止、Result へ接続 | C++ | DeathMontage, 結果遷移までの遅延, 死体 Collision プロファイル |
| T2-2 | PG-05 | 回復中の停止処理（ガード側は完了済み。回復の終了/中断/死亡でフラグ解除を確認） | C++ | 回復中に許可する操作フラグ |
| T2-3 | PG-06(P側) | ガード成功で通常ダメージ抑止＋ガード成功 Montage、攻撃者へ `OnAttackGuarded` 通知 | C++ | GuardSuccessMontage, ガード可能角度, HitStop |
| T2-4 | PG-07 | 大攻撃: 一振り=1 DT 行。入力ごとに1行再生、HitStart/HitEnd で判定、1回だけダメージ | C++/DT | DT 行（段数・派生は NextComboId） |
| T2-5 | PG-08 | `FPlayerAttackRow` に `InterruptibleStartFrame`(int32) 追加。到達後＋受付中の入力で即次段 | C++/DT | InterruptibleStartFrame, FrameRate |
| T2-6 | PG-09 | 1:1 同期: 総フレームから InterruptibleStartFrame を自動算出するエディタ用ボタン（端数ルールは設定値） | C++/DT | 端数処理（切捨/切上）, 再生速度 PlayRate |
| T2-7 | PG-10 | 強制終了時の共通終了関数 `EndCurrentAttackStep()`（判定OFF・RootMotion停止・バッファ消去を1回） | C++ | ブレンドアウト時間 |
| T2-8 | PG-20 | 回避開始時に入力方向へ即回転（入力なし=後方 or 前方は設定）、ロックオン中の優先度 | C++ | 入力なし時方向, 基準（カメラ/キャラ）, デッドゾーン |
| T2-9 | PG-22 | 回復薬所持数 `PotionCount`（初期3, 最大値は設定）。0で使用不可、変更デリゲートで HUD 更新 | C++ | InitialPotionCount=3, MaxPotionCount |

## フェーズ3: モンスター

| ID | 仕様 | タスク | 種別 | 変更可能にする値 |
|---|---|---|---|---|
| T3-1 | PG-11 | 死亡: AI/移動/攻撃予約/判定停止、死亡 Montage 1回、撃破後処理（既存 Dead 状態の補強） | C++ | DeathMontage, ラグドール有無, 消滅時間 |
| T3-2 | PG-12 | ダウン/やられ/スタン終了 → `GetUp` 状態で立ち上がり Montage、完了まで AI 停止、Dead 優先 | C++ | GetUpMontage, 無敵有無, 対象となる状態 |
| T3-3 | PG-06(M側) | ガードされた時: 攻撃判定終了＋被ガードやられ Montage 1回 → AI 復帰 | C++ | GuardedReactionMontage, ボス適用フラグ |
| T3-4 | PG-13 | 新モーションの尺に合わせ DT_MonsterAttacks の秒数更新 | DT/Ed | DT 値 |
| T3-5 | PG-14 | 斧の溜め: DT 行に `WindupHoldTime`(=0.5) 追加。WindupEnd Notify で Montage を PlayRate 0 → 再開 | C++/DT | WindupHoldTime |
| T3-6 | PG-15 | 振り返り攻撃: DT 行に `RotationAuthority`(RootMotion/Game) と `MaxTrackingAngleDeg` 追加 | C++/DT | 両項目 |
| T3-7 | PG-16 | 多段判定: DT 行に `HitWindows[]`（Start/End/Damage/再ヒット可否）追加、同一対象ヒット管理 | C++/DT | HitWindows |
| T3-8 | PG-17 | 斧軌道・RootMotion・床追従（IK は ABP 側） | Ed/[?] | — |

## フェーズ4: 共通演出

| ID | 仕様 | タスク | 種別 | 変更可能にする値 |
|---|---|---|---|---|
| T4-1 | PG-18 | 攻撃/命中/ガード/回復/回避/死亡/UI の SE・VFX・Shake・Trail を T0-2 経由で再生（DT 行 or コンポーネント設定） | C++ | 全 SE/VFX アセット |
| T4-2 | PG-19 | 命中時ヒットストップを T0-1 に統一。攻撃別時間は DT 行、多重発生ガード、空振り時なし | C++/DT | 攻撃別 HitStop |

## フェーズ5: UI

| ID | 仕様 | タスク | 種別 | 変更可能にする値 |
|---|---|---|---|---|
| T5-1 | PG-21 | タイトル: ゲームパッド接続検出（起動時＋ホットプラグ）で文言切替、対応入力で1回だけ遷移 | C++ | 両文言(FText), 画像 |
| T5-2 | PG-22 | HUD に回復薬残数表示（T2-9 のデリゲート購読） | C++ | 位置/フォント/アイコン |
| T5-3 | PG-23 | 操作説明画面: コントローラー画像＋操作表。表は DataTable `DT_ControlGuide`（ボタン名/操作名） | C++/DT | 画像, 表行 |
| T5-4 | PG-27 | HP/攻撃ゲージ枠・装飾の組込み（`FGaugeImageConfig` に Frame/Decoration/9-slice 追加） | C++/Ed | 素材, 位置, Margin |

## フェーズ6: 調整・検証（コード外）

| ID | 仕様 | タスク | 種別 |
|---|---|---|---|
| T6-1 | PG-24 | 予備動作/判定/硬直/次行動を DT に一元化済みか確認し数値調整 | DT |
| T6-2 | PG-25 | 回帰テスト（PIE / 実機） | Ed |
| T6-3 | PG-26 | 差分整理・Redirector 修正・コミット分割 | Ed/Git |

## 確認事項 [?]（実装前に決めたいもの）

1. PG-08: 既存 DT は「秒」管理（`ComboWindowStart`）。フレーム(int32)を追加して秒と併用するか、秒に統一するか。
2. PG-03: 判定タイミングの正は DT の秒数か、Montage の Notify か。
3. PG-06: ガード可能角度・ボスへの適用・ガード成功時ヒットストップ有無。
4. PG-04/11: 死体 Collision・消滅有無・リザルト遷移タイミング。
5. PG-22: 回復のコストは現状「ゲージ3」。回復薬制に置き換えか併用か。
