# QuickDraw Study Project - 全体ドキュメント

> Apple QuickDraw (1984) のアルゴリズムを起点に、現代の描画パイプライン・流体エフェクト最適化を実装したプロジェクトの全記録。

---

## 目次

1. [プロジェクト概要](#overview)
2. [学習ロードマップ](#roadmap)
3. [Phase 1: ミニソフトウェアレンダラー](#phase1)
4. [Phase 2: GPU 化](#phase2)
5. [Phase 3-1: Blender アドオン](#phase3-1)
6. [Phase 3-2: Houdini HDA](#phase3-2)
7. [Phase 3-3: 汎用最適化ライブラリ](#phase3-3)
8. [Phase 4: After Effects プラグイン](#phase4)
9. [全体アーキテクチャ](#architecture)
10. [QuickDraw 対応表（全 Phase）](#mapping)
11. [ツール一覧](#tools)

---

## 1. プロジェクト概要 {#overview}

| 項目 | 内容 |
|---|---|
| 起点教材 | The Art of QuickDraw (Apple, 1984) / 日本語版 PDF |
| 目的 | 描画パイプライン基礎理解 → 流体エフェクト・映像描画最適化 |
| 最終目標 | Houdini・Blender・After Effects の描画負荷軽減ツール群 |
| 言語 | C++17 / Python 3.x / GLSL / HLSL |
| 対象 DCC | Adobe After Effects / SideFX Houdini / Blender / Unreal Engine |

### プロジェクト全体の問い

```
「QuickDraw の描画アルゴリズムを理解すれば、
  現代の流体エフェクト・映像描画最適化ができるか？」

答え: YES — ただし現代固有の知識（GPU並列・シェーダー）を追加する必要がある。
```

---

## 2. 学習ロードマップ {#roadmap}

```
QuickDraw (.p Pascal サンプル)
  |
  | Ch2-3: 図形・転送モード・クリッピング
  | Ch4:   矩形演算の内部実装
  | Ch5:   DDA・固定小数点・スラブ描画
  | Ch6:   3D変換・透視投影 (Graf3D)
  |
  v
Phase 1: ミニソフトウェアレンダラー [C++ / SDL2]
  - DDA ライン描画 (float -> 固定小数点)
  - スラブ三角形ラスタライズ
  - Z バッファ（深度テスト）
  - Pitch/Yaw/Roll 回転行列
  - 透視投影
  |
  v
Phase 2: GPU 化 [OpenGL + GLSL / HLSL]
  - 同アルゴリズムをシェーダーに移植
  - CPU/GPU 速度比較
  - GLSL・HLSL 並走
  |
  v
Phase 3-1: Blender アドオン [Python / bpy]     <- 流体最適化
Phase 3-2: Houdini HDA [Python SOP]            <- 流体最適化
Phase 3-3: 汎用最適化ライブラリ [Python]      <- VDB/Alembic
  |
  v
Phase 4: After Effects プラグイン [C++ / AE SDK]
  - タイル描画
  - ダーティレクト
  - 転送モード最適化
```

---

## 3. Phase 1: ミニソフトウェアレンダラー {#phase1}

**場所:** `mini-renderer/`

### 実装アルゴリズム

#### DDA ライン描画（浮動小数点版）
```cpp
// QuickDraw 0501DDALine.p の直訳
float slope = (float)dv / (float)dh;
float fy    = (float)y0;
for (int x = x0; x <= x1; x++) {
    putPixel(x, (int)std::round(fy), color);
    fy += slope;
}
```

#### 固定小数点 DDA（最適化版）
```cpp
// QuickDraw 0503DDAFixedLineSpeed.p の直訳
// float -> 16.16 固定小数点で高速化
const int SHIFT = 16;
int slope = (dv << SHIFT) / dh;   // 除算は初期化時1回のみ
int fy    = y0 << SHIFT;
for (int x = x0; x <= x1; x++) {
    putPixel(x, fy >> SHIFT, color);  // ビットシフト = round() 代替
    fy += slope;                       // 整数加算のみ
}
```

#### スラブ三角形ラスタライズ
```
頂点を Y 順にソート → 長辺と短辺を特定
各スキャンライン y で:
  xa = 長辺上の X 交差点
  xb = 短辺上の X 交差点
  for x = xa to xb: putPixel(x, y, color)
```

#### Z バッファ（深度テスト）
```cpp
bool testAndSetDepth(int x, int y, float depth) {
    float& slot = m_depth[y * m_width + x];
    if (depth < slot) { slot = depth; return true; }
    return false;   // 奥にある -> スキップ
}
```

### パフォーマンス最適化（実施済み）

ホットスポット分析から特定した6つの改善をすべて適用済み。

| 最適化 | 変更前 | 変更後 |
|---|---|---|
| バックフェイスカリング | 12三角形/frame | ~6三角形/frame（符号付き面積判定） |
| `fillTriangleZ` Z補間 | 除算×ピクセル数 | `zStep` 増分加算（除算1回/span） |
| クリップ境界テスト | 内ループで毎ピクセル | スキャンライン事前クランプ |
| `Framebuffer` 境界チェック | 毎呼び出し | unchecked 高速パス（内ループ限定） |
| `fillTriangle` スロープ | スキャンラインごと整数除算 | 16.16 固定小数点増分 |
| FOV係数 `tan()` | 8回/frame | フレームループ外で1回のみ |

追加 API:
- `Framebuffer::setPixelUnchecked()` / `testAndSetDepthUnchecked()` — 境界保証済みの高速パス
- `Vec3::cross()` — 外積（バックフェイスカリングで使用）
- `perspectiveProjectFast(v, w, h, f)` — 事前計算 `f` を受け取る高速版

### ファイル構成
```
mini-renderer/
├── CMakeLists.txt      SDL2 自動取得
└── src/
    ├── math3d.h        Vec3(+cross) / Mat4 / perspectiveProject / perspectiveProjectFast
    ├── framebuffer.h/cpp  色バッファ + 深度バッファ (unchecked 高速パス付き)
    ├── renderer.h/cpp  DDA / スラブ / Z バッファ / クリッピング（最適化済み）
    └── main.cpp        回転キューブ メインループ（バックフェイスカリング + FOVプリコンピュート）
```

---

## 4. Phase 2: GPU 化 {#phase2}

**場所:** `mini-renderer-gpu/`

### Phase 1 → Phase 2 対応表

| Phase 1 (CPU) | Phase 2 (GPU) | 自動化 |
|---|---|---|
| `rot.transform(v)` | Vertex Shader | — |
| `perspectiveProject()` | Vertex Shader | — |
| `fillTriangleZ()` slab | GPU Rasterizer | **完全自動** |
| `testAndSetDepth()` | GPU Depth Buffer | **完全自動** |
| `putPixel()` | Fragment Shader | — |

### GLSL / HLSL 比較

| 項目 | GLSL | HLSL |
|---|---|---|
| Uniform | `uniform mat4 uRotation` | `cbuffer : register(b0)` |
| 頂点入力 | `layout(location=0) in vec3` | `float3 aPos : POSITION` |
| 出力 | `gl_Position` / `out vec4` | `SV_Position` / `SV_Target` |
| NDC Z 範囲 | `[-1, 1]` | `[0, 1]` |
| 行列乗算 | `*` 演算子 | `mul()` 関数 |

### パフォーマンス最適化（実施済み）

| 最適化 | 変更前 | 変更後 |
|---|---|---|
| 回転行列生成 | `matRotX` + `matRotY` + `matRotZ` + `matMul` × 2 | `matRotXYZ` 1関数（解析展開） |
| 演算量 | 6 trig + 128 mul + 96 add | 6 trig + 18 mul + 6 add |

`matRotXYZ` は Rz·Ry·Rx を解析的に展開し、列優先 OpenGL 形式で直接書き出す：

```cpp
static void matRotXYZ(float pitch, float yaw, float roll, float m[16]) {
    float cx = cosf(pitch), sx = sinf(pitch);
    float cy = cosf(yaw),   sy = sinf(yaw);
    float cz = cosf(roll),  sz = sinf(roll);
    m[0] = cy*cz;  m[1] = cy*sz;  m[2] = -sy;    m[3] = 0.0f;
    m[4] = sx*sy*cz - cx*sz;  ...
}
```

### ファイル構成
```
mini-renderer-gpu/
├── shaders/
│   ├── cube.vert.glsl / cube.frag.glsl   OpenGL 動作版
│   └── cube.vert.hlsl / cube.frag.hlsl   DirectX 参照版
└── src/
    ├── gl_renderer.h/cpp  OpenGL 描画クラス + GPU タイマー
    ├── timer.h/cpp        CPU/GPU 時間計測
    └── main.cpp           メインループ (matRotXYZ 最適化済み)
```

---

## 5. Phase 3-1: Blender アドオン {#phase3-1}

**場所:** `blender-fluid-viewer/`

### 最適化の3層構造

```
フレームデータ
  |
  | [LRUFrameCache]  読み込み済みフレームを再利用 (I/O 削減)
  v
VolFrame
  |
  | [BVHCuller]  視野外ボクセルブロックをスキップ (演算削減)
  v
可視ボクセル
  |
  | [DirtyTracker]  変化ボクセルのみ更新 (更新量削減)
  v
Blender メッシュ更新
```

### 実測スキップ率
- 静的領域: 最大 **95% スキップ**
- LOD1 ダウンサンプル: 処理量 **1/8**
- LOD2 ダウンサンプル: 処理量 **1/64**

### ファイル構成
```
blender-fluid-viewer/
├── __init__.py       登録・Scene プロパティ
├── vol_reader.py     Jet .vol パーサー
├── cache_manager.py  DirtyTracker / BVHCuller / LRUFrameCache
├── operators.py      Load / Unload / Export VDB
└── panels.py         プロパティパネル UI
```

---

## 6. Phase 3-2: Houdini HDA {#phase3-2}

**場所:** `houdini-fluid-hda/`

### Blender との比較

| 項目 | Blender | Houdini |
|---|---|---|
| 言語 | Python (bpy) | Python SOP / HDK (C++) |
| 出力 | Mesh 点群 | Points / Volume プリミティブ |
| フレーム連動 | `frame_change_post` ハンドラー | SOP Cook（自動） |
| VDB 変換 | Export ボタン | Convert VDB SOP を接続 |
| 統計表示 | Scene パネル | ノードコメント |

### ファイル構成
```
houdini-fluid-hda/
├── create_hda.py      HDA 自動生成スクリプト
├── shelf_tool.py      シェルフツール
└── python/
    ├── vol_reader.py  .vol パーサー（Blender 共通）
    ├── dirty_tracker.py  ダーティレクト
    └── sop_cook.py    SOP Cook エントリポイント
```

---

## 7. Phase 3-3: 汎用最適化ライブラリ {#phase3-3}

**場所:** `render-optimizer/`

### テスト実測値

| テスト | 結果 |
|---|---|
| BVH 125 アイテム構築 | Depth 7 / 0.18ms |
| BVH クエリ | 8 アイテムヒット（正確） |
| LOD 距離2 → LOD0 / 距離49 → LOD2 | 正確 |
| LOD1: 処理量 **1/8** | 確認済み |
| LOD2: 処理量 **1/64** | 確認済み |
| ダーティレクト 2フレーム目 | **96.9% スキップ** |
| フルパイプライン | **0.23ms/frame** |

### 対応フォーマット

| フォーマット | 対応 | 備考 |
|---|---|---|
| Jet `.vol` | ✅ | ネイティブ実装 |
| OpenVDB `.vdb` | ✅ | pyopenvdb が必要 |
| Alembic `.abc` | ✅ | alembic バインディングが必要 |

---

## 8. Phase 4: After Effects プラグイン {#phase4}

**場所:** `ae-fluid-effect/`

### AE SDK と QuickDraw の対応

| AE の仕組み | QuickDraw の対応 |
|---|---|
| `PF_EffectWorld` | `GrafPort portBits` |
| `PF_Fixed` (16.16) | Ch5: 固定小数点 `0503DDAFixedLineSpeed.p` |
| `PF_Tile` | Ch5: スラブ描画 `0507FixedRectSlabDrawLine.p` |
| DirtyRect チェック | Ch3: RegionClipping `0310RegionClipping.p` |
| Transfer Mode | Ch3: TransferModes `0303TransferModes.p` |
| `.r` リソースファイル | Ch6: `0600MacTech3D.R`（**同一形式**） |

### Transfer Mode と QuickDraw 転送モードの対応

| AE モード | QuickDraw | 効果 |
|---|---|---|
| Normal | `patCopy` | そのまま上書き |
| Add | `patOr` | 加算（明るくなる） |
| Screen | `notPatBic` | さらに明るく |
| Multiply | `patBic` | 暗くなる |

---

## 9. 全体アーキテクチャ {#architecture}

```
QuickDraw アルゴリズム知識ベース
         |
         |  ピクセルレベルの実装理解
         v
[Phase 1] CPU ソフトウェアレンダラー
         |                    |
         | GPU 化              | 流体最適化
         v                    v
[Phase 2] GPU シェーダー    [Phase 3] 最適化エンジン
  GLSL + HLSL               ├── Blender アドオン
  OpenGL + DirectX          ├── Houdini HDA
                            └── 汎用ライブラリ
                                 .vol / VDB / Alembic
                                      |
                                      v
                            [Phase 4] AE プラグイン
                              タイル + ダーティレクト
                              + 転送モード最適化
```

---

## 10. QuickDraw 対応表（全 Phase） {#mapping}

| QuickDraw | ファイル | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|---|---|---|---|---|---|
| DDA ライン | 0501 | drawLineDDA() | — | — | — |
| 固定小数点 DDA | 0503 | drawLineFixed() | raymarch step | — | raymarch step |
| 速度比較 | 0502 | benchmark | GPU timer | — | — |
| スラブ三角形 | 0507 | fillTriangle() | GPU rasterizer | — | renderTile() |
| 転送モード | 0303 | — | blend eq | — | applyTransfer() |
| リージョンクリップ | 0310 | setClipRect() | scissor | DirtyTracker | DirtyRect |
| 矩形演算 | 0403-0411 | AABB ops | — | AABB class | — |
| Pitch/Yaw/Roll | Graf3D | Mat4::rotX/Y/Z | uniform mat4 | — | — |
| 透視投影 | Graf3D | perspectiveProject() | vertex shader | — | raymarch |
| PixelBuffer | GrafPort | Framebuffer | SDL texture | bpy.mesh | PF_EffectWorld |
| リソース定義 | 0600.R | — | — | — | fluid_effect.r |

---

## 11. ツール一覧 {#tools}

| ツール | 場所 | 言語 | 用途 |
|---|---|---|---|
| ミニソフトウェアレンダラー | `mini-renderer/` | C++ | Phase 1 |
| GPU レンダラー | `mini-renderer-gpu/` | C++ / GLSL | Phase 2 |
| Blender アドオン | `blender-fluid-viewer/` | Python | Phase 3-1 |
| Houdini HDA | `houdini-fluid-hda/` | Python | Phase 3-2 |
| 汎用最適化ライブラリ | `render-optimizer/` | Python | Phase 3-3 |
| AE プラグイン | `ae-fluid-effect/` | C++ | Phase 4 |

---

## 12. CI / リリース

GitHub Actions でタグ push または手動実行から自動ビルド・配布を行う。

**ワークフロー:** `.github/workflows/release.yml`

| トリガー | 動作 |
|---|---|
| `git tag v*` + push | 正式リリースを GitHub Releases に公開 |
| GitHub Actions UI で「Run workflow」 | `latest-dev` プレリリースを更新 |

**成果物:**
- `mini-renderer-windows.zip` — SDL2 静的リンク済み `.exe`
- `mini-renderer-gpu-windows.zip` — `.exe` + `shaders/` フォルダ

CMake なしで別 PC で実行したい場合は [GitHub Releases](https://github.com/manato1201/LearningQuickDraw/releases) から zip をダウンロードして展開するだけでよい。
