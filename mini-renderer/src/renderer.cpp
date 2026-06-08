#include "renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

// Initialize clip state
namespace {
    // (no globals needed)
}

// ================================================================
// Constructor - initialize clip state
// ================================================================
Renderer::Renderer(Framebuffer& fb)
    : m_fb(fb)
    , m_clipEnabled(false)
    , m_clipX(0), m_clipY(0), m_clipW(0), m_clipH(0)
{}

// ================================================================
// putPixel
// QuickDraw Ch3: SetClip / clipRgn
// ================================================================
void Renderer::putPixel(int x, int y, uint32_t color) {
    if (m_clipEnabled) {
        if (x < m_clipX || x >= m_clipX + m_clipW) return;
        if (y < m_clipY || y >= m_clipY + m_clipH) return;
    }
    m_fb.setPixel(x, y, color);
}

// ================================================================
// drawLineDDA - float DDA line
// QuickDraw Ch5: 0501DDALine.p
// ================================================================
void Renderer::drawLineDDA(int x0, int y0, int x1, int y1, uint32_t color) {
    int dv = y1 - y0;
    int dh = x1 - x0;

    if (std::abs(dh) >= std::abs(dv)) {
        if (x1 < x0) { std::swap(x0, x1); std::swap(y0, y1); dv = -dv; dh = -dh; }
        if (dh == 0) { putPixel(x0, y0, color); return; }
        float slope = (float)dv / (float)dh;
        float fy    = (float)y0;
        for (int x = x0; x <= x1; x++) {
            putPixel(x, (int)std::round(fy), color);
            fy += slope;
        }
    } else {
        if (y1 < y0) { std::swap(x0, x1); std::swap(y0, y1); dv = -dv; dh = -dh; }
        if (dv == 0) { putPixel(x0, y0, color); return; }
        float slope = (float)dh / (float)dv;
        float fx    = (float)x0;
        for (int y = y0; y <= y1; y++) {
            putPixel((int)std::round(fx), y, color);
            fx += slope;
        }
    }
}

// ================================================================
// drawLineFixed - fixed-point DDA line (fast)
// QuickDraw Ch5: 0503DDAFixedLineSpeed.p
//
// float -> int: upper 16bit = integer part, lower 16bit = fraction
// ================================================================
void Renderer::drawLineFixed(int x0, int y0, int x1, int y1, uint32_t color) {
    const int SHIFT = 16;

    int dv = y1 - y0;
    int dh = x1 - x0;

    if (std::abs(dh) >= std::abs(dv)) {
        if (x1 < x0) { std::swap(x0, x1); std::swap(y0, y1); dv = -dv; dh = -dh; }
        if (dh == 0) { putPixel(x0, y0, color); return; }
        int slope = (dv << SHIFT) / dh;
        int fy    = y0 << SHIFT;
        for (int x = x0; x <= x1; x++) {
            putPixel(x, fy >> SHIFT, color);
            fy += slope;
        }
    } else {
        if (y1 < y0) { std::swap(x0, x1); std::swap(y0, y1); dv = -dv; dh = -dh; }
        if (dv == 0) { putPixel(x0, y0, color); return; }
        int slope = (dh << SHIFT) / dv;
        int fx    = x0 << SHIFT;
        for (int y = y0; y <= y1; y++) {
            putPixel(fx >> SHIFT, y, color);
            fx += slope;
        }
    }
}

// ================================================================
// fillTriangle - slab scanline fill
// QuickDraw Ch5: 0507FixedRectSlabDrawLine.p
//
// Split triangle into horizontal slabs.
// For each scanline: find X on long edge and short edge, then fill.
// ================================================================
void Renderer::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); }
    if (y1 > y2) { std::swap(x1, x2); std::swap(y1, y2); }
    if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); }

    int totalH = y2 - y0;
    if (totalH == 0) return;

    for (int y = y0; y <= y2; y++) {
        bool upper = (y < y1) || (y1 == y0);

        int xa = x0 + (x2 - x0) * (y - y0) / totalH;

        int xb = 0;
        if (upper) {
            int segH = y1 - y0;
            xb = (segH == 0) ? x0 : x0 + (x1 - x0) * (y - y0) / segH;
        } else {
            int segH = y2 - y1;
            xb = (segH == 0) ? x1 : x1 + (x2 - x1) * (y - y1) / segH;
        }

        if (xa > xb) std::swap(xa, xb);
        for (int x = xa; x <= xb; x++) {
            putPixel(x, y, color);
        }
    }
}

// ================================================================
// fillTriangleZ - slab fill with Z-buffer depth test
//
// Same slab algorithm as fillTriangle, but:
//   - interpolates depth Z across each scanline
//   - calls testAndSetDepth() before writing pixel
//   - pixels behind existing geometry are discarded
// ================================================================
void Renderer::fillTriangleZ(
    int x0, int y0, float z0,
    int x1, int y1, float z1,
    int x2, int y2, float z2,
    uint32_t color)
{
    // Sort by Y (carry Z along)
    if (y0 > y1) { std::swap(x0,x1); std::swap(y0,y1); std::swap(z0,z1); }
    if (y1 > y2) { std::swap(x1,x2); std::swap(y1,y2); std::swap(z1,z2); }
    if (y0 > y1) { std::swap(x0,x1); std::swap(y0,y1); std::swap(z0,z1); }

    int totalH = y2 - y0;
    if (totalH == 0) return;

    for (int y = y0; y <= y2; y++) {
        bool upper = (y < y1) || (y1 == y0);

        float t = (float)(y - y0) / (float)totalH;

        // Long edge: vertex 0 -> vertex 2
        int   xa = x0 + (int)((x2 - x0) * t);
        float za = z0 + (z2 - z0) * t;

        // Short edge
        int   xb = 0;
        float zb = 0.0f;
        if (upper) {
            int segH = y1 - y0;
            if (segH == 0) { xb = x0; zb = z0; }
            else {
                float s = (float)(y - y0) / (float)segH;
                xb = x0 + (int)((x1 - x0) * s);
                zb = z0 + (z1 - z0) * s;
            }
        } else {
            int segH = y2 - y1;
            if (segH == 0) { xb = x1; zb = z1; }
            else {
                float s = (float)(y - y1) / (float)segH;
                xb = x1 + (int)((x2 - x1) * s);
                zb = z1 + (z2 - z1) * s;
            }
        }

        // Make xa <= xb
        if (xa > xb) { std::swap(xa, xb); std::swap(za, zb); }

        // Fill scanline with Z interpolation
        int span = xb - xa;
        for (int x = xa; x <= xb; x++) {
            float frac = (span > 0) ? (float)(x - xa) / (float)span : 0.0f;
            float z    = za + (zb - za) * frac;

            if (m_fb.testAndSetDepth(x, y, z)) {
                if (!m_clipEnabled ||
                    (x >= m_clipX && x < m_clipX + m_clipW &&
                     y >= m_clipY && y < m_clipY + m_clipH)) {
                    m_fb.setPixel(x, y, color);
                }
            }
        }
    }
}

// ================================================================
// setClipRect / clearClipRect
// QuickDraw Ch3: SetClip
// ================================================================
void Renderer::setClipRect(int x, int y, int w, int h) {
    m_clipEnabled = true;
    m_clipX = x;
    m_clipY = y;
    m_clipW = w;
    m_clipH = h;
}

void Renderer::clearClipRect() {
    m_clipEnabled = false;
}
