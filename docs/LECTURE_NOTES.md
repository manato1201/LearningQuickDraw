# 描画アルゴリズムと最適化 — 実践講義ノート

> **担当:** 実務家教員（ゲーム・インフラ・ツール・システム開発）
> **対象:** 大学3〜4年生、情報系専攻
> **前提知識:** C/C++ 基礎、線形代数（行列・ベクトル）、基本的なアルゴリズム

---

## 講義の方針

この講義で私が伝えたいのは「**なぜそうなっているか**」です。

GPU が何をやっているかを教科書で読むより、**CPU で1ピクセルずつ手で書いてみる**方が100倍速く理解できます。1984年の Apple が作った QuickDraw は、まさにその「手書き」を全部見せてくれる最良の教材です。

```
「GPUがやっていることの本質を理解したければ、
  まずCPUで同じことを自分で実装しろ」
                          — 実務での鉄則
```

---

## 第1講：描画の基本単位 — ピクセルとは何か

### 1.1 フレームバッファ

画面に何かを表示するということは、**メモリ上の配列に数値を書く**ことです。

```cpp
// 640x480 ピクセルのフレームバッファ
uint32_t pixels[640 * 480];

// ピクセル (x, y) に色を書く
pixels[y * 640 + x] = 0xFFRRGGBB;
```

これは QuickDraw の `GrafPort portBits`（BitMap）と全く同じ構造です。After Effects の `PF_EffectWorld` も、OpenGL のフレームバッファも、**本質は同じ「配列にピクセルを書く」**だけです。

> **【学生への問い】**
> 640×480 の ARGB8888 フレームバッファのメモリ量は何バイトか？
> → 640 × 480 × 4 = **1,228,800 バイト ≒ 1.2MB**
> 60fps なら毎秒 **72MB** を書き換えることになる。

### 1.2 なぜ最適化が必要か

```
60fps = 1フレームに 16.7ms しかない

1,228,800 ピクセル × 16.7ms = ピクセルあたり 13.6 ナノ秒

1 ナノ秒 = CPU クロック 3〜4 サイクル
→ 1ピクセルに 40〜50 サイクルしか使えない
```

これが「描画最適化」が重要な理由の根本です。

---

## 第2講：直線を描く — DDA アルゴリズム

### 2.1 問題の定式化

「点 (x0, y0) から (x1, y1) までの直線を描け」

最も素朴な方法：

```cpp
// NG: 遅い、穴が開く
for (float t = 0; t <= 1.0f; t += 0.001f) {
    int x = x0 + (x1 - x0) * t;
    int y = y0 + (y1 - y0) * t;
    setPixel(x, y, color);
}
```

問題：`t` のステップを何にすればよいか分からない。小さすぎると遅く、大きすぎると穴が開く。

### 2.2 DDA の考え方

**DDA (Digital Differential Analyzer)**: 「長い方の軸を主軸にして1ずつ進む」

```
水平方向が長い場合:
  x を 1 ずつ増やす
  y は slope = dy/dx を足していく
  → 必ずピクセルが埋まる
  → ステップ数 = |dx| 回（最小限）
```

```cpp
// QuickDraw 0501DDALine.p から
float slope = (float)(y1 - y0) / (float)(x1 - x0);
float y     = y0;
for (int x = x0; x <= x1; x++) {
    setPixel(x, round(y), color);
    y += slope;  // 毎ステップ slope を加算するだけ
}
```

**ポイント**: 除算は初期化時に1回だけ。あとは加算のみ。

> **【演習】**
> 点 (0, 0) → (5, 3) を DDA で描くとき、各 x での y の値を計算せよ。
> slope = 3/5 = 0.6
> x=0: y=0.0→0, x=1: y=0.6→1, x=2: y=1.2→1, x=3: y=1.8→2, x=4: y=2.4→2, x=5: y=3.0→3

### 2.3 固定小数点最適化

DDA のボトルネック：`float` の加算と `round()`。

**1984年の QuickDraw の解決策**: 浮動小数点を整数に変換する「固定小数点演算」。

```
16.16 固定小数点:
  32bit 整数の上位 16bit = 整数部
  32bit 整数の下位 16bit = 小数部

  3.5 → 3 * 65536 + 32768 = 229376 = 0x00038000

  加算: 通常の整数加算と完全に同じ
  整数部取り出し: >> 16 (右シフト1回)
  round(): >> 16 で代替可能
```

```cpp
// 高速版 (QuickDraw 0503DDAFixedLineSpeed.p)
const int SHIFT = 16;
int slope = (dv << SHIFT) / dh;  // 除算1回のみ
int fy    = y0 << SHIFT;
for (int x = x0; x <= x1; x++) {
    setPixel(x, fy >> SHIFT, color);  // シフト1回 = round() 代替
    fy += slope;                        // 整数加算のみ (最速)
}
```

> **QuickDraw の Arith64.asm (0519)**: Motorola 68000 は 32bit CPU なので  
> 64bit 演算を `ADDX.L`（キャリー付き加算）命令で自前実装していた。  
> これが「ハードウェアの限界を知り、それを回避する」実務的思考の原点。

### 2.4 速度比較の実践

QuickDraw 0502DDALineSpeed.p は **74本の放射状の線**を float 版と固定小数点版で描いて `TickCount` で時間計測する。同じことを現代でやると：

```cpp
// Phase 1: timer.h を使った比較
Timer t;
t.start();
for (int i = 0; i < 74; i++) drawLineDDA(...);
double float_ms = t.elapsedMs();

t.start();
for (int i = 0; i < 74; i++) drawLineFixed(...);
double fixed_ms = t.elapsedMs();

printf("float: %.2fms  fixed: %.2fms  speedup: %.1fx\n",
       float_ms, fixed_ms, float_ms/fixed_ms);
```

---

## 第3講：三角形を塗る — スラブラスタライズ

### 3.1 なぜ三角形か

現代の GPU は **全てのジオメトリを三角形に分解**して描画します。三角形が基本単位である理由：
1. 3点は必ず平面上にある（4点以上だと平面が保証されない）
2. 凸図形なので塗りつぶしアルゴリズムが単純
3. GPU の並列処理に最適

### 3.2 スラブ（Slab）描画

**スラブ = 水平な帯1本**。三角形を縦方向にスキャンして、水平帯を積み重ねる。

```
      * top(x0,y0)
     / \
    /   \  ← スラブ1本
   *-----+
   |     |  ← スラブ1本
   |     |
   *-----* bottom(x2,y2)
   mid(x1,y1)
```

アルゴリズム：

```
1. 頂点を Y 順にソート: top / mid / bottom
2. 長辺 (top→bottom) と短辺 (top→mid, mid→bottom) を特定
3. y = top から y = bottom まで1ずつ進む:
     xa = 長辺上の X 座標
     xb = 短辺上の X 座標
     for x = xa to xb: setPixel(x, y, color)  ← スラブ1本分
```

> **GPU との対応**: GPU のラスタライザも本質的にスラブ（スキャンライン変換）を行う。
> Phase 2 で同じキューブを描くとき GPU はこの処理を **秒間10億回以上**並列実行する。

### 3.3 Z バッファ（深度テスト）

複数の三角形が重なるとき、手前のものだけを描く。

```cpp
// 深度バッファ: ピクセルと同解像度の float 配列
float depth[640 * 480];  // 全て FLT_MAX で初期化

// ピクセル描画時の深度テスト
bool testAndSetDepth(int x, int y, float z) {
    if (z < depth[y * 640 + x]) {
        depth[y * 640 + x] = z;
        return true;   // 手前 -> 描画許可
    }
    return false;      // 奥  -> スキップ
}
```

スキャンライン内でZ値を補間：

```
左端の Z = za,  右端の Z = zb
ピクセル x での Z = za + (zb - za) * (x - xa) / (xb - xa)
```

---

## 第4講：3D 空間 — 変換行列と透視投影

### 4.1 回転行列

3D の回転は 4×4 行列で表現します。

```
X 軸回転 (Pitch):       Y 軸回転 (Yaw):       Z 軸回転 (Roll):
| 1   0    0  |         | c  0  s |            | c -s  0 |
| 0  cos -sin |         | 0  1  0 |            | s  c  0 |
| 0  sin  cos |         |-s  0  c |            | 0  0  1 |
```

これは QuickDraw の Graf3D が `Pitch()`, `Yaw()`, `Roll()` として提供していたものと全く同じです。

**合成**: 複数の回転を組み合わせるには行列の積を使う。

```cpp
Mat4 rot = Mat4::rotationX(pitch)
         * Mat4::rotationY(yaw)
         * Mat4::rotationZ(roll);
Vec3 v_transformed = rot.transform(v_original);
```

> **【注意点】** 行列の掛け算は順序に依存する (非可換)。  
> `rotX * rotY ≠ rotY * rotX`  
> これが「ジンバルロック」などの問題の根源。

### 4.2 透視投影

遠くのものが小さく見える投影。

```
          カメラ (z = 0)
               |
               | z = v.z + 4.0 (カメラから4単位前)
               |
  (v.x, v.y, v.z)  →  スクリーン座標 (sx, sy)

焦点距離: f = 1 / tan(fov/2)

sx = (v.x * f) / z       // 遠いほど小さく見える
sy = (v.y * f) / z

スクリーン変換:
sx_pixel = sx * W/2 + W/2
sy_pixel = -sy * H/2 + H/2  (Y 軸反転)
```

> **【演習】** fov = 60°, z = 4.0 のとき、f はいくつか？
> f = 1 / tan(30°) = 1 / 0.577 ≈ 1.73

---

## 第5講：クリッピングと領域最適化

### 5.1 クリッピングの本質

「見えない部分は描かない」— これが最大の最適化です。

```cpp
// QuickDraw Ch3: SetClip / putPixel の実装
void putPixel(int x, int y, uint32_t color) {
    if (x < clip_x || x >= clip_x + clip_w) return;  // 範囲外 -> スキップ
    if (y < clip_y || y >= clip_y + clip_h) return;
    pixels[y * width + x] = color;
}
```

現代の GPU では **シザーテスト** として同じことをハードウェアで行います。

### 5.2 ダーティレクト（Dirty Rect）

「変化した領域だけ再描画する」— 動画・ゲーム・UIで最も効果的な最適化。

```
フレームn:    □□□□□
フレームn+1:  □□■□□  ← 1箇所だけ変化

ダーティレクト なし: フレーム全体を再描画 (100%)
ダーティレクト あり: ■の部分だけ再描画 (5%)

→ 95% の計算を省略できる
```

実装：

```python
# 前フレームとの差分を計算
diff = np.abs(current - previous) > threshold
dirty_region = np.argwhere(diff)  # 変化したボクセルの座標

# 変化した部分のみ更新
for coord in dirty_region:
    update_voxel(*coord)
```

**実測値**: Phase 3-3 のテストで確認
- 1箇所だけ変化する場合: **96.9% スキップ**
- フルパイプライン: **0.23ms/frame**

### 5.3 BVH（階層バウンディングボリューム）

「視野外のグループをまとめてスキップ」する空間データ構造。

```
BVH ツリー構造:
  ルートノード（全体を包む AABB）
    ├─ 左子ノード（左半分）
    │    ├─ 葉ノード（アイテム1,2,3,4）
    │    └─ 葉ノード（アイテム5,6,7,8）
    └─ 右子ノード（右半分）
         ├─ 葉ノード（アイテム9,10,11,12）
         └─ ...

カリング:
  カメラ視錐台と交差しないノード → そのサブツリー全体をスキップ
  → O(log N) で可視アイテムを抽出
```

> **QuickDraw との対応**: Ch3 の `RegionClipping` が 2D の「見えない領域はスキップ」なら、  
> BVH は 3D の「視野外のグループはまとめてスキップ」。概念は同じ、次元が違う。

---

## 第6講：GPU パイプライン — CPU 実装との対応

### 6.1 なぜ GPU は速いか

```
CPU (Phase 1):
  コア数: 8〜16
  1フレームのピクセル処理: シングルスレッドで順番に
  640×480 = 307,200 ピクセルを 16ms で → 1ピクセル 52ns

GPU (Phase 2):
  シェーダーコア数: 数千〜数万
  1フレームのピクセル処理: 全ピクセルを並列処理
  640×480 = 307,200 ピクセルを < 1ms で
```

### 6.2 パイプラインの対応

```
Phase 1 (CPU)                  Phase 2 (GPU / GLSL)
─────────────────────────────────────────────────────
rot.transform(v)          →    Vertex Shader
                                  uniform mat4 uRotation;
                                  vec3 rotated = (uRotation * vec4(pos,1)).xyz;

perspectiveProject(v)     →    Vertex Shader
                                  float px = (rotated.x * f) / z;
                                  gl_Position = vec4(px, py, ndc_z, 1.0);

fillTriangleZ() スキャンライン →  GPU Rasterizer (自動・ハードウェア)

testAndSetDepth()         →    GPU Depth Buffer (自動・ハードウェア)

putPixel()                →    Fragment Shader
                                  fragColor = vec4(vColor, 1.0);
```

### 6.3 GLSL と HLSL の違い

```glsl
// GLSL (OpenGL / Blender / Houdini)
uniform mat4  uRotation;
layout(location=0) in vec3 aPos;
void main() {
    vec3 r = (uRotation * vec4(aPos, 1.0)).xyz;
    gl_Position = vec4(r.x, r.y, r.z/10.0, 1.0);
}
```

```hlsl
// HLSL (DirectX / Unreal Engine / After Effects)
cbuffer cb : register(b0) { float4x4 uRotation; }
float4 main(float3 aPos : POSITION) : SV_Position {
    float3 r = mul(uRotation, float4(aPos, 1.0)).xyz;
    return float4(r.x, r.y, r.z/10.0, 1.0);
}
```

主な違い：
1. Uniform の宣言方法（`uniform` vs `cbuffer`）
2. 行列乗算（演算子 vs `mul()` 関数）
3. NDC の Z 範囲（`[-1,1]` vs `[0,1]`）
4. 出力のセマンティクス（`gl_Position` vs `SV_Position`）

---

## 第7講：最適化の実践 — LOD と LRU

### 7.1 LOD（Level of Detail）

「遠くのものは低解像度で描く」。

```
カメラから距離2  → LOD0 (フル解像度)   処理量: 1
カメラから距離15 → LOD1 (1/2解像度)   処理量: 1/8
カメラから距離40 → LOD2 (1/4解像度)   処理量: 1/64
カメラから距離80 → LOD3 (1/8解像度)   処理量: 1/512
```

なぜ処理量が 1/8 になるか：
- 1辺が 1/2 になると → x方向 1/2 × y方向 1/2 × z方向 1/2 = 1/8

```python
# numpy ストライドスライスで O(1) に近い速度で実現
def downsample(data, lod):
    step = 2 ** lod
    return data[::step, ::step, ::step]  # ストライド参照 -> コピーなし
```

### 7.2 LRU キャッシュ

「最近使ったフレームデータをメモリに保持して I/O を省く」。

```
LRU (Least Recently Used):
  最も古く使われていないフレームを追い出す

フレーム: 1 2 3 4 5 ... 再生 ... 3 4 5 3 4 5 ...
          ↑読込 ↑読込    ↑キャッシュヒット (I/O なし)

効果: 同フレームへの再アクセスでディスク I/O ゼロ
```

```python
from collections import OrderedDict

class LRUCache:
    def __init__(self, max_size):
        self._cache = OrderedDict()
        self._max   = max_size

    def get(self, key):
        if key in self._cache:
            self._cache.move_to_end(key)  # 最近使用済みに更新
            return self._cache[key]
        return None

    def put(self, key, value):
        if len(self._cache) >= self._max:
            self._cache.popitem(last=False)  # 最も古いものを削除
        self._cache[key] = value
```

---

## 第8講：DCC ツール統合 — 理論から実装へ

### 8.1 Blender アドオンの構造

```python
# bpy: Blender Python API の基本構造
import bpy

class MyOperator(bpy.types.Operator):
    bl_idname = "mytools.do_something"
    bl_label  = "Do Something"

    def execute(self, context):
        # ここに処理を書く
        return {"FINISHED"}

def register():
    bpy.utils.register_class(MyOperator)
```

フレーム変更ハンドラー（タイムライン再生に同期）：

```python
def on_frame_change(scene, depsgraph=None):
    frame = scene.frame_current
    vol   = load_and_optimize(frame)  # 最適化パイプライン呼び出し
    update_blender_mesh(vol)

bpy.app.handlers.frame_change_post.append(on_frame_change)
```

### 8.2 After Effects プラグインの構造

AE の `EffectMain` 関数は **セレクター（コマンド）** を受け取って処理を分岐する。これは QuickDraw のイベントドリブン設計と全く同じ思想：

```cpp
PF_Err EffectMain(PF_Cmd cmd, ...) {
    switch (cmd) {
        case PF_Cmd_GLOBAL_SETUP:  // 初期化（一回だけ）
        case PF_Cmd_PARAMS_SETUP:  // UI パラメーター定義
        case PF_Cmd_SMART_RENDER:  // タイル描画（毎フレーム）
        case PF_Cmd_SEQUENCE_SETUP:// シーケンス初期化
        ...
    }
}
```

**タイル描画** の仕組み：
- AE は大きなフレームを複数の Tile に分割
- 各 Tile を並列レンダリング
- = QuickDraw のスラブ（水平帯）を 2D に拡張したもの

---

## 第9講：アルゴリズムの選び方 — 実務の視点

### 9.1 計測なしに最適化するな

```
「推測で最適化するな。計測してから最適化せよ」
                          — 実務での鉄則 (Donald Knuth)
```

QuickDraw 0502 は **まず計測コード（TickCount）を書いてから** 最適化している。これは現代でも変わらない。

```python
import time

t0 = time.perf_counter()
result = heavy_computation()
print(f"Time: {(time.perf_counter()-t0)*1000:.2f}ms")
```

### 9.2 最適化の優先順位

```
効果大                         効果小
─────────────────────────────────
描かない (カリング・クリップ)
    ↓
まとめて描かない (LOD)
    ↓
変化した部分だけ描く (ダーティレクト)
    ↓
計算を安く済ませる (固定小数点)
    ↓
メモリ効率 (LRU キャッシュ)
    ↓
並列化 (マルチスレッド・GPU)
```

> **実務でよくあるミス**: アルゴリズムを変えずに「並列化」だけしようとする。  
> まず「描かない」「まとめて描かない」を実装してから並列化を考える。

### 9.3 QuickDraw から現代まで続く原則

| 1984年の QuickDraw | 2024年の現代 |
|---|---|
| `SetClip` で範囲外を棄却 | シザーテスト / フラスタムカリング |
| `Region` で複雑な形状クリップ | ステンシルバッファ |
| 固定小数点演算 | SIMD / GPU 整数演算 |
| スラブ描画 | タイルベースレンダリング（モバイル GPU） |
| `TickCount` で速度計測 | GPU タイマークエリ |
| 転送モード（patOr, patXor...） | ブレンド方程式 |
| BitMap への直接書き込み | フレームバッファオブジェクト |

---

## 第10講：まとめと次のステップ

### この講義で学んだこと

```
1. 描画の基本はピクセルを配列に書くだけ
2. DDA: 加算のみで直線を描く → 固定小数点で高速化
3. スラブ: 水平帯を積み重ねて三角形を塗る
4. Z バッファ: 深度テストで奥行きを正確に処理
5. 行列: 3D 変換を4×4行列で統一的に表現
6. クリッピング: 見えない部分は最初から計算しない
7. ダーティレクト: 変化した部分だけ更新する
8. BVH: 空間を階層化して大規模シーンをO(log N)でクエリ
9. GPU: CPU でやっていた処理を並列化した結果
10. 実務: まず計測し、効果の大きいところから最適化する
```

### 次に学ぶべきこと

```
この講義の続き（さらに深める方向）:
  ├─ Physically Based Rendering (PBR): 光の物理シミュレーション
  ├─ Compute Shader: GPU での流体シミュレーション
  ├─ Vulkan / Metal: 低レベル GPU 制御
  └─ Neural Rendering: 機械学習を使った描画

実装で活かす方向:
  ├─ OpenVDB: 業界標準ボリュームフォーマット
  ├─ USD (Universal Scene Description): Pixar のシーン記述言語
  ├─ OpenColorIO: カラーマネジメント
  └─ MaterialX: マテリアル記述言語
```

---

## 参考コード索引

| アルゴリズム | QuickDraw | Phase 1 | Phase 2 | Phase 3/4 |
|---|---|---|---|---|
| float DDA | 0501DDALine.p | `drawLineDDA()` | — | — |
| 固定小数点 DDA | 0503DDAFixedLineSpeed.p | `drawLineFixed()` | VS raymarch | VS raymarch |
| スラブ三角形 | 0507FixedRectSlabDrawLine.p | `fillTriangleZ()` | GPU rasterizer | `renderTile()` |
| 転送モード | 0303TransferModes.p | — | blend eq | `applyTransfer()` |
| クリッピング | 0310RegionClipping.p | `setClipRect()` | scissor | `DirtyRect` |
| 矩形演算 | 0403-0411 | `AABB` class | — | `AABB` class |
| 3D 回転 | Graf3D Pitch/Yaw/Roll | `Mat4::rotX/Y/Z` | `uniform mat4` | — |
| 透視投影 | Graf3D ViewAngle | `perspectiveProject()` | VS | `raymarch` |
| ピクセルバッファ | GrafPort portBits | `Framebuffer` | GL texture | `PF_EffectWorld` |

---

## 演習問題

### 基礎

1. 640×480、60fps、RGBA8 のフレームバッファを毎フレーム全クリアするときの帯域幅を計算せよ。
2. slope = 0.75 の直線を x=0 から x=7 まで DDA で描くとき、各ピクセルの y 座標を求めよ。
3. 固定小数点 16.16 形式で値 `2.75` を表す 32bit 整数を求めよ。

### 応用

4. 三角形 (10,0), (0,20), (20,20) をスラブアルゴリズムで塗りつぶすとき、y=10 でのスキャンライン範囲 (xa, xb) を計算せよ。
5. カメラが (0,0,10) にあり、fov=60°, aspect=1.333 のとき、点 (1, 1, 4) のスクリーン座標を計算せよ（画面サイズ 640×480）。
6. 10万個のアイテムを BVH（深さ17）で管理するとき、最悪ケースのクエリコストは何回の AABB テストか？

### 実装

7. Phase 1 の `fillTriangleZ()` に面の法線ベクトルを使ったシェーディング（Lambert shading）を追加せよ。
8. Phase 3-3 の `BVHTree` に「球とのクエリ（球内に入るアイテムを返す）」を実装せよ。
9. Phase 3-1 の Blender アドオンに、LOD レベルをビューポートのズームレベルに自動連動させる機能を追加せよ。

---

## 演習解答（抜粋）

**問1**: 640 × 480 × 4 bytes × 60fps = 73,728,000 bytes/s ≈ **70.3 MB/s**

**問3**: 2.75 = 2 + 0.75 = 2 * 65536 + 49152 = **0x0002C000** (180224)

**問5**:
```
z = 4.0 + 4.0 = 8.0 (カメラオフセット4を足す)
f = 1/tan(30°) = 1.732

px = (1.0 * 1.732) / 8.0 = 0.2165
py = (1.0 * 1.732) / 8.0 = 0.2165

sx = 0.2165 / 1.333 * 320 + 320 = 371.9 ≈ 372
sy = -0.2165 * 240 + 240 = 187.9 ≈ 188
```
