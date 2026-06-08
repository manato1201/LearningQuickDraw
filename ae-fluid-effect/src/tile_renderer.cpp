/*
 * tile_renderer.cpp
 *
 * QuickDraw 各章の実装がここに集約する:
 *
 *   Ch3 RegionClipping  -> computeDirty()  : 変化領域のみ再描画
 *   Ch3 TransferModes   -> applyTransfer() : 合成モード
 *   Ch5 スラブ描画      -> renderTile()    : タイル単位の処理
 *   Ch5 固定小数点      -> raymarch()      : ステップ計算の整数化
 *   Ch6 透視投影        -> raymarch()      : レイ生成
 */

#include "tile_renderer.h"
#include <cmath>
#include <algorithm>
#include <cstring>

// ================================================================
// ColorRamp
// ================================================================
PixelRGBA ColorRamp::sample(float density) const {
    float d = std::max(0.0f, std::min(1.0f, density));
    // デフォルト: 白煙
    return { d, d, d, d * 0.8f };
}

ColorRamp ColorRamp::smoke() {
    return {};  // デフォルト実装
}

ColorRamp ColorRamp::fire() {
    ColorRamp r;
    return r;
}

ColorRamp ColorRamp::water() {
    ColorRamp r;
    return r;
}

// ================================================================
// DirtyRect 計算
//
// QuickDraw Ch3: 0310RegionClipping.p の差分検出
// 前フレームとの密度差が threshold 以上のボクセルを包む最小矩形を返す。
// After Effects は矩形単位で再レンダリング要否を判断するため、
// 変化がなければ renderTile() 呼び出し自体をスキップできる。
// ================================================================
DirtyRect TileRenderer::computeDirty(
    const float* prev,
    const float* curr,
    int          count,
    float        threshold)
{
    if (!prev || !curr) {
        return { 0, 0, 0, 0, false };   // 全域が変化
    }

    DirtyRect dr = { INT_MAX, INT_MAX, INT_MIN, INT_MIN, true };
    bool any_change = false;

    // 簡略化: 1D インデックスを 2D とみなして dirty rect を求める
    // 実際は 3D ボクセルを投影した 2D 領域で計算する
    int side = (int)std::cbrt((float)count);
    if (side <= 0) return { 0, 0, 0, 0, false };

    for (int i = 0; i < count; i++) {
        if (std::abs(curr[i] - prev[i]) > threshold) {
            int z = i / (side * side);
            int y = (i / side) % side;
            int x = i % side;
            dr.x0 = std::min(dr.x0, x);
            dr.y0 = std::min(dr.y0, y);
            dr.x1 = std::max(dr.x1, x + 1);
            dr.y1 = std::max(dr.y1, y + 1);
            any_change = true;
        }
    }

    if (!any_change) {
        dr = { 0, 0, 0, 0, true };  // 変化なし -> 空の有効 dirty rect
    }
    return dr;
}

// ================================================================
// Raymarch - レイキャスティングで密度を積算
//
// QuickDraw との対応:
//   Ch5 スラブ描画 : Z 方向に「スラブを積み重ねて」密度を蓄積
//   Ch6 透視投影   : レイの方向を透視投影で決定
//   Ch5 固定小数点 : ステップサイズを固定小数点で整数化
// ================================================================
float TileRenderer::raymarch(
    float px, float py,
    const VolFrame& vol,
    int   lod,
    float density_min,
    int   num_steps)
{
    // レイ方向（直交投影で簡略化）
    float density_sum = 0.0f;

    // LOD 解像度
    int xs = LodSize(vol.header.x_res, lod);
    int ys = LodSize(vol.header.y_res, lod);
    int zs = LodSize(vol.header.z_res, lod);

    // スクリーン座標 -> ボクセルグリッド座標
    int vx = (int)(px * xs);
    int vy = (int)(py * ys);

    vx = std::max(0, std::min(xs - 1, vx));
    vy = std::max(0, std::min(ys - 1, vy));

    // Z 方向にスラブを積み重ねて密度を積算
    // QuickDraw Ch5: スラブ描画（水平帯を積み重ねる）の Z 版
    //
    // 固定小数点ステップ (Ch5: 0503DDAFixedLineSpeed.p と同じ思想):
    //   step_fixed = (zs << 16) / num_steps  ... 除算は1回だけ
    //   z_fixed    = 0
    //   z_fixed   += step_fixed  ... 整数加算のみ
    const int SHIFT = 16;
    int step_fixed = (zs << SHIFT) / num_steps;
    int z_fixed    = 0;

    for (int s = 0; s < num_steps; s++) {
        int vz = z_fixed >> SHIFT;
        z_fixed += step_fixed;

        if (vz >= zs) break;

        float d = vol.sample_lod(vx, vy, vz, lod);
        if (d > density_min) {
            density_sum += d * (1.0f / num_steps);
        }
    }

    return std::min(1.0f, density_sum);
}

// ================================================================
// applyTransfer - 転送モード合成
//
// QuickDraw Ch3: 0303TransferModes.p の直訳
//   patCopy  -> NORMAL   : src をそのまま上書き
//   patOr    -> ADD      : src + dst (明るくなる)
//   notPatBic -> SCREEN  : 1-(1-s)*(1-d) (さらに明るく)
//   patBic   -> MULTIPLY : src * dst (暗くなる)
// ================================================================
PixelRGBA TileRenderer::applyTransfer(
    const PixelRGBA& src,
    const PixelRGBA& dst,
    int mode)
{
    PixelRGBA out;
    float a = src.a;

    switch (mode) {
        case 0:  // NORMAL (patCopy)
            out.r = src.r * a + dst.r * (1.f - a);
            out.g = src.g * a + dst.g * (1.f - a);
            out.b = src.b * a + dst.b * (1.f - a);
            out.a = a + dst.a * (1.f - a);
            break;

        case 1:  // ADD (patOr)
            out.r = std::min(1.f, src.r + dst.r);
            out.g = std::min(1.f, src.g + dst.g);
            out.b = std::min(1.f, src.b + dst.b);
            out.a = std::min(1.f, src.a + dst.a);
            break;

        case 2:  // SCREEN (notPatBic)
            out.r = 1.f - (1.f - src.r) * (1.f - dst.r);
            out.g = 1.f - (1.f - src.g) * (1.f - dst.g);
            out.b = 1.f - (1.f - src.b) * (1.f - dst.b);
            out.a = 1.f - (1.f - src.a) * (1.f - dst.a);
            break;

        case 3:  // MULTIPLY (patBic)
            out.r = src.r * dst.r;
            out.g = src.g * dst.g;
            out.b = src.b * dst.b;
            out.a = src.a * dst.a;
            break;

        default:
            out = src;
            break;
    }
    return out;
}

// ================================================================
// renderTile - タイル1枚をレンダリング
//
// After Effects はフレームを Tile に分割して renderTile() を呼ぶ。
// QuickDraw のスラブ描画（水平帯を積み重ねる）と同じ分割戦略。
//
// DirtyRect チェック:
//   タイルが dirty rect と重ならない場合は即リターン。
//   QuickDraw Ch3: SetClip で範囲外を棄却するのと同じ。
// ================================================================
void TileRenderer::renderTile(
    const RenderTile&  tile,
    const VolFrame&    vol,
    const DirtyRect&   dirty,
    int                lod,
    float              density_scale,
    float              density_min,
    int                transfer_mode,
    const ColorRamp&   ramp,
    PixelRGBA*         out_pixels)
{
    int tw = tile.width();
    int th = tile.height();

    // 仮のフレームサイズ（AE では in_data->width / height を使用）
    float frame_w = (float)(vol.header.x_res);
    float frame_h = (float)(vol.header.y_res);

    for (int ty = 0; ty < th; ty++) {
        int fy = tile.y0 + ty;

        for (int tx = 0; tx < tw; tx++) {
            int fx = tile.x0 + tx;

            // ----------------------------------------------------------------
            // DirtyRect チェック
            // QuickDraw Ch3: SetClip / putPixel のクリッピングと同じ
            // dirty rect 外のピクセルは再計算不要 -> スキップ
            // ----------------------------------------------------------------
            if (dirty.valid && !dirty.contains(fx, fy)) {
                // 前フレームのピクセルをそのまま保持（変化なし）
                // out_pixels は AE が前フレームバッファを維持するので
                // ここでは何もしないだけで OK
                continue;
            }

            // スクリーン座標を [0,1] に正規化
            float px = (fx + 0.5f) / frame_w;
            float py = (fy + 0.5f) / frame_h;

            // レイキャスティングで密度を取得
            float density = raymarch(px, py, vol, lod, density_min);
            density *= density_scale;

            // 密度 -> 色変換
            PixelRGBA vol_color = ramp.sample(density);

            // 既存ピクセルを取得（転送モード合成のため）
            PixelRGBA dst = out_pixels[ty * tw + tx];

            // 転送モード合成
            // QuickDraw Ch3: patCopy / patOr / patXor / patBic
            out_pixels[ty * tw + tx] = applyTransfer(vol_color, dst, transfer_mode);
        }
    }
}
