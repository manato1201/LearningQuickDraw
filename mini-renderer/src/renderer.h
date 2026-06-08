#pragma once
#include "framebuffer.h"
#include "math3d.h"
#include <cstdint>

// 2D drawing primitives
// QuickDraw Ch3-5
class Renderer {
public:
    explicit Renderer(Framebuffer& fb);

    // Float DDA line - QuickDraw Ch5: 0501DDALine.p
    void drawLineDDA(int x0, int y0, int x1, int y1, uint32_t color);

    // Fixed-point DDA line (fast) - QuickDraw Ch5: 0503DDAFixedLineSpeed.p
    void drawLineFixed(int x0, int y0, int x1, int y1, uint32_t color);

    // Slab triangle fill (no depth) - QuickDraw Ch5: 0507FixedRectSlabDrawLine.p
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color);

    // Slab triangle fill with Z-buffer depth test
    void fillTriangleZ(
        int x0, int y0, float z0,
        int x1, int y1, float z1,
        int x2, int y2, float z2,
        uint32_t color);

    // Clip rect - QuickDraw Ch3: 0310RegionClipping.p
    void setClipRect(int x, int y, int w, int h);
    void clearClipRect();

private:
    Framebuffer& m_fb;
    bool         m_clipEnabled;
    int          m_clipX;
    int          m_clipY;
    int          m_clipW;
    int          m_clipH;

    void putPixel(int x, int y, uint32_t color);
};
