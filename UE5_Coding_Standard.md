# Unreal Engine 5 開発命名規則ガイドライン

本ドキュメントは、プロジェクトにおけるC++コードおよびエディタ内アセットの命名規則を定めたものです。コードの可読性向上、およびアセット管理の効率化のために必ず遵守してください。

---

## 1. C++ クラス命名規則（UE5公式ルール）

Unreal Engineのコンパイルシステム（UHT）が正しくクラスを認識・処理するために、クラス名の先頭には必ず指定の**接頭辞（プレフィックス）**を大文字で付与します。

| 接頭辞 | 対象となるクラス | 具定例 |
| :--- | :--- | :--- |
| **`A`** | **Actor:** レベル（世界）に配置・スポーンできるオブジェクト | `AMyCharacter`, `AMyGameModeBase`, `AWeapon` |
| **`U`** | **Object / Component:** 配置できない内部ロジックやコンポーネント | `UInteractionComponent`, `UPlayerAttributeSet` |
| **`F`** | **Struct:** 純粋なC++の構造体 | `FPlayerData`, `FItemInfo` |
| **`E`** | **Enum:** 列挙型 | `EPlayerState`, `EItemType` |
| **`I`** | **Interface:** インターフェースクラス | `IInteractableInterface` |

---

## 2. C++ 変数・関数の命名規則

コード内の記述はすべて**パスカルケース（PascalCase）**で統一します。単語の区切りは大文字にし、アンダースコア（`_`）は原則使用しません。

### 2.1 メンバ変数（Member Variables）
* 原則として**パスカルケース**で記述します。
* **`bool` 型（真偽値）のみ、先頭に小文字の `b` を付与**し、その直後をパスカルケースにします（UE公式ルール）。
* ポインタ変数などに対する特別な接頭辞（`p` や `ptr` など）は付与しません。

| 変数の型 | ルール | 良い例 | 悪い例 |
| :--- | :--- | :--- | :--- |
| `int32` / `float` | パスカルケース | `PlayerHealth`, `WalkSpeed` | `playerHealth`, `walk_speed` |
| `bool` | `b` + パスカルケース | `bIsDead`, `bCanJump` | `isDead`, `b_isDead`, `IsDead` |
| `FString` / `FName` | パスカルケース | `PlayerName`, `ItemRowName` | `playerName`, `item_name` |
| クラスポインタ | パスカルケース | `TargetActor`, `MyController` | `pTargetActor`, `target_actor` |

### 2.2 関数（Functions）
* すべて**パスカルケース**で記述します。
* 原則として「動詞」から開始し、その関数が何を行うかを明確にします。

| 関数の目的 | 良い例 | 悪い例 |
| :--- | :--- | :--- |
| 状態の変更・実行 | `TakeDamage()`, `SpawnWeapon()` | `takeDamage()`, `damage_take()` |
| 値の取得（Get） | `GetHealth()`, `GetMovementSpeed()` | `getHealth()`, `FetchHealth()` |
| 判定（戻り値 bool） | `IsAlive()`, `HasEnoughMana()` | `checkAlive()`, `is_alive()` |

---

## 3. エディタ内アセット命名規則

コンテンツブラウザ内での視認性と検索性を高めるため、アセットは**「大文字の接頭辞_アセット名（パスカルケース）」**の形式で統一します。

### 3.1 主要アセット接頭辞（プレフィックス）一覧

| アセットの種類 | 接頭辞 | 命名例 |
| :--- | :--- | :--- |
| **ブループリントクラス** | `BP_` | `BP_MyCharacter`, `BP_BaseWeapon` |
| **マテリアル** | `M_` | `M_Brick_Wall` |
| **マテリアルインスタンス** | `MI_` | `MI_Brick_Wall_Red` |
| **テクスチャ（カラー/ベース）** | `T_` | `T_Brick_Wall_D` （※末尾のDはDiffuse/ベースカラー） |
| **テクスチャ（ノーマル）** | `T_` | `T_Brick_Wall_N` （※末尾のNはNormalマップ） |
| **スタティックメッシュ** | `SM_` | `SM_Chair`, `SM_Pillar` |
| **スケルタルメッシュ** | `SK_` | `SK_Mannequin`, `SK_EnemyBoss` |
| **アニメーションシーケンス** | `AS_` | `AS_Player_Run`, `AS_Player_Jump` |
| **アニメーションBP** | `ABP_` | `ABP_Player` |
| **レベル / マップ** | `LV_` | `LV_Title`, `LV_MainStage` |
| **入力アクション（Enhanced Input）** | `IA_` | `IA_Move`, `IA_Jump`, `IA_Interact` |
| **マッピングコンテキスト（EI）** | `IMC_` | `IMC_DefaultKeyboard`, `IMC_Gamepad` |
| **サウンド（Waveファイル）** | `S_` | `S_Explosion`, `S_Footstep` |
| **サウンドキュー** | `SC_` | `SC_BackgroundMusic` |

---

## 4. 推奨フォルダ構成（Content/ 直下）

アセットの散らかりを防ぐため、`Content` フォルダの直下は機能や種類ごとにトップフォルダを分けて管理します。

```text
Content/
  ├── Blueprints/      (C++を継承したBPクラス、ゲームモードBPなど)
  ├── Characters/      (キャラクターごとのメッシュ、アニメーション、BP)
  │     ├── Player/
  │     └── Enemy/
  ├── Input/           (IAやIMCなどの入力設定アセット)
  ├── Maps/            (LV_ タイトルや各ステージのレベルファイル)
  ├── Materials/       (共通マテリアル、マテリアルインスタンス、テクスチャ)
  └── UI/              (ウィジェットブループリントなど)