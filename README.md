# 映像後処理（Post FX）

[English](README.en.md)

Steam 版『侍道4』に、アンチエイリアスとカメラの動きによるモーションブラーを追加する Mod です。

ゲーム外の試験を終えた試作版です。画質・設定タブの表示・各画面での UI 除外・デバイス復旧・他 Mod との併用は要ゲーム内確認です。

## 機能

- FXAA：輪郭のギザギザを軽減します。細かな模様が軟らかくなることがあります。
- SMAA 1x：輪郭の形を調べて処理する方式です。公式実装の High プリセットを使用します。
- SSAA：3Dシーンを縦横2倍の解像度で描き、4画素を平均して縮小します。FXAA・SMAA と併用できます。UI の解像度は変えません。
- モーションブラー：カメラの動きに合わせて映像をぼかします。カメラが止まったまま動く人物や刀へのブラーは対象外です。
- カメラに近い部分は、ブラーの強さと最大幅を距離に応じて抑えます。プレイヤーのほか、手前のNPCや物にも同じ補正が掛かります。
- 各効果を個別に切り替えられます。効果は 3D の映像に掛かり、文字や UI には掛かりません。
- カメラや深度が利用できない描画ではブラーを飛ばし、アンチエイリアスは独立して適用します。
- ゲームのウィンドウが最小化中や背面にある間はブラーだけを止め、アンチエイリアスと SSAA は続けます。
- シーン切替、カメラ切替、大きな視点の変化、デバイス復旧などの後は、ブラーをいったん止めてから再開します。

## 設定

タイトル画面とプレイ中の設定に「映像後処理」（英語では「Post FX」）のタブを追加します。SSAA 以外の変更は次の描画から反映されます。SSAA は設定を保存してゲームを終了し、再起動すると反映されます。SSAA をいまの状態から変えると、その下に再起動が必要なことを知らせる文が出ます。

| 項目 | 選択肢 | 既定値 |
| --- | --- | --- |
| アンチエイリアス | 無効 / FXAA / SMAA | 無効 |
| モーションブラー | 無効 / 有効 | 無効 |
| ブラーの強さ | 弱 / 標準 / 強 | 標準 |
| ブラーの品質 | 低 / 標準 / 高 | 標準 |
| SSAA | 無効 / 縦横2倍（4サンプル） | 無効 |

強さは露光量、品質はサンプル数（4 / 8 / 12）を変えます。品質を上げると GPU の負荷が増えます。具体的な処理時間と FPS は未測定です。

SSAA は描画する画素数が4倍になり、GPU の負荷とメモリ使用量が大きく増えます。デバイスの最大テクスチャ寸法、または拡大後のシーンの上限（64 Mi画素）を超える場合は通常の解像度で描画します。上限内でも、GPU のメモリが足りるとは限りません。まず SMAA 単独で比較し、余裕があれば SSAA を試してください。

ブラー無効時も強さ・品質の値を保持します。設定値は `mods/post_fx/config.ini` にあり、ゲームがオプションを保存するときに書き込みます。直接編集する場合はゲームを終了してください。値は大文字・小文字を区別しません。項目がない場合や不正な値の場合は既定値を使います。

| セクション | 項目 | 既定値 | 説明 |
| --- | --- | --- | --- |
| `antialiasing` | `method` | `off` | アンチエイリアス。`off`（無効）/ `fxaa` / `smaa` |
| `antialiasing` | `supersampling` | `off` | SSAA。`off`（無効）/ `2x`（縦横2倍・4サンプル）。次の起動から反映 |
| `motion_blur` | `enabled` | `0` | モーションブラー。`1` で有効、`0` で無効 |
| `motion_blur` | `strength` | `normal` | ブラーの強さ。`low` / `normal` / `high` |
| `motion_blur` | `quality` | `normal` | ブラーの品質。`low` / `normal` / `high` |

更新するときに今の設定を残したい場合は、`mods/post_fx/config.ini` を上書きせずに、`mod.dll` だけを置き換えてください。

## インストール（開発版）

> [!WARNING]
> 事前に [wots4-mods（ModLoader）](https://github.com/nagataaaas/wots4-mods) を導入してください。

試作版のため、Release にはまだ入れていません。次の [ビルド](#ビルド) の手順でビルドしてから入れてください。

1. ゲームを終了してください。
2. ビルドで得られた `build/game/mods/post_fx/` フォルダを、侍道4のインストールフォルダ（`WayOfTheSamurai4.exe` がある場所）の `mods` フォルダへコピーしてください。
3. `{侍道4インストールフォルダ}/mods/post_fx/mod.dll` があることを確認し、ゲームを起動してください。

## アンインストール

1. ゲームを終了してください。
2. `{侍道4インストールフォルダ}/mods/post_fx/` フォルダを削除してください。中の設定（`config.ini`）も消えます。

## ビルド

[wots4-mods（ModLoader）](https://github.com/nagataaaas/wots4-mods) の SDK、MSVC、Windows SDK（`fxc.exe` を含む）が必要です。x86 / C++17 でビルドします。シェーダーはビルド時にコンパイルして DLL に埋め込むため、実行時にシェーダーコンパイラや追加のシェーダーファイルは不要です。

```powershell
cmake -S . -B build -A Win32 -DWOTS4_SDK_DIR="C:/path/to/wots4-mods"
cmake --build build --config Release
ctest --test-dir build -C Release -LE needs_exe --output-on-failure
```

出力は `build/game/mods/post_fx/mod.dll`。

試験の内容と残る確認事項は [docs/VALIDATION.md](docs/VALIDATION.md) を参照してください。

## ライセンス

MinHook のライセンスは [docs/licenses/minhook.txt](docs/licenses/minhook.txt)、SMAA のライセンスは [docs/licenses/smaa.txt](docs/licenses/smaa.txt) にあります。ゲームの exe と既存シェーダーソースは配布物に含めません。
