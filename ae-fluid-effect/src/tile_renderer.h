/*
 * tile_renderer.h
 * タイルベースボリュームレンダラー
 *
 * QuickDraw との対応:
 *   Tile      <-> QuickDraw Ch5: スラブ (水平帯)
 *   DirtyRect <-> QuickDraw Ch3: RegionClipping (変化領域のみ再描画)
 *   Transfer  <-> QuickDraw Ch3: 転送モード (patCopy/patOr/patXor)
 *
 * After Effects のタイル分割:
 *   AE は大きなフレームを複数の Tile に分割して並列レンダリングする。
 *   これは QuickDraw のスラブ描画（水平帯を積み重ねる）と同じ概念。
 */

#pragma once
#include "vol_loader.h"

// ----------------------------------------------------------------
// RenderTile - レンダリング対象タイル
// QuickDraw Ch5: スラブ (横1行) を2Dに拡張したもの
// ----------------------------------------------------------------
struct RenderTile {
    int   x0, y0;      // タイル左上角
    int   x1, y1;      // タイル右下角
    int   width()  const { return x1 - x0; }
    int   height() const { return y1 - y0; }
};

// ----------------------------------------------------------------
// DirtyRect - 変化領域の追跡
// QuickDraw Ch3: RegionClipping 相当
// ----------------------------------------------------------------
struct DirtyRect {
    int   x0, y0, x1, y1;
    bool  valid;   // false = 全域が変化

    bool contains(int x, int y) const {
        return valid && x >= x0 && x < x1 && y >= y0 && y < y1;
    }
};

// ----------------------------------------------------------------
// PixelRGBA - 出力ピクセル
// AE: PF_Pixel8 (8bit) または PF_PixelFloat (32bit float)
// ----------------------------------------------------------------
struct PixelRGBA {
    float r, g, b, a;
};

// ----------------------------------------------------------------
// ColorRamp - 密度値 -> 色マッピング
// QuickDraw Ch3: StuffHex でパターンを直接定義したことと同じ思想
// ----------------------------------------------------------------
struct ColorRamp {
    // 密度値 [0,1] を RGBA に変換
    PixelRGBA sample(float density) const;

    // プリセット
    static ColorRamp smoke();   // 白煙
    static ColorRamp fire();    // 炎
    static ColorRamp water();   // 水
};

// ----------------------------------------------------------------
// TileRenderer
// ----------------------------------------------------------------
class TileRenderer {
public:
    // タイル1枚をレンダリング
    // out_pixels: タイルサイズの RGBA バッファ
    void renderTile(
        const RenderTile&   tile,
        const VolFrame&     vol,
        const DirtyRect&    dirty,
        int                 lod,
        float               density_scale,
        float               density_min,
        int                 transfer_mode,
        const ColorRamp&    ramp,
        PixelRGBA*          out_pixels   // [tile.width * tile.height]
    );

    // 前フレームとの差分から DirtyRect を計算
    // QuickDraw Ch3: RegionClipping の差分検出と同じ思想
    DirtyRect computeDirty(
        const float* prev,   // 前フレームの密度バッファ [W*H*D]
        const float* curr,   // 現フレームの密度バッファ
        int          count,
        float        threshold
    );

private:
    // レイキャスティング: スクリーン座標 (px, py) から密度を積算
    // QuickDraw Ch6: Graf3D の透視投影 + Ch5: スラブ蓄積
    float raymarch(
        float  px,           // スクリーン X [0,1]
        float  py,           // スクリーン Y [0,1]
        const VolFrame& vol,
        int    lod,
        float  density_min,
        int    num_steps = 32
    );

    // 転送モード適用
    // QuickDraw Ch3: patCopy / patOr / patXor / patBic 相当
    PixelRGBA applyTransfer(
        const PixelRGBA& src,    // 現在のボリューム色
        const PixelRGBA& dst,    // 既存のフレームバッファ色
        int              mode
    );
};
