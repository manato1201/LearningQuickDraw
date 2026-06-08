// ================================================================
// Pixel Shader (HLSL - DirectX 11 reference)
//
// GLSL cube.frag.glsl との対応:
//   in  vec3 vColor    -> float3 vColor : COLOR
//   out vec4 fragColor -> float4 return : SV_Target
// ================================================================

struct PSInput {
    float4 position : SV_Position;
    float3 vColor   : COLOR;
};

// HLSL と GLSL の主な違い:
//   GLSL: out vec4 fragColor;  fragColor = vec4(...);
//   HLSL: float4 main(...) : SV_Target { return float4(...); }
float4 main(PSInput input) : SV_Target {
    return float4(input.vColor, 1.0);
}
