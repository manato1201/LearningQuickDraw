#include <SDL.h>
#include <glad/gl.h>
#include <cmath>
#include <cstdio>
#include "gl_renderer.h"
#include "timer.h"

static const int W = 640;
static const int H = 480;

// ================================================================
// Minimal 4x4 matrix math (column-major for OpenGL)
// Phase 1 math3d.h と同じロジック
// ================================================================
static void matIdentity(float m[16]) {
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void matMul(const float a[16], const float b[16], float out[16]) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            out[i*4+j] = 0.0f;
            for (int k = 0; k < 4; k++)
                out[i*4+j] += a[i*4+k] * b[k*4+j];
        }
}

// X-axis rotation (Pitch) - column-major
static void matRotX(float angle, float m[16]) {
    matIdentity(m);
    float c = cosf(angle);
    float s = sinf(angle);
    m[5]  =  c;  m[6]  = s;
    m[9]  = -s;  m[10] = c;
}

// Y-axis rotation (Yaw) - column-major
static void matRotY(float angle, float m[16]) {
    matIdentity(m);
    float c = cosf(angle);
    float s = sinf(angle);
    m[0]  =  c;  m[2]  = -s;
    m[8]  =  s;  m[10] =  c;
}

// Z-axis rotation (Roll) - column-major
static void matRotZ(float angle, float m[16]) {
    matIdentity(m);
    float c = cosf(angle);
    float s = sinf(angle);
    m[0] =  c;  m[1] = s;
    m[4] = -s;  m[5] = c;
}

// ================================================================
int main(int argc, char* argv[]) {
    // SDL + OpenGL context
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow(
        "Mini Renderer GPU  [Space: Wire/Solid | ESC: Quit]",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        W, H,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);

    SDL_GLContext glCtx = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1);  // VSync

    // Load OpenGL functions via GLAD
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        SDL_Log("GLAD init failed");
        return 1;
    }

    printf("OpenGL: %s\n", glGetString(GL_VERSION));
    printf("GPU:    %s\n", glGetString(GL_RENDERER));

    GlRenderer renderer;
    if (!renderer.init(W, H)) {
        SDL_Log("Renderer init failed");
        return 1;
    }

    renderer.setFov(60.0f);
    renderer.setAspect((float)W / (float)H);

    float pitch     = 0.0f;
    float yaw       = 0.0f;
    float roll      = 0.0f;
    bool  wireframe = false;
    bool  running   = true;

    Timer cpuTimer;
    int   frameCount  = 0;
    float fpsTimer    = 0.0f;
    char  titleBuf[256];

    while (running) {
        cpuTimer.start();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_SPACE)  wireframe = !wireframe;
                if (event.key.keysym.sym == SDLK_ESCAPE) running   = false;
            }
        }

        // Build rotation matrix (Pitch / Yaw / Roll)
        // Phase 1: Mat4::rotationX/Y/Z + operator*  と同じロジック
        pitch += 0.010f;
        yaw   += 0.020f;
        roll  += 0.005f;

        float rx[16], ry[16], rz[16], rxy[16], rot[16];
        matRotX(pitch, rx);
        matRotY(yaw,   ry);
        matRotZ(roll,  rz);
        matMul(rx, ry,  rxy);
        matMul(rxy, rz, rot);

        renderer.setRotation(rot);

        if (wireframe)
            renderer.drawCubeWire();
        else
            renderer.drawCubeSolid();

        SDL_GL_SwapWindow(window);

        double cpuMs = cpuTimer.elapsedMs();
        double gpuMs = renderer.lastGpuMs();

        // FPS + timing display
        frameCount++;
        fpsTimer += (float)cpuMs;
        if (fpsTimer >= 500.0f) {
            float fps = frameCount / (fpsTimer / 1000.0f);
            snprintf(titleBuf, sizeof(titleBuf),
                "Mini Renderer GPU  |  FPS: %.0f  CPU: %.2f ms  GPU: %.3f ms  [Space: Wire/Solid]",
                fps, cpuMs, gpuMs);
            SDL_SetWindowTitle(window, titleBuf);
            frameCount = 0;
            fpsTimer   = 0.0f;
        }
    }

    renderer.shutdown();
    SDL_GL_DeleteContext(glCtx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
