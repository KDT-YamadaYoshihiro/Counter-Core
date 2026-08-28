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

## 背景モデルの置き方（TODO / 手作業）

`LV_Title` は今 sky/light と `SM_SkySphere` のみ。仕様の「ゲーム内で使う背景モデル」を出すには
どれか:

1. **推奨**: `LV_Ingame` を指す **Level Instance** アクターを `LV_Title` に配置し、
   `TitleSceneController` をその中心へ。`LV_Title` 側の `DirectionalLight` /
   `SkyLight` / `SkyAtmosphere` / `ExponentialHeightFog` / `VolumetricCloud` は
   二重になるので削除（`LV_Ingame` 側の照明を使う）。
2. `LV_Ingame` を常時ロードのサブレベルにする（1 と同じ照明重複の注意）。
3. 背景用の軽量メッシュを数点だけ `LV_Title` に直接配置。

> 一度サブレベル追加を自動でやろうとしたが、`BP_Enemy` まで持ち込む・照明重複など
> 副作用が大きいので、レベルの体裁はレベルデザイン側で確定させる方針にした。
