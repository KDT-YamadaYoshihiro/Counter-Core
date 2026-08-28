# タイトルシーン — `feature/PlayerImpl-cpp`

仕様書「全体フロー / タイトルシーン・タイトル画面の動き・画面遷移」。

## 現状の分担

| 要素 | 実装 |
|---|---|
| 1枚絵 UI（`T_TitleLogo`）+「PUSH A BUTTON」 | **既存** `WBP_Title`（`GM_Title` が BeginPlay で生成・AddToViewport） |
| A ボタン → 黒フェードアウト → シーン切替 | **既存** `WBP_Title`（`TriggerFadeOut` / `FadeDuration`、`WBP_Transition` 経由で `OpenLevel(LV_Ingame)`） |
| フェード時間 0.1 秒単位で調整 | **既存** `WBP_Title.FadeDuration`（float） |
| **ドローンでカメラがぐるぐる背景を回る** | **新規 C++** `ATitleSceneController`（このコミット） |
| 背景モデル（ゲーム内の背景を使用） | **未** — `LV_Title` に配置が必要（下記） |
| A 以外のボタン無効 | `WBP_Title` は A と P（デバッグ）に反応。P は不要なら `WBP_Title` グラフから削除 |

## `ATitleSceneController`（`Title/TitleSceneController.h`）

`LV_Title` に 1 つ配置済み。アクター位置＝**周回中心**。BeginPlay で `ACameraActor` を
生成し `SetViewTargetWithBlend` で視点にして、毎 Tick その周りを回す。

| プロパティ | 既定 | 意味 |
|---|---|---|
| `OrbitRadius` | 1700 | 周回半径 cm |
| `OrbitHeight` | 450 | 周回中心からのカメラ高さ cm |
| `OrbitDegreesPerSecond` | 6 | 周回速度 度/秒 |
| `LookAtHeightOffset` | 180 | 注視点の高さ cm |
| `CameraFOV` | 78 | |
| `StartAngleDegrees` | 0 | 開始角度 |
| `BobAmplitude` / `BobPeriod` | 40 / 8 | カメラのゆるい上下揺れ |

`GM_Title` は `default_pawn_class = None`。ポーンは出ず、PC + このカメラだけ。

## 背景 = 実ステージを外から周回

仕様「ゲーム内で使う背景モデル」に合わせ、`LV_Title` に **`LV_Ingame` を常時ロードの
サブレベル**として追加し、`TitleSceneController` を大きめの半径（既定 5500cm / 高さ
2400cm）でその外周を回す。

**サブレベルが未追加なら**（エディタで手作業、20 秒）:
1. `LV_Title` を開く → メニュー Window ▸ Levels
2. Add Existing… ▸ `LV_Ingame` を選択 → 追加した行を右クリック ▸ Change Streaming Method ▸ **Always Loaded**
3. 明るすぎ / 昼夜がちぐはぐなら `LV_Title` の `DirectionalLight` を無効化（`LV_Ingame` 側の照明を使う）
4. `TitleSceneController` を戦闘エリアの中心あたりへ移動、Details で `OrbitRadius` /
   `OrbitHeight` / `LookAtHeightOffset` を画に合わせて調整

> `BP_Enemy` も一緒に読み込まれる（アリーナに立つボス）。タイトルの画としては
> むしろ良いのでそのまま。不要なら `LV_Ingame` 側で対応。
> ※Python から `add_level_to_world` でも入るが、500+ アクター読み込みで
> エディタが固まりやすいので手作業推奨。
