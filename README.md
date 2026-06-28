# LearningQuickDraw

Apple QuickDraw (1984) のアルゴリズムを起点に、現代の描画パイプライン・流体エフェクト最適化を実装したプロジェクトです。

---

## ビルド済みバイナリ（推奨）

CMake 環境がない場合は **GitHub Releases** からダウンロードして即実行できます。

| パッケージ | 内容 |
|---|---|
| `mini-renderer-windows.zip` | `mini-renderer.exe`（SDL2 静的リンク済み、追加DLL不要） |
| `mini-renderer-gpu-windows.zip` | `mini-renderer-gpu.exe` + `shaders/` フォルダ |

> **Releases:** https://github.com/manato1201/LearningQuickDraw/releases

---

## プロジェクト構成

```
LearningQuickDraw/
├── mini-renderer/          Phase 1: CPU ソフトウェアレンダラー (C++/SDL2)
├── mini-renderer-gpu/      Phase 2: GPU レンダラー (OpenGL/GLSL + HLSL)
├── blender-fluid-viewer/   Phase 3-1: Blender アドオン (Python/bpy)
├── houdini-fluid-hda/      Phase 3-2: Houdini HDA (Python SOP)
├── render-optimizer/       Phase 3-3: 汎用最適化ライブラリ (Python)
├── ae-fluid-effect/        Phase 4: After Effects プラグイン (C++/AE SDK)
├── docs/                   ドキュメント・講義資料 (MD + HTML)
└── generate_test_vol.py    テスト用 .vol キャッシュ生成スクリプト
```

---

## Phase 1 — ミニソフトウェアレンダラー

**実装内容:** DDA ライン描画 / 固定小数点最適化 / スラブ三角形ラスタライズ / Z バッファ / Pitch・Yaw・Roll 回転 / 透視投影

**パフォーマンス最適化済み:**
- `fillTriangleZ` 内ループ: ピクセルごとの除算 → 増分Z加算に変更
- バックフェイスカリング: スクリーン空間符号付き面積で裏面を除去（約50%削減）
- `perspectiveProjectFast`: FOV係数をフレームループ外で1回だけ計算
- `fillTriangle`: 固定小数点スロープ（スキャンラインごとの除算を廃止）
- `Framebuffer`: 境界チェックなしの高速パス追加

**動作確認済み:** ✅ リアルタイム回転キューブ表示

### ビルド・実行（ソースから）

```bash
cd mini-renderer
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
./build/Release/mini-renderer.exe
```

### 操作方法

| キー | 動作 |
|---|---|
| `Space` | ワイヤーフレーム / ソリッド 切り替え |
| `ESC` | 終了 |

---

## Phase 2 — GPU レンダラー

**実装内容:** OpenGL 3.3 Core / GLSL バーテックス・フラグメントシェーダー / HLSL 参照ファイル / CPU・GPU タイマー比較

**パフォーマンス最適化済み:**
- `matRotXYZ`: 3回の行列生成+2回の行列積 → sin/cos6値から結合回転行列を直接構築（128mul+96add → 18mul+6add）

**動作確認済み:** ✅ ウィンドウタイトルに FPS・ms 表示

### ビルド・実行（ソースから）

**依存: [GLAD2](https://gen.glad.dav1d.de/) (cmake FetchContent で自動取得)**

```bash
cd mini-renderer-gpu
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
./build/Release/mini-renderer-gpu.exe
```

### GLSL / HLSL 対応

| シェーダー | ファイル |
|---|---|
| OpenGL (動作版) | `shaders/cube.vert.glsl` / `cube.frag.glsl` |
| DirectX (参照版) | `shaders/cube.vert.hlsl` / `cube.frag.hlsl` |

---

## Phase 3-1 — Blender アドオン

**実装内容:** Jet `.vol` キャッシュ読み込み / BVH 視野外カリング / ダーティレクト差分更新 / LRU フレームキャッシュ / LOD

**動作確認:** Blender 4.5 で動作確認済み ✅

### テスト用 .vol ファイルの生成

```bash
python generate_test_vol.py
# -> test_vol_cache/smoke_0000.vol ~ smoke_0029.vol (30フレーム) が生成される
```

### インストール手順

1. `blender-fluid-viewer/` フォルダを zip に圧縮
2. Blender → **Edit → Preferences → Add-ons → Install...**
3. zip を選択して **Install Add-on** → `Fluid Cache Viewer` にチェック ✓

### 使い方

1. **Properties パネル → Scene タブ → Fluid Cache Viewer**
2. Cache Directory に `test_vol_cache/` のパスを設定
3. Frame Start: `0` / Frame End: `29`
4. **Load Cache** ボタン → タイムライン再生

### 最適化効果

| 手法 | 効果 |
|---|---|
| ダーティレクト | 静的領域で最大 **95% スキップ** |
| LOD1 | 処理量 **1/8** |
| LOD2 | 処理量 **1/64** |
| LRU キャッシュ | I/O 削減（再利用） |

---

## Phase 3-2 — Houdini HDA

**実装内容:** Python SOP Cook / `.vol` パーサー / ダーティレクト / Points・Volume プリミティブ出力

**動作確認:** Houdini 21.0.700 で動作確認済み ✅

### セットアップ

Houdini の Python Shell (`Alt+Shift+P`) で実行:

```python
import sys
sys.path.insert(0, r"C:\path\to\houdini-fluid-hda")
exec(open(r"C:\path\to\houdini-fluid-hda\shelf_tool.py").read())
```

または HDA を事前生成:

```bash
cd houdini-fluid-hda
hython create_hda.py
```

---

## Phase 3-3 — 汎用最適化ライブラリ

**実装内容:** BVH (SAH ベース) / LOD / ダーティレクト / LRU キャッシュ / VDB・Alembic 対応

**動作確認済み:** ✅ 全6テスト PASSED

### テスト実行

```bash
cd render-optimizer
pip install pytest numpy
python -m pytest tests/test_optimizer.py -v
```

### テスト結果

```
test_aabb_operations        PASSED
test_bvh_build_and_query    PASSED
test_lod_selection          PASSED
test_voxel_lod_downsample   PASSED
test_dirty_rect             PASSED
test_full_pipeline          PASSED
6 passed in 0.48s
```

### 実測値

| 項目 | 結果 |
|---|---|
| BVH 構築 (125アイテム) | Depth 7 / 0.18ms |
| ダーティレクト スキップ率 | **96.9%** |
| フルパイプライン | **0.23ms/frame** |

### 使い方

```python
from src.optimizer import RenderOptimizer, OptimizerConfig

opt = RenderOptimizer(OptimizerConfig(lod_distances=[10, 30], cache_size=8))
result = opt.optimize_frame(volumes, camera_pos, camera_dir, prev_frame)
print(result.stats)
```

---

## Phase 4 — After Effects プラグイン

**実装内容:** タイル描画 (PF_Tile) / ダーティレクト / レイマーチング / Transfer Mode (Normal/Add/Screen/Multiply)

**注意:** AE SDK のインストールが必要です（[Adobe Developer](https://developer.adobe.com) からダウンロード）

### ビルド

```bash
cd ae-fluid-effect
# CMakeLists.txt の AE_SDK_DIR を設定後
cmake -B build -G "Visual Studio 17 2022" -DAE_SDK_DIR="C:/AdobeSDK/AfterEffectsSDK"
cmake --build build --config Release
# -> ae-fluid-effect.aex が生成される
```

### インストール

```
ae-fluid-effect.aex
-> C:\Program Files\Adobe\After Effects\Support Files\Plug-ins\FluidStudy\
```

---

## ドキュメント

| ファイル | 内容 |
|---|---|
| [docs/PROJECT_DOCUMENT.md](docs/PROJECT_DOCUMENT.md) | 全フェーズ技術ドキュメント |
| [docs/PROJECT_DOCUMENT.html](docs/PROJECT_DOCUMENT.html) | HTML 版（ダークテーマ） |
| [docs/LECTURE_NOTES.md](docs/LECTURE_NOTES.md) | 講義資料 11講（パフォーマンス最適化含む） |
| [docs/LECTURE_NOTES.html](docs/LECTURE_NOTES.html) | HTML 版（カラーコード付き） |
| [mini-renderer/DOCUMENT.md](mini-renderer/DOCUMENT.md) | Phase 1 詳細技術ドキュメント |

## CI / リリース

GitHub Actions でプッシュされたタグから自動ビルド・リリースを生成します。

```bash
# タグを付けて push するだけで GitHub Releases に .zip が自動生成される
git tag v1.0
git push origin v1.0
```

ワークフロー: [.github/workflows/release.yml](.github/workflows/release.yml)

---

## QuickDraw アルゴリズム対応表

| QuickDraw | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|---|---|---|---|---|
| DDA ライン (0501) | `drawLineDDA()` | — | — | — |
| 固定小数点 DDA (0503) | `drawLineFixed()` | raymarch step | — | raymarch step |
| スラブ描画 (0507) | `fillTriangleZ()` | GPU rasterizer | — | `renderTile()` |
| 転送モード (0303) | — | blend equation | — | `applyTransfer()` |
| リージョンクリップ (0310) | `setClipRect()` | scissor test | DirtyTracker | DirtyRect |
| Pitch/Yaw/Roll (Graf3D) | `Mat4::rotX/Y/Z` | uniform mat4 | — | — |
| 透視投影 (Graf3D) | `perspectiveProject()` | vertex shader | — | raymarch |

---

## 動作環境

| 項目 | バージョン |
|---|---|
| OS | Windows 11 |
| コンパイラ | MSVC (Visual Studio 2022) |
| CMake | 3.20 以上 |
| Python | 3.12 |
| Blender | 4.5（Phase 3-1） |
| Houdini | 21.0.700（Phase 3-2） |
| After Effects | 要インストール（Phase 4） |
