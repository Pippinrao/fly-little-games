#include <metal_stdlib>
using namespace metal;

struct FlyNesVertexOut {
    float4 position [[position]];
    float2 texcoord;
};

struct CrtUniforms {
    float2 textureSize;
    float2 outputSize;
};

vertex FlyNesVertexOut flynes_crt_vs(uint vid [[vertex_id]]) {
    const float2 positions[4] = {
        float2(-1.0, -1.0), float2(1.0, -1.0),
        float2(-1.0,  1.0), float2(1.0,  1.0)
    };
    const float2 uvs[4] = {
        float2(0.0, 1.0), float2(1.0, 1.0),
        float2(0.0, 0.0), float2(1.0, 0.0)
    };
    FlyNesVertexOut out;
    out.position = float4(positions[vid], 0.0, 1.0);
    out.texcoord = uvs[vid];
    return out;
}

fragment float4 flynes_crt_fs(FlyNesVertexOut in [[stage_in]],
                              constant CrtUniforms& uniforms [[buffer(0)]],
                              texture2d<float> tex [[texture(0)]]) {
    constexpr sampler linear_sampler(address::clamp_to_edge, filter::linear);
    float4 color = tex.sample(linear_sampler, in.texcoord);
    float scanline = 0.88 + 0.12 * sin(in.texcoord.y * uniforms.outputSize.y * 3.14159265);
    float mask = 0.96 + 0.04 * sin(in.texcoord.x * uniforms.outputSize.x * 2.0943951);
    float2 edge = in.texcoord * (float2(1.0) - in.texcoord);
    float vignette = clamp(pow(16.0 * edge.x * edge.y, 0.12), 0.78, 1.0);
    return float4(color.rgb * scanline * mask * vignette, color.a);
}
