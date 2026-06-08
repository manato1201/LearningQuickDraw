#version 330 core

// ================================================================
// Fragment Shader (GLSL)
//
// Phase 1 (CPU) との対応:
//   putPixel(x, y, color)  -> fragColor への出力
//   testAndSetDepth()      -> GPU が gl_FragDepth で自動処理
// ================================================================

in  vec3 vColor;
out vec4 fragColor;

void main() {
    // Phase 1: putPixel(x, y, color) 相当
    // GPU が自動的に深度テスト (testAndSetDepth 相当) を行う
    fragColor = vec4(vColor, 1.0);
}
