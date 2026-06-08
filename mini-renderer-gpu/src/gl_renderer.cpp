#include "gl_renderer.h"
#include <fstream>
#include <sstream>
#include <cstdio>

// ================================================================
// Cube geometry (same as Phase 1)
// ================================================================
static const float CUBE_POS[8][3] = {
    {-1.0f,  1.0f, -1.0f},
    {-1.0f, -1.0f, -1.0f},
    {-1.0f, -1.0f,  1.0f},
    {-1.0f,  1.0f,  1.0f},
    { 1.0f,  1.0f,  1.0f},
    { 1.0f, -1.0f,  1.0f},
    { 1.0f, -1.0f, -1.0f},
    { 1.0f,  1.0f, -1.0f},
};

// Per-vertex colors (same face colors as Phase 1)
static const float CUBE_COLOR[8][3] = {
    {0.8f, 0.27f, 0.27f},
    {0.8f, 0.27f, 0.27f},
    {0.27f, 0.8f, 0.27f},
    {0.27f, 0.8f, 0.27f},
    {0.27f, 0.27f, 0.8f},
    {0.27f, 0.27f, 0.8f},
    {0.8f, 0.8f, 0.27f},
    {0.8f, 0.8f, 0.27f},
};

// Solid: 12 triangles (same as Phase 1 CUBE_FACES)
static const unsigned int IDX_SOLID[36] = {
    0,1,2, 0,2,3,
    7,6,5, 7,5,4,
    3,2,5, 3,5,4,
    0,7,6, 0,6,1,
    0,3,4, 0,4,7,
    1,6,5, 1,5,2,
};

// Wireframe: 12 edges (same as Phase 1 CUBE_EDGES)
static const unsigned int IDX_WIRE[24] = {
    0,1, 1,2, 2,3, 3,0,
    4,5, 5,6, 6,7, 7,4,
    0,7, 1,6, 2,5, 3,4,
};

// ================================================================
GlRenderer::GlRenderer() {}

GlRenderer::~GlRenderer() { shutdown(); }

bool GlRenderer::init(int screenW, int screenH) {
    m_w = screenW;
    m_h = screenH;

    glEnable(GL_DEPTH_TEST);   // Z-buffer: GPU automatic (Phase 1 testAndSetDepth 相当)
    glDepthFunc(GL_LESS);

    glViewport(0, 0, screenW, screenH);

    if (!compileShaders()) return false;

    buildCubeBuffers();

    // GPU timer query
    glGenQueries(1, &m_queryId);

    return true;
}

void GlRenderer::shutdown() {
    if (m_program)  { glDeleteProgram(m_program);  m_program  = 0; }
    if (m_vao)      { glDeleteVertexArrays(1, &m_vao); m_vao  = 0; }
    if (m_vboPos)   { glDeleteBuffers(1, &m_vboPos);   m_vboPos   = 0; }
    if (m_vboColor) { glDeleteBuffers(1, &m_vboColor); m_vboColor = 0; }
    if (m_iboSolid) { glDeleteBuffers(1, &m_iboSolid); m_iboSolid = 0; }
    if (m_iboWire)  { glDeleteBuffers(1, &m_iboWire);  m_iboWire  = 0; }
    if (m_queryId)  { glDeleteQueries(1, &m_queryId);  m_queryId  = 0; }
}

// ================================================================
// Shader compilation
// ================================================================
GLuint GlRenderer::loadShader(GLenum type, const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        printf("Shader not found: %s\n", path.c_str());
        return 0;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string src = ss.str();
    const char* c   = src.c_str();

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &c, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        printf("Shader compile error [%s]:\n%s\n", path.c_str(), log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool GlRenderer::compileShaders() {
    GLuint vert = loadShader(GL_VERTEX_SHADER,   "shaders/cube.vert.glsl");
    GLuint frag = loadShader(GL_FRAGMENT_SHADER, "shaders/cube.frag.glsl");
    if (!vert || !frag) return false;

    m_program = glCreateProgram();
    glAttachShader(m_program, vert);
    glAttachShader(m_program, frag);
    glLinkProgram(m_program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(m_program, 512, nullptr, log);
        printf("Program link error:\n%s\n", log);
        return false;
    }

    m_locRotation = glGetUniformLocation(m_program, "uRotation");
    m_locFov      = glGetUniformLocation(m_program, "uFov");
    m_locAspect   = glGetUniformLocation(m_program, "uAspect");

    return true;
}

// ================================================================
// Vertex / Index buffers
// ================================================================
void GlRenderer::buildCubeBuffers() {
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    // Position buffer (location = 0)
    glGenBuffers(1, &m_vboPos);
    glBindBuffer(GL_ARRAY_BUFFER, m_vboPos);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_POS), CUBE_POS, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    // Color buffer (location = 1)
    glGenBuffers(1, &m_vboColor);
    glBindBuffer(GL_ARRAY_BUFFER, m_vboColor);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_COLOR), CUBE_COLOR, GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(1);

    // Index buffers
    glGenBuffers(1, &m_iboSolid);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iboSolid);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(IDX_SOLID), IDX_SOLID, GL_STATIC_DRAW);

    glGenBuffers(1, &m_iboWire);
    // Wire IBO stored separately (unbind solid first)
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Upload wire IBO
    glBindBuffer(GL_ARRAY_BUFFER, m_iboWire);
    glGenBuffers(1, &m_iboWire);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iboWire);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(IDX_WIRE), IDX_WIRE, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// ================================================================
// Uniform setters
// ================================================================
void GlRenderer::setRotation(const float* mat4) {
    glUseProgram(m_program);
    glUniformMatrix4fv(m_locRotation, 1, GL_FALSE, mat4);
}

void GlRenderer::setFov(float fovDeg) {
    glUseProgram(m_program);
    glUniform1f(m_locFov, fovDeg);
}

void GlRenderer::setAspect(float aspect) {
    glUseProgram(m_program);
    glUniform1f(m_locAspect, aspect);
}

// ================================================================
// Draw calls
// ================================================================
void GlRenderer::drawCubeSolid() {
    glBeginQuery(GL_TIME_ELAPSED, m_queryId);

    glClearColor(0.067f, 0.071f, 0.094f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_program);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iboSolid);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glEndQuery(GL_TIME_ELAPSED);

    // Retrieve last frame GPU time (nanoseconds -> ms)
    GLuint64 ns = 0;
    glGetQueryObjectui64v(m_queryId, GL_QUERY_RESULT, &ns);
    m_lastGpuMs = (double)ns * 1e-6;
}

void GlRenderer::drawCubeWire() {
    glBeginQuery(GL_TIME_ELAPSED, m_queryId);

    glClearColor(0.067f, 0.071f, 0.094f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_program);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iboWire);
    glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glEndQuery(GL_TIME_ELAPSED);

    GLuint64 ns = 0;
    glGetQueryObjectui64v(m_queryId, GL_QUERY_RESULT, &ns);
    m_lastGpuMs = (double)ns * 1e-6;
}
