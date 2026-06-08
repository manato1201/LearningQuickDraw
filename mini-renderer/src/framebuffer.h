#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <limits>

// Pixel buffer + depth buffer
// QuickDraw: GrafPort portBits (BitMap)
class Framebuffer {
public:
    Framebuffer(int width, int height);

    // Color buffer
    void     clear(uint32_t color = 0xFF111111);
    void     setPixel(int x, int y, uint32_t color);
    uint32_t getPixel(int x, int y) const;

    // Depth buffer (Z-buffer)
    void  clearDepth();
    float getDepth(int x, int y) const;
    bool  testAndSetDepth(int x, int y, float depth);

    int  width()  const { return m_width;  }
    int  height() const { return m_height; }

    const uint32_t* data() const { return m_pixels.data(); }

private:
    int                   m_width;
    int                   m_height;
    std::vector<uint32_t> m_pixels;
    std::vector<float>    m_depth;
};
