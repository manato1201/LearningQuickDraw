#include <SDL.h>
#include <cmath>
#include "framebuffer.h"
#include "renderer.h"
#include "math3d.h"

static const int W = 640;
static const int H = 480;

// Cube vertices - QuickDraw Ch6: 0601RotatingCube.p verts[0..7]
static const Vec3 CUBE_VERTS[8] = {
    Vec3(-1.0f,  1.0f, -1.0f),
    Vec3(-1.0f, -1.0f, -1.0f),
    Vec3(-1.0f, -1.0f,  1.0f),
    Vec3(-1.0f,  1.0f,  1.0f),
    Vec3( 1.0f,  1.0f,  1.0f),
    Vec3( 1.0f, -1.0f,  1.0f),
    Vec3( 1.0f, -1.0f, -1.0f),
    Vec3( 1.0f,  1.0f, -1.0f),
};

// Wireframe edges (12 edges)
static const int CUBE_EDGES[12][2] = {
    {0,1},{1,2},{2,3},{3,0},
    {4,5},{5,6},{6,7},{7,4},
    {0,7},{1,6},{2,5},{3,4},
};

// Solid faces: 6 faces x 2 triangles = 12
static const int CUBE_FACES[12][3] = {
    {0,1,2},{0,2,3},
    {7,6,5},{7,5,4},
    {3,2,5},{3,5,4},
    {0,7,6},{0,6,1},
    {0,3,4},{0,4,7},
    {1,6,5},{1,5,2},
};

// 2 triangles per face share same color
static const uint32_t FACE_COLORS[6] = {
    0xFFCC4444,
    0xFF44CC44,
    0xFF4444CC,
    0xFFCCCC44,
    0xFF44CCCC,
    0xFFCC44CC,
};

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Mini Software Renderer  [Space: Wire/Solid | ESC: Quit]",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        W, H, SDL_WINDOW_SHOWN);

    SDL_Renderer* sdlRend = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    SDL_Texture* texture = SDL_CreateTexture(
        sdlRend,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        W, H);

    Framebuffer fb(W, H);
    Renderer    renderer(fb);

    float pitch     = 0.0f;
    float yaw       = 0.0f;
    float roll      = 0.0f;
    bool  wireframe = false;
    bool  running   = true;

    // Precompute perspective factor once — fov never changes
    const float PROJ_F = 1.0f / std::tan(60.0f * (3.14159265f / 180.0f) * 0.5f);

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_SPACE)  wireframe = !wireframe;
                if (event.key.keysym.sym == SDLK_ESCAPE) running   = false;
            }
        }

        // Clear color + depth each frame
        fb.clear(0xFF111111);
        fb.clearDepth();

        // Update rotation - QuickDraw Ch6: Pitch / Yaw / Roll
        pitch += 0.010f;
        yaw   += 0.020f;
        roll  += 0.005f;

        Mat4 rot = Mat4::rotationX(pitch)
                 * Mat4::rotationY(yaw)
                 * Mat4::rotationZ(roll);

        // Transform all vertices in view space, then project
        Vec3     viewVerts[8];
        ProjVert projected[8];
        for (int i = 0; i < 8; i++) {
            viewVerts[i] = rot.transform(CUBE_VERTS[i]);
            projected[i] = perspectiveProjectFast(viewVerts[i], W, H, PROJ_F);
        }

        if (wireframe) {
            // Wireframe: fixed-point DDA - QuickDraw Ch5: 0503
            for (int e = 0; e < 12; e++) {
                int a = CUBE_EDGES[e][0];
                int b = CUBE_EDGES[e][1];
                renderer.drawLineFixed(
                    (int)projected[a].sx, (int)projected[a].sy,
                    (int)projected[b].sx, (int)projected[b].sy,
                    0xFF00FF88);
            }
        } else {
            // Solid: slab fill with Z-buffer - QuickDraw Ch5: 0507
            for (int f = 0; f < 12; f++) {
                int a = CUBE_FACES[f][0];
                int b = CUBE_FACES[f][1];
                int c = CUBE_FACES[f][2];

                // Backface cull: skip triangles facing away from camera.
                // Screen-space signed area < 0 → front-facing (y-down convention).
                float ax = projected[b].sx - projected[a].sx;
                float ay = projected[b].sy - projected[a].sy;
                float bx = projected[c].sx - projected[a].sx;
                float by = projected[c].sy - projected[a].sy;
                if (ax * by - ay * bx >= 0.0f) continue;

                renderer.fillTriangleZ(
                    (int)projected[a].sx, (int)projected[a].sy, projected[a].depth,
                    (int)projected[b].sx, (int)projected[b].sy, projected[b].depth,
                    (int)projected[c].sx, (int)projected[c].sy, projected[c].depth,
                    FACE_COLORS[f / 2]);
            }
        }

        // Framebuffer -> SDL texture -> screen
        SDL_UpdateTexture(texture, NULL, fb.data(), W * (int)sizeof(uint32_t));
        SDL_RenderClear(sdlRend);
        SDL_RenderCopy(sdlRend, texture, NULL, NULL);
        SDL_RenderPresent(sdlRend);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(sdlRend);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
