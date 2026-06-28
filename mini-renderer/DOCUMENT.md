# Mini Software Renderer - Technical Document

QuickDraw (Apple Macintosh 1984) の描画アルゴリズムを現代C++で再実装したソフトウェアレンダラー。  
流体エフェクト・映像描画最適化の基礎研究として構築。

---

## 目次

1. [プロジェクト概要](#overview)
2. [アーキテクチャ](#architecture)
3. [アルゴリズム詳解](#algorithms)
   - [DDA ライン描画](#dda)
   - [固定小数点最適化](#fixed-point)
   - [スラブ三角形ラスタライズ](#slab)
   - [Zバッファ（深度バッファ）](#zbuffer)
   - [透視投影](#perspective)
   - [3D回転行列](#rotation)
   - [リージョンクリッピング](#clipping)
4. [パフォーマンス最適化](#optimization)
5. [QuickDrawとの対応表](#quickdraw-mapping)
6. [現代GPU パイプラインとの対応](#gpu-mapping)
7. [ファイル構成](#files)
8. [ビルド手順](#build)
9. [操作方法](#controls)
10. [今後のロードマップ](#roadmap)

---

## 1. プロジェクト概要 {#overview}

| 項目 | 内容 |
|---|---|
| 言語 | C++17 |
| 描画 | CPU ソフトウェアレンダリング（GPUなし） |
| ウィンドウ | SDL2 |
| 参考文献 | The Art of QuickDraw (Apple, 1984) |
| 目的 | 描画パイプラインの基礎理解、流体エフェクト最適化の土台 |

---

## 2. アーキテクチャ {#architecture}

```
+------------------+
|    main.cpp      |  メインループ / 3D変換 / 投影
+------------------+
        |
        v
+------------------+
|   Renderer       |  2D描画プリミティブ (DDA / スラブ / クリッピング)
+------------------+
        |
        v
+------------------+
|   Framebuffer    |  ピクセルバッファ + 深度バッファ
+------------------+
        |
        v
+------------------+
|   SDL2 Texture   |  画面転送
+------------------+
```

### データフロー

```
3D頂点座標 (Vec3)
    |
    | [回転行列 Mat4::transform()]
    v
ビュー空間座標 (Vec3)
    |
    | [透視投影 perspectiveProject()]
    v
スクリーン座標 + 深度 (ProjVert)
    |
    | [fillTriangleZ() / drawLineFixed()]
    v
フレームバッファ (Framebuffer)
    |
    | [SDL_UpdateTexture()]
    v
画面
```

---

## 3. アルゴリズム詳解 {#algorithms}

### 3-1. DDA ライン描画 {#dda}

**参照 QuickDraw:** `0501DDALine.p`

DDA (Digital Differential Analyzer) は直線を描画する最もシンプルなアルゴリズム。

#### 原理

```
傾き slope = dy / dx

水平方向が主軸の場合:
  x を 1 ずつ増やし、y = y + slope で更新
  → y を round() して整数ピクセルに変換
```

#### コード（浮動小数点版）

```cpp
float slope = (float)dv / (float)dh;
float y     = (float)y0;
for (int x = x0; x <= x1; x++) {
    putPixel(x, (int)std::round(y), color);
    y += slope;
}
```

#### 計算量

- O(max(|dx|, |dy|)) — 描画するピクセル数に比例
- ボトルネック: `float` 除算 + `round()`

---

### 3-2. 固定小数点最適化 {#fixed-point}

**参照 QuickDraw:** `0503DDAFixedLineSpeed.p`, `0519Arith64.asm`

float演算を整数演算に置き換えて高速化する。

#### 16.16 固定小数点

```
32bitの整数を上位16bit（整数部）と下位16bit（小数部）に分割

例: 3.5 = 3 * 65536 + 32768 = 229376 = 0x00038000

変換:
  float -> fixed : value << 16
  fixed -> int   : value >> 16  (round()の代替)
  加算           : fixed_a + fixed_b (通常の整数加算と同じ)
```

#### コード（固定小数点版）

```cpp
const int SHIFT = 16;
int slope = (dv << SHIFT) / dh;  // 除算は1回だけ
int fy    = y0 << SHIFT;

for (int x = x0; x <= x1; x++) {
    putPixel(x, fy >> SHIFT, color);  // >>16 = round()の代替
    fy += slope;                       // 整数加算のみ
}
```

#### 速度比較（QuickDraw 0502 DDALineSpeed.p より）

```
float DDA    : round() + float加算 = 遅い
fixed-point  : bitshift + int加算  = 速い
QuickDraw標準: アセンブリ最適化    = さらに速い
```

---

### 3-3. スラブ三角形ラスタライズ {#slab}

**参照 QuickDraw:** `0507FixedRectSlabDrawLine.p`

三角形を水平な「スラブ（帯）」に分解して塗りつぶす。

#### 原理

```
頂点をY順にソート: top(y0) / mid(y1) / bottom(y2)

                 * (x0, y0)
                /|
               / |  長辺 (y0 -> y2)
              /  |
   短辺上     /   |
  (y0->y1) * ----+---- 各スキャンラインで横塗り
            |    |
   短辺下   |    |  長辺続き
  (y1->y2) |    |
            *---* (x2, y2)
       (x1, y1)
```

#### 処理手順

```
1. 頂点をY順にソート
2. 長辺 (v0 -> v2) と短辺 (v0->v1, v1->v2) に分割
3. 各スキャンライン y について:
   xa = 長辺上のX座標 = x0 + (x2-x0) * (y-y0) / totalH
   xb = 短辺上のX座標
4. xa <= xb の範囲を横塗り (スラブ1本分)
```

#### コード

```cpp
int totalH = y2 - y0;
for (int y = y0; y <= y2; y++) {
    float t  = (float)(y - y0) / totalH;
    int   xa = x0 + (int)((x2 - x0) * t);  // 長辺
    int   xb = /* 短辺 */;

    for (int x = xa; x <= xb; x++) {
        putPixel(x, y, color);
    }
}
```

---

### 3-4. Zバッファ（深度バッファ） {#zbuffer}

#### 目的

複数の三角形が重なるとき、手前のもの（Z値が小さい）だけを描画する。

#### データ構造

```
Framebuffer
  m_pixels[] : ピクセル色 (ARGB8888)
  m_depth[]  : 深度値 (float)  <- 今回追加
               初期値: FLT_MAX（無限遠）
```

#### 深度テストアルゴリズム

```
for each pixel (x, y) in triangle:
    z = 補間された深度値
    
    if z < depth_buffer[x][y]:   // 既存より手前？
        draw pixel                // -> 描画
        depth_buffer[x][y] = z    // -> バッファ更新
    else:
        discard                   // -> 捨てる（奥にある）
```

#### スキャンライン内のZ補間

```
各スキャンライン端のZを辺に沿って線形補間:
  za = Z of left edge intersection
  zb = Z of right edge intersection

各ピクセルのZ:
  frac = (x - xa) / (xb - xa)
  z    = za + (zb - za) * frac
```

#### 毎フレームのリセット

```cpp
fb.clear(0xFF111111);   // 色バッファをクリア
fb.clearDepth();         // 深度バッファを FLT_MAX にリセット
```

---

### 3-5. 透視投影 {#perspective}

**参照 QuickDraw:** `Graf3D ViewAngle / LookAt`

#### 原理

```
3D座標 (x, y, z) -> スクリーン座標 (sx, sy)

焦点距離: f = 1 / tan(fov / 2)

px = (x * f) / z
py = (y * f) / z

スクリーン座標:
  sx = px * W/2 + W/2
  sy = -py * H/2 + H/2  (Y軸反転)
```

#### 深度値

```
depth = z (view-space Z)
  z が小さい = カメラに近い = 手前
  z が大きい = カメラから遠い = 奥
```

---

### 3-6. 3D回転行列（Pitch / Yaw / Roll） {#rotation}

**参照 QuickDraw:** `Graf3D Pitch() / Yaw() / Roll()`, `0601RotatingCube.p`

#### 回転行列

```
X軸回転 (Pitch):        Y軸回転 (Yaw):         Z軸回転 (Roll):
| 1   0    0  |         | c  0  s |             | c -s  0 |
| 0  cos -sin |         | 0  1  0 |             | s  c  0 |
| 0  sin  cos |         |-s  0  c |             | 0  0  1 |
```

#### 合成

```cpp
Mat4 rot = Mat4::rotationX(pitch)
         * Mat4::rotationY(yaw)
         * Mat4::rotationZ(roll);

Vec3 transformed = rot.transform(vertex);
```

---

### 3-7. リージョンクリッピング {#clipping}

**参照 QuickDraw:** `0310RegionClipping.p`, `SetClip`

矩形の範囲外ピクセルを棄却する。

```cpp
void putPixel(int x, int y, uint32_t color) {
    if (m_clipEnabled) {
        if (x < m_clipX || x >= m_clipX + m_clipW) return;
        if (y < m_clipY || y >= m_clipY + m_clipH) return;
    }
    m_fb.setPixel(x, y, color);
}
```

---

## 4. パフォーマンス最適化 {#optimization}

レンダリングループのプロファイリングで特定したボトルネックと対策をまとめる。

---

### 4-1. バックフェイスカリング

**問題:** 12枚の三角形を全て処理していたが、常に最大6枚（裏面）は不可視。

**解決策:** スクリーン空間の符号付き面積でカリング。

```cpp
// main.cpp — 投影後の頂点を使い、面積の符号で表裏を判定
float ax = projected[b].sx - projected[a].sx;
float ay = projected[b].sy - projected[a].sy;
float bx = projected[c].sx - projected[a].sx;
float by = projected[c].sy - projected[a].sy;
if (ax * by - ay * bx >= 0.0f) continue;  // 裏面: スキップ
```

**効果:** 毎フレーム平均 **約50%** の三角形描画をスキップ。

---

### 4-2. `fillTriangleZ` 内ループ — 増分Z補間

**問題:** スキャンライン内で毎ピクセル `(x - xa) / span` の浮動小数点除算が発生。

```cpp
// Before: ピクセルごとに除算
float frac = (span > 0) ? (float)(x - xa) / (float)span : 0.0f;
float z    = za + (zb - za) * frac;
```

**解決策:** スキャンライン開始前に1回だけ `zStep` を計算し、加算で進む。

```cpp
// After: 除算1回 → 加算のみ (固定小数点DDAと同じ原理)
float zStep = (span > 0) ? (zb - za) / (float)span : 0.0f;
float z     = za + zStep * (float)(xStart - xa);  // クランプ開始点に合わせる
for (int x = xStart; x <= xEnd; x++, z += zStep) { ... }
```

---

### 4-3. `fillTriangleZ` クリップ処理の最適化

**問題:** 内ループ内でピクセルごとにクリップ境界チェックを実施。

**解決策:** スキャンライン単位で事前計算し、有効なX範囲だけをループ。

```cpp
// ループ前に境界を確定
int yLo = m_clipEnabled ? std::max(m_clipY, 0)             : 0;
int yHi = m_clipEnabled ? std::min(m_clipY+m_clipH-1, fbH-1) : fbH-1;
int xLo = m_clipEnabled ? std::max(m_clipX, 0)             : 0;
int xHi = m_clipEnabled ? std::min(m_clipX+m_clipW-1, fbW-1) : fbW-1;

for (int y = y0; y <= y2; y++) {
    if (y < yLo || y > yHi) continue;          // Yスキップ
    int xStart = std::max(xa, xLo);
    int xEnd   = std::min(xb, xHi);
    if (xStart > xEnd) continue;               // 全スキップ
    // 以降は unchecked 書き込み可能
}
```

---

### 4-4. `Framebuffer` — 境界チェックなし高速パス

**問題:** `testAndSetDepth()` / `setPixel()` は毎回 `x < 0 || x >= width` を判定。  
`fillTriangleZ` 内ループは上記クランプ後に呼ぶため判定が二重になっていた。

**解決策:** `Framebuffer` に unchecked インラインメソッドを追加。

```cpp
// framebuffer.h (inline)
void setPixelUnchecked(int x, int y, uint32_t color) {
    m_pixels[y * m_width + x] = color;
}
bool testAndSetDepthUnchecked(int x, int y, float depth) {
    float& slot = m_depth[y * m_width + x];
    if (depth < slot) { slot = depth; return true; }
    return false;
}
```

境界が保証された fillTriangleZ の内ループでのみ使用し、putPixel 経由のパスは引き続き安全な checked 版を使う。

---

### 4-5. `fillTriangle` — 固定小数点スロープ

**問題:** スキャンラインごとに `(x2-x0)*(y-y0)/totalH` の整数除算が発生（精度も低い）。

**解決策:** 16.16 固定小数点スロープを事前計算し、加算で各辺を進む。

```cpp
// Before: per-scanline division
int xa = x0 + (x2 - x0) * (y - y0) / totalH;

// After: pre-computed slope, accumulate
const int SHIFT = 16;
int dxa   = ((x2 - x0) << SHIFT) / totalH;  // 除算は初期化時1回
int xa_fp = x0 << SHIFT;
// ループ内: xa_fp += dxa; int xa = xa_fp >> SHIFT;
```

fillTriangle と drawLineFixed が同じ 16.16 固定小数点原理を共有する。

---

### 4-6. `perspectiveProjectFast` — FOV係数のプリコンピュート

**問題:** `perspectiveProject()` が毎頂点呼び出しで `1/tan(fovRad/2)` を計算（フレームごと8回）。

**解決策:** フレームループ外で定数として一度だけ計算し `perspectiveProjectFast` に渡す。

```cpp
// main.cpp — ループ外（FOVは変化しないため）
const float PROJ_F = 1.0f / std::tan(60.0f * (3.14159265f / 180.0f) * 0.5f);

// ループ内
projected[i] = perspectiveProjectFast(viewVerts[i], W, H, PROJ_F);
```

---

### 最適化効果まとめ

| 対象 | 変更前 | 変更後 | 分類 |
|---|---|---|---|
| バックフェイスカリング | 12三角形/frame | ~6三角形/frame | アルゴリズム |
| `fillTriangleZ` Z補間 | 除算×ピクセル数 | 除算1回+加算 | 数値計算 |
| クリップ境界チェック | ピクセルごと | スキャンラインごと | 分岐削減 |
| `Framebuffer` 境界チェック | 毎呼び出し | ゼロ（unchecked） | 不要チェック除去 |
| `fillTriangle` スロープ | スキャンラインごと除算 | 初期化時1回 | 固定小数点 |
| FOV係数 `tan()` | 8回/frame | 0回/frame（定数） | ホイスト |

---

## 5. QuickDrawとの対応表 {#quickdraw-mapping}

| 本実装 | QuickDrawファイル | 内容 |
|---|---|---|
| `drawLineDDA()` | `0501DDALine.p` | 浮動小数点DDA |
| `drawLineFixed()` | `0503DDAFixedLineSpeed.p` | 固定小数点DDA |
| `fillTriangle()` | `0507FixedRectSlabDrawLine.p` | スラブ三角形 |
| `setClipRect()` | `0310RegionClipping.p` | クリッピング |
| `Mat4::rotationX/Y/Z()` | `Graf3D Pitch/Yaw/Roll` | 3D回転 |
| `perspectiveProject()` | `Graf3D ViewAngle/LookAt` | 透視投影 |
| `Framebuffer` | `GrafPort portBits` | ピクセルバッファ |
| `fillTriangleZ()` | (現代の拡張) | Z バッファ付き描画 |

---

## 6. 現代GPUパイプラインとの対応 {#gpu-mapping}

```
本実装 (CPU)                  GPU パイプライン
--------------------------------------------------
rot.transform(v)          ->  Vertex Shader
perspectiveProject(v)     ->  Rasterizer (projection)
fillTriangleZ() slab      ->  Rasterizer (scanline)
Z補間 + testAndSetDepth() ->  Depth Test / Depth Buffer
putPixel() + blend        ->  Fragment Shader + Blend
SDL_UpdateTexture()       ->  Frame Buffer Present
```

---

## 7. ファイル構成 {#files}

```
mini-renderer/
├── CMakeLists.txt        SDL2 自動取得 + ビルド設定
└── src/
    ├── math3d.h          Vec3 / Vec2 / ProjVert / Mat4 / perspectiveProject
    ├── framebuffer.h/cpp ピクセルバッファ + 深度バッファ
    ├── renderer.h/cpp    DDA / スラブ / Zバッファ / クリッピング
    └── main.cpp          メインループ / キューブ定義 / 3D変換
```

---

## 8. ビルド手順 {#build}

```powershell
# cmake 設定（初回のみ SDL2 を GitHub から取得）
cmake -B build -G "Visual Studio 17 2022"

# ビルド
cmake --build build --config Release

# 実行
.\build\Release\mini-renderer.exe
```

---

## 9. 操作方法 {#controls}

| キー | 動作 |
|---|---|
| `Space` | ワイヤーフレーム / ソリッド 切り替え |
| `ESC` | 終了 |

---

## 10. 今後のロードマップ {#roadmap}

### Phase 2: GPU化
- GLSL シェーダーで本実装と同等のロジックを移植
- CPU/GPU の速度差を体感

### Phase 3: Houdini / Blender アドオン
- Jet流体キャッシュ (.vol) の読み込み
- ダーティレクト最適化（変化ボクセルのみ更新）
- BVH による視錐台カリング

### Phase 4: After Effects プラグイン
- AEGP で本レンダラーをエフェクトとして統合
- エフェクト範囲の差分管理による高速化
