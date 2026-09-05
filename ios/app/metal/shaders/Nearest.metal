#include <metal_stdlib>
using namespace metal;

struct FlyNesVertexOut {
    float4 position [[position]];
    float2 texcoord;
};

vertex FlyNesVertexOut flynes_nearest_vs(uint vid [[vertex_id]]) {
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

fragment float4 flynes_nearest_fs(FlyNesVertexOut in [[stage_in]],
                                  texture2d<float> tex [[texture(0)]]) {
    constexpr sampler nearest_sampler(address::clamp_to_edge, filter::nearest,
                                      mag_filter::nearest, min_filter::nearest);
    return tex.sample(nearest_sampler, in.texcoord);
}
