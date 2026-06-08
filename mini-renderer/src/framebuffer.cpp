#include "framebuffer.h"

static const float DEPTH_FAR = 1.0e30f;

Framebuffer::Framebuffer(int width, int height)
    : m_width(width)
    , m_height(height)
    , m_pixels(width * height, 0xFF111111)
    , m_depth(width * height, DEPTH_FAR)
{}

void Framebuffer::clear(uint32_t color) {
    std::fill(m_pixels.begin(), m_pixels.end(), color);
}

void Framebuffer::clearDepth() {
    std::fill(m_depth.begin(), m_depth.end(), DEPTH_FAR);
}

void Framebuffer::setPixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
    m_pixels[y * m_width + x] = color;
}

uint32_t Framebuffer::getPixel(int x, int y) const {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return 0;
    return m_pixels[y * m_width + x];
}

float Framebuffer::getDepth(int x, int y) const {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return DEPTH_FAR;
    return m_depth[y * m_width + x];
}

// Returns true and writes depth if new depth is closer (smaller)
bool Framebuffer::testAndSetDepth(int x, int y, float depth) {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return false;
    float& slot = m_depth[y * m_width + x];
    if (depth < slot) {
        slot = depth;
        return true;
    }
    return false;
}
