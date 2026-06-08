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
4. [QuickDrawとの対応表](#quickdraw-mapping)
5. [現代GPU パイプラインとの対応](#gpu-mapping)
6. [ファイル構成](#files)
7. [ビルド手順](#build)
8. [操作方法](#controls)
9. [今後のロードマップ](#roadmap)

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

## 4. QuickDrawとの対応表 {#quickdraw-mapping}

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

## 5. 現代GPUパイプラインとの対応 {#gpu-mapping}

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

## 6. ファイル構成 {#files}

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

## 7. ビルド手順 {#build}

```powershell
# cmake 設定（初回のみ SDL2 を GitHub から取得）
cmake -B build -G "Visual Studio 17 2022"

# ビルド
cmake --build build --config Release

# 実行
.\build\Release\mini-renderer.exe
```

---

## 8. 操作方法 {#controls}

| キー | 動作 |
|---|---|
| `Space` | ワイヤーフレーム / ソリッド 切り替え |
| `ESC` | 終了 |

---

## 9. 今後のロードマップ {#roadmap}

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
