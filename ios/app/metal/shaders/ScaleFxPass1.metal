/*
	ScaleFX - Pass 1 (Metal port)
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

constant float SFX_CLR = 0.5;
constant float SFX_SAA = 1.0;

float str(float d, float2 a, float2 b) {
    float diff = a.x - a.y;
    float wght1 = max(SFX_CLR - d, 0.) / SFX_CLR;
    float wght2 = clamp((1. - d) + (min(a.x, b.x) + a.x > min(a.y, b.y) + a.y ? diff : -diff), 0., 1.);
    return (SFX_SAA == 1. || 2. * d < a.x + a.y) ? (wght1 * wght2) * (a.x * a.y) : 0.;
}

vertex FlyNesVertexOut flynes_scalefx_pass1_vs(uint vid [[vertex_id]]) {
    const float2 positions[4] = {
        float2(-1.0, -1.0), float2(1.0, -1.0),
        float2(-1.0,  1.0), float2(1.0,  1.0)
    };
    FlyNesVertexOut out;
    out.position = float4(positions[vid], 0.0, 1.0);
    out.texcoord = float2((positions[vid].x + 1.0) * 0.5, (1.0 - positions[vid].y) * 0.5);
    return out;
}

fragment float4 flynes_scalefx_pass1_fs(FlyNesVertexOut in [[stage_in]],
                                        constant ScaleFxUniforms& uniforms [[buffer(0)]],
                                        texture2d<float> tex [[texture(0)]]) {
    constexpr sampler nearest_sampler(address::clamp_to_edge, filter::nearest);
    float2 t = 1.0 / uniforms.textureSize;
    float4 A = tex.sample(nearest_sampler, in.texcoord + float2(-t.x, -t.y));
    float4 B = tex.sample(nearest_sampler, in.texcoord + float2(0.0, -t.y));
    float4 D = tex.sample(nearest_sampler, in.texcoord + float2(-t.x, 0.0));
    float4 E = tex.sample(nearest_sampler, in.texcoord);
    float4 F = tex.sample(nearest_sampler, in.texcoord + float2(t.x, 0.0));
    float4 G = tex.sample(nearest_sampler, in.texcoord + float2(-t.x, t.y));
    float4 H = tex.sample(nearest_sampler, in.texcoord + float2(0.0, t.y));
    float4 I = tex.sample(nearest_sampler, in.texcoord + float2(t.x, t.y));
    float4 res;
    res.x = str(D.z, float2(D.w, E.y), float2(A.w, D.y));
    res.y = str(F.x, float2(E.w, E.y), float2(B.w, F.y));
    res.z = str(H.z, float2(E.w, H.y), float2(H.w, I.y));
    res.w = str(H.x, float2(D.w, H.y), float2(G.w, G.y));
    return res;
}
