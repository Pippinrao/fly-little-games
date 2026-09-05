#include <metal_stdlib>
using namespace metal;

struct FlyNesVertexOut {
    float4 position [[position]];
    float2 texcoord;
};

struct SharpUniforms {
    float2 textureSize;
};

vertex FlyNesVertexOut flynes_sharp_vs(uint vid [[vertex_id]]) {
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

fragment float4 flynes_sharp_fs(FlyNesVertexOut in [[stage_in]],
                                constant SharpUniforms& uniforms [[buffer(0)]],
                                texture2d<float> tex [[texture(0)]]) {
    constexpr sampler linear_sampler(address::clamp_to_edge, filter::linear);
    const float2 pixel = in.texcoord * uniforms.textureSize - float2(0.5);
    const float2 base = floor(pixel);
    const float2 sharpFraction = clamp((fract(pixel) - float2(0.5)) * 2.0 + float2(0.5), 0.0, 1.0);
    const float2 uv = (base + sharpFraction + float2(0.5)) / uniforms.textureSize;
    return tex.sample(linear_sampler, uv);
}
