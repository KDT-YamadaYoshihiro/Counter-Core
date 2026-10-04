# 引き継ぎ (2026-10-05 08:11 / feature/after/prog1)

次の一手: エディタで `LV_Ingame` を開いて Play（Alt+P）し、敵の待機/走りモーション（Tポーズ修正）と、死亡モーション後に倒れたまま止まるかを目視確認する。

## 完了
- ブラッシュアップ仕様（`Docs/BrushUp_PG_Tasks.md`）のプログラマー担当分の実装（コミット `fc2cf94`）
- 素材・数値を DT / BP で差し替え可能に。手順書: `Docs/Asset_Param_Swap_Guide.md`
- 仮仕様は確定: 回復コスト=回復薬のみ（`HealCostMode`）、判定タイミング=DT秒数（`bUseNotifyHitWindow`=false）
- 敵のTポーズ修正: `MonsterAnimInstance.cpp` の `PreUpdate` で待機/走りアニメを毎フレーム再生ノードへ反映
- 敵の死亡: ラグドール無効（BP_Enemy と LV_Ingame/LV_Title 配置の `bRagdollOnDeath`=False）、Dead に `AM_MonsterStanStart` を仮設定、終端のブレンド前でポーズ固定（`MonsterCharacterBase.cpp:1157`）。PIE で一時停止・物理オフを確認済み

## 残り（優先順）
- ユーザーによる PIE での目視確認（Tポーズ／死亡ポーズ）
- 「アニメーションの遷移と再生の違和感」の症状をユーザーから聞いて原因を絞る（候補: 待機⇔走りのブレンド、Montage のブレンド時間、振り向きモーション無し、判定とモーションのずれ）
- 問題なければ `feature/after/prog1` の PR 作成（ユーザーが実施予定）

## 保留
- 本物の死亡モーション — デザイナーの素材待ち（BP_Enemy `ReactionMontages` → Dead を差し替えるだけ）
- T1-1〜T1-3、T3-4、T3-8 — アセット待ち

## 再開に必要なもの
- UE 5.7 と Visual Studio 2022。ビルド: `"$LOCALAPPDATA/uecli/uecli.exe" code build --project Counter_Core`
- エディタが UE CLI に応答しなくなることがある（Live Coding 中など）。その場合はエディタを終了してフルビルド
