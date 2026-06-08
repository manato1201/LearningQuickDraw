#pragma once
#include <cmath>

// ================================================================
// Vec3 - 3D Vector
// QuickDraw Ch6: Point3D
// ================================================================
struct Vec3 {
    float x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(float s)       const { return Vec3(x * s,   y * s,   z * s  ); }

    float dot(const Vec3& o) const { return x*o.x + y*o.y + z*o.z; }
    float length()           const { return std::sqrt(dot(*this)); }
};

// ================================================================
// Vec2 - Screen Space
// ================================================================
struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float x, float y) : x(x), y(y) {}
};

// ================================================================
// Projected vertex: screen XY + depth Z for Z-buffer
// ================================================================
struct ProjVert {
    float sx;    // screen x
    float sy;    // screen y
    float depth; // view-space Z (smaller = closer to camera)
    ProjVert() : sx(0), sy(0), depth(0) {}
    ProjVert(float sx, float sy, float depth) : sx(sx), sy(sy), depth(depth) {}
};

// ================================================================
// Mat4 - 4x4 Matrix
//
// QuickDraw Ch6 mapping:
//   rotationX -> Pitch()
//   rotationY -> Yaw()
//   rotationZ -> Roll()
//   transform -> Transform()
// ================================================================
struct Mat4 {
    float m[4][4];

    Mat4();

    static Mat4 identity();
    static Mat4 rotationX(float angle);
    static Mat4 rotationY(float angle);
    static Mat4 rotationZ(float angle);

    Mat4  operator*(const Mat4& o) const;
    Vec3  transform(const Vec3& v) const;
};

// --- Mat4 implementations ---

inline Mat4::Mat4() {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            m[i][j] = 0.0f;
}

inline Mat4 Mat4::identity() {
    Mat4 r;
    r.m[0][0] = 1.0f;
    r.m[1][1] = 1.0f;
    r.m[2][2] = 1.0f;
    r.m[3][3] = 1.0f;
    return r;
}

// X-axis rotation (QuickDraw: Pitch)
inline Mat4 Mat4::rotationX(float angle) {
    Mat4  r = identity();
    float c = std::cos(angle);
    float s = std::sin(angle);
    r.m[1][1] =  c;
    r.m[1][2] = -s;
    r.m[2][1] =  s;
    r.m[2][2] =  c;
    return r;
}

// Y-axis rotation (QuickDraw: Yaw)
inline Mat4 Mat4::rotationY(float angle) {
    Mat4  r = identity();
    float c = std::cos(angle);
    float s = std::sin(angle);
    r.m[0][0] =  c;
    r.m[0][2] =  s;
    r.m[2][0] = -s;
    r.m[2][2] =  c;
    return r;
}

// Z-axis rotation (QuickDraw: Roll)
inline Mat4 Mat4::rotationZ(float angle) {
    Mat4  r = identity();
    float c = std::cos(angle);
    float s = std::sin(angle);
    r.m[0][0] =  c;
    r.m[0][1] = -s;
    r.m[1][0] =  s;
    r.m[1][1] =  c;
    return r;
}

inline Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                r.m[i][j] += m[i][k] * o.m[k][j];
            }
        }
    }
    return r;
}

// Vertex transform (QuickDraw: Transform)
inline Vec3 Mat4::transform(const Vec3& v) const {
    float x = m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z + m[0][3];
    float y = m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z + m[1][3];
    float z = m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z + m[2][3];
    return Vec3(x, y, z);
}

// ================================================================
// perspectiveProject
//
// QuickDraw Ch6 mapping:
//   ViewAngle -> fov
//   LookAt    -> camera offset
// ================================================================
inline ProjVert perspectiveProject(const Vec3& v, int screenW, int screenH, float fovDeg = 60.0f) {
    float fovRad = fovDeg * (3.14159265f / 180.0f);
    float f      = 1.0f / std::tan(fovRad * 0.5f);
    float z      = v.z + 4.0f;  // camera offset: push scene 4 units in front
    if (z < 0.001f) z = 0.001f;

    float px = (v.x * f) / z;
    float py = (v.y * f) / z;

    float sx = px *  (float)screenW * 0.5f + (float)screenW * 0.5f;
    float sy = py * -(float)screenH * 0.5f + (float)screenH * 0.5f;

    // depth: smaller z = closer to camera
    return ProjVert(sx, sy, z);
}
