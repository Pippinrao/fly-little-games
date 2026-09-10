/*
	ScaleFX - Pass 0 (Metal port)
	by Sp00kyFox, 2017-03-01

Copyright (c) 2016 Sp00kyFox - ScaleFX@web.de

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include <metal_stdlib>
using namespace metal;

struct FlyNesVertexOut {
    float4 position [[position]];
    float2 texcoord;
};

struct ScaleFxUniforms {
    float2 textureSize;
};

float dist(float3 A, float3 B) {
    float r = 0.5 * (A.r + B.r);
    float3 d = A - B;
    float3 c = float3(2. + r, 4., 3. - r);
    return sqrt(dot(c * d, d)) / 3.;
}

vertex FlyNesVertexOut flynes_scalefx_pass0_vs(uint vid [[vertex_id]]) {
    const float2 positions[4] = {
        float2(-1.0, -1.0), float2(1.0, -1.0),
        float2(-1.0,  1.0), float2(1.0,  1.0)
    };
    const float2 uvs[4] = {
        float2(0.0, 0.0), float2(1.0, 0.0),
        float2(0.0, 1.0), float2(1.0, 1.0)
    };
    FlyNesVertexOut out;
    out.position = float4(positions[vid], 0.0, 1.0);
    out.texcoord = uvs[vid];
    return out;
}

fragment float4 flynes_scalefx_pass0_fs(FlyNesVertexOut in [[stage_in]],
                                        constant ScaleFxUniforms& uniforms [[buffer(0)]],
                                        texture2d<float> tex [[texture(0)]]) {
    constexpr sampler nearest_sampler(address::clamp_to_edge, filter::nearest);
    float2 texel = 1.0 / uniforms.textureSize;
    float3 A = tex.sample(nearest_sampler, in.texcoord + float2(-texel.x, -texel.y)).rgb;
    float3 B = tex.sample(nearest_sampler, in.texcoord + float2(0.0, -texel.y)).rgb;
    float3 C = tex.sample(nearest_sampler, in.texcoord + float2(texel.x, -texel.y)).rgb;
    float3 E = tex.sample(nearest_sampler, in.texcoord).rgb;
    float3 F = tex.sample(nearest_sampler, in.texcoord + float2(texel.x, 0.0)).rgb;
    return float4(dist(E, A), dist(E, B), dist(E, C), dist(E, F));
}
