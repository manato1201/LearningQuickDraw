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

// Combined Rz*Ry*Rx rotation — 6 trig calls, no matrix multiply needed.
// Expands the analytical product and writes directly into column-major output.
static void matRotXYZ(float pitch, float yaw, float roll, float m[16]) {
    float cx = cosf(pitch), sx = sinf(pitch);
    float cy = cosf(yaw),   sy = sinf(yaw);
    float cz = cosf(roll),  sz = sinf(roll);

    // Column-major: m[col*4 + row]
    // Column 0
    m[0]  =  cy*cz;             m[1]  =  cy*sz;             m[2]  = -sy;      m[3]  = 0.0f;
    // Column 1
    m[4]  =  sx*sy*cz - cx*sz;  m[5]  =  sx*sy*sz + cx*cz;  m[6]  =  sx*cy;  m[7]  = 0.0f;
    // Column 2
    m[8]  =  cx*sy*cz + sx*sz;  m[9]  =  cx*sy*sz - sx*cz;  m[10] =  cx*cy;  m[11] = 0.0f;
    // Column 3
    m[12] =  0.0f;              m[13] =  0.0f;               m[14] =  0.0f;   m[15] = 1.0f;
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

        // Build rotation matrix (Pitch / Yaw / Roll) — single combined matrix
        pitch += 0.010f;
        yaw   += 0.020f;
        roll  += 0.005f;

        float rot[16];
        matRotXYZ(pitch, yaw, roll, rot);

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
