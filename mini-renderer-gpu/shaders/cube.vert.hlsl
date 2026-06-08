// ================================================================
// Vertex Shader (HLSL - DirectX 11 reference)
//
// GLSL cube.vert.glsl との対応:
//   layout(location=0) in vec3  -> float3 aPos   : POSITION
//   layout(location=1) in vec3  -> float3 aColor : COLOR
//   uniform mat4 uRotation      -> cbuffer の float4x4 uRotation
//   gl_Position                 -> SV_Position
// ================================================================

cbuffer FrameConstants : register(b0) {
    float4x4 uRotation;
    float    uFov;
    float    uAspect;
    float2   _pad;
};

struct VSInput {
    float3 aPos   : POSITION;
    float3 aColor : COLOR;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 vColor   : COLOR;
};

VSOutput main(VSInput input) {
    VSOutput output;

    // rot.transform(v) 相当
    float3 rotated = mul(uRotation, float4(input.aPos, 1.0)).xyz;

    // perspectiveProject() 相当
    float z = rotated.z + 4.0;
    z = max(z, 0.001);
    float f  = 1.0 / tan(radians(uFov) * 0.5);

    float px = (rotated.x * f) / z;
    float py = (rotated.y * f) / z;

    float near  = 0.1;
    float far   = 20.0;
    float ndc_z = (z - near) / (far - near);  // DirectX: [0, 1]

    // HLSL と GLSL の主な違い:
    //   GLSL: gl_Position = vec4(...)        NDC Z: [-1, 1]
    //   HLSL: SV_Position = float4(...)      NDC Z: [ 0, 1]
    output.position = float4(px / uAspect, py, ndc_z, 1.0);
    output.vColor   = input.aColor;

    return output;
}
