#pragma once
#include <glad/gl.h>
#include <string>

// OpenGL shader program wrapper
class GlRenderer {
public:
    GlRenderer();
    ~GlRenderer();

    bool init(int screenW, int screenH);
    void shutdown();

    // Upload 4x4 rotation matrix (column-major float[16])
    void setRotation(const float* mat4);
    void setFov(float fovDeg);
    void setAspect(float aspect);

    // Draw cube: solid or wireframe
    void drawCubeSolid();
    void drawCubeWire();

    // Returns last GPU frame time in ms (via timer query)
    double lastGpuMs() const { return m_lastGpuMs; }

private:
    bool   compileShaders();
    GLuint loadShader(GLenum type, const std::string& path);
    void   buildCubeBuffers();

    int    m_w = 0;
    int    m_h = 0;

    GLuint m_program    = 0;
    GLuint m_vao        = 0;
    GLuint m_vboPos     = 0;
    GLuint m_vboColor   = 0;
    GLuint m_iboSolid   = 0;
    GLuint m_iboWire    = 0;

    GLint  m_locRotation = -1;
    GLint  m_locFov      = -1;
    GLint  m_locAspect   = -1;

    GLuint m_queryId    = 0;
    double m_lastGpuMs  = 0.0;
};
