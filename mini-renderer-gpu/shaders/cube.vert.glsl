#version 330 core

// ================================================================
// Vertex Shader (GLSL)
//
// Phase 1 (CPU) との対応:
//   rot.transform(v)      -> uRotation * vec4(aPos, 1.0)
//   perspectiveProject()  -> 焦点距離 f による透視除算
//   ProjVert.depth        -> gl_Position.z
// ================================================================

layout(location = 0) in vec3 aPos;    // 頂点座標
layout(location = 1) in vec3 aColor;  // 面の色

uniform mat4  uRotation;  // Pitch/Yaw/Roll 合成回転行列
uniform float uFov;       // 視野角 (degrees)
uniform float uAspect;    // アスペクト比 W/H

out vec3 vColor;          // フラグメントシェーダーへ渡す

void main() {
    // --- Phase 1: rot.transform(v) 相当 ---
    vec3 rotated = (uRotation * vec4(aPos, 1.0)).xyz;

    // --- Phase 1: perspectiveProject() 相当 ---
    float z = rotated.z + 4.0;                        // カメラオフセット
    z = max(z, 0.001);
    float f  = 1.0 / tan(radians(uFov) * 0.5);       // 焦点距離

    float px = (rotated.x * f) / z;
    float py = (rotated.y * f) / z;

    // OpenGL クリップ空間 [-1, 1] に変換
    // Z: 近 = -1, 遠 = +1 に正規化 (near=0.1, far=20.0)
    float near  = 0.1;
    float far   = 20.0;
    float ndc_z = (z - near) / (far - near) * 2.0 - 1.0;

    gl_Position = vec4(px / uAspect, py, ndc_z, 1.0);

    vColor = aColor;
}
