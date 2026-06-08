/*
 * vol_loader.cpp
 */

#include "vol_loader.h"
#include <fstream>
#include <cstring>
#include <cstdio>

std::unique_ptr<VolFrame> ReadVolFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;

    // Magic check
    char magic[3];
    f.read(magic, 3);
    if (magic[0] != 'V' || magic[1] != 'O' || magic[2] != 'L')
        return nullptr;

    uint8_t version;
    f.read(reinterpret_cast<char*>(&version), 1);

    auto frame = std::make_unique<VolFrame>();
    VolHeader& h = frame->header;

    int32_t encoding;
    f.read(reinterpret_cast<char*>(&encoding), 4);
    f.read(reinterpret_cast<char*>(&h.x_res),      4);
    f.read(reinterpret_cast<char*>(&h.y_res),      4);
    f.read(reinterpret_cast<char*>(&h.z_res),      4);
    f.read(reinterpret_cast<char*>(&h.n_channels), 4);
    f.read(reinterpret_cast<char*>(h.aabb_min),    12);
    f.read(reinterpret_cast<char*>(h.aabb_max),    12);

    if (!f) return nullptr;

    int count = h.x_res * h.y_res * h.z_res * h.n_channels;
    frame->data = std::make_unique<float[]>(count);
    f.read(reinterpret_cast<char*>(frame->data.get()), count * sizeof(float));

    if (!f) return nullptr;
    return frame;
}

std::string VolFramePath(const char* cache_dir, int frame) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/smoke_%04d.vol", cache_dir, frame);
    return buf;
}
