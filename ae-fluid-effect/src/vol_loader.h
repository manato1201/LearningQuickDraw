/*
 * vol_loader.h
 * Jet .vol キャッシュ読み込み (C++ 版)
 * Phase 1-3 の Python 実装を C++ に移植
 */

#pragma once
#include <cstdint>
#include <memory>
#include <string>

struct VolHeader {
    int32_t x_res;
    int32_t y_res;
    int32_t z_res;
    int32_t n_channels;
    float   aabb_min[3];
    float   aabb_max[3];
};

struct VolFrame {
    VolHeader             header;
    std::unique_ptr<float[]> data;   // [x_res * y_res * z_res]

    int voxel_count() const {
        return header.x_res * header.y_res * header.z_res;
    }

    float sample(int x, int y, int z) const {
        if (x < 0 || x >= header.x_res) return 0.f;
        if (y < 0 || y >= header.y_res) return 0.f;
        if (z < 0 || z >= header.z_res) return 0.f;
        return data[z * header.y_res * header.x_res
                  + y * header.x_res
                  + x];
    }

    // LOD ダウンサンプル (stride slice)
    // QuickDraw Ch5: 固定小数点最適化の思想 - 精度を落として処理量削減
    float sample_lod(int x, int y, int z, int lod) const {
        int step = 1 << lod;
        return sample(x * step, y * step, z * step);
    }
};

// ----------------------------------------------------------------
// .vol ファイル読み込み
// ----------------------------------------------------------------
std::unique_ptr<VolFrame> ReadVolFile(const char* path);

// フレームパス生成 (smoke_XXXX.vol)
std::string VolFramePath(const char* cache_dir, int frame);

// LOD サイズ計算
inline int LodSize(int original, int lod) {
    return (original + (1 << lod) - 1) >> lod;
}
