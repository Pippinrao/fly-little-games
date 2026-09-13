/*
	ScaleFX - Pass 2 (Metal port)
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

float4 GE(float4 x, float4 y) { return 1. - step(x, y); }
float4 LE(float4 x, float4 y) { return 1. - step(y, x); }
float4 LEQ(float4 x, float4 y) { return step(x, y); }
float4 NOT4(float4 x) { return 1. - x; }

float4 dom(float3 x, float3 y, float3 z, float3 w) {
    return 2. * float4(x.y, y.y, z.y, w.y) - (float4(x.x, y.x, z.x, w.x) + float4(x.z, y.z, z.z, w.z));
}

float clear(float2 crn, float2 a, float2 b) {
    return (crn.x >= max(min(a.x, a.y), min(b.x, b.y))) && (crn.y >= max(min(a.x, b.y), min(b.x, a.y))) ? 1. : 0.;
}

vertex FlyNesVertexOut flynes_scalefx_pass2_vs(uint vid [[vertex_id]]) {
    const float2 positions[4] = {
        float2(-1.0, -1.0), float2(1.0, -1.0),
        float2(-1.0,  1.0), float2(1.0,  1.0)
    };
    FlyNesVertexOut out;
    out.position = float4(positions[vid], 0.0, 1.0);
    out.texcoord = float2((positions[vid].x + 1.0) * 0.5, (1.0 - positions[vid].y) * 0.5);
    return out;
}

fragment float4 flynes_scalefx_pass2_fs(FlyNesVertexOut in [[stage_in]],
                                        constant ScaleFxUniforms& uniforms [[buffer(0)]],
                                        texture2d<float> metric [[texture(0)]],
                                        texture2d<float> strength [[texture(1)]]) {
    constexpr sampler nearest_sampler(address::clamp_to_edge, filter::nearest);
    float2 t = 1.0 / uniforms.textureSize;
    float4 A = metric.sample(nearest_sampler, in.texcoord + float2(-t.x, -t.y));
    float4 B = metric.sample(nearest_sampler, in.texcoord + float2(0.0, -t.y));
    float4 D = metric.sample(nearest_sampler, in.texcoord + float2(-t.x, 0.0));
    float4 E = metric.sample(nearest_sampler, in.texcoord);
    float4 F = metric.sample(nearest_sampler, in.texcoord + float2(t.x, 0.0));
    float4 G = metric.sample(nearest_sampler, in.texcoord + float2(-t.x, t.y));
    float4 H = metric.sample(nearest_sampler, in.texcoord + float2(0.0, t.y));
    float4 I = metric.sample(nearest_sampler, in.texcoord + float2(t.x, t.y));
    float4 As = strength.sample(nearest_sampler, in.texcoord + float2(-t.x, -t.y));
    float4 Bs = strength.sample(nearest_sampler, in.texcoord + float2(0.0, -t.y));
    float4 Cs = strength.sample(nearest_sampler, in.texcoord + float2(t.x, -t.y));
    float4 Ds = strength.sample(nearest_sampler, in.texcoord + float2(-t.x, 0.0));
    float4 Es = strength.sample(nearest_sampler, in.texcoord);
    float4 Fs = strength.sample(nearest_sampler, in.texcoord + float2(t.x, 0.0));
    float4 Gs = strength.sample(nearest_sampler, in.texcoord + float2(-t.x, t.y));
    float4 Hs = strength.sample(nearest_sampler, in.texcoord + float2(0.0, t.y));
    float4 Is = strength.sample(nearest_sampler, in.texcoord + float2(t.x, t.y));

    float4 jSx = float4(As.z, Bs.w, Es.x, Ds.y);
    float4 jDx = dom(As.yzw, Bs.zwx, Es.wxy, Ds.xyz);
    float4 jSy = float4(Bs.z, Cs.w, Fs.x, Es.y);
    float4 jDy = dom(Bs.yzw, Cs.zwx, Fs.wxy, Es.xyz);
    float4 jSz = float4(Es.z, Fs.w, Is.x, Hs.y);
    float4 jDz = dom(Es.yzw, Fs.zwx, Is.wxy, Hs.xyz);
    float4 jSw = float4(Ds.z, Es.w, Hs.x, Gs.y);
    float4 jDw = dom(Ds.yzw, Es.zwx, Hs.wxy, Gs.xyz);

    float4 zero4 = float4(0.);
    float4 jx = min(GE(jDx, zero4) * (LEQ(jDx.yzwx, zero4) * LEQ(jDx.wxyz, zero4) + GE(jDx + jDx.zwxy, jDx.yzwx + jDx.wxyz)), 1.);
    float4 jy = min(GE(jDy, zero4) * (LEQ(jDy.yzwx, zero4) * LEQ(jDy.wxyz, zero4) + GE(jDy + jDy.zwxy, jDy.yzwx + jDy.wxyz)), 1.);
    float4 jz = min(GE(jDz, zero4) * (LEQ(jDz.yzwx, zero4) * LEQ(jDz.wxyz, zero4) + GE(jDz + jDz.zwxy, jDz.yzwx + jDz.wxyz)), 1.);
    float4 jw = min(GE(jDw, zero4) * (LEQ(jDw.yzwx, zero4) * LEQ(jDw.wxyz, zero4) + GE(jDw + jDw.zwxy, jDw.yzwx + jDw.wxyz)), 1.);

    float4 res;
    res.x = min(jx.z + NOT4(jx.y) * NOT4(jx.w) * GE(jSx.z, 0.) * (jx.x + GE(jSx.x + jSx.z, jSx.y + jSx.w)), 1.).x;
    res.y = min(jy.w + NOT4(jy.z) * NOT4(jy.x) * GE(jSy.w, 0.) * (jy.y + GE(jSy.y + jSy.w, jSy.x + jSy.z)), 1.).x;
    res.z = min(jz.x + NOT4(jz.w) * NOT4(jz.y) * GE(jSz.x, 0.) * (jz.z + GE(jSz.x + jSz.z, jSz.y + jSz.w)), 1.).x;
    res.w = min(jw.y + NOT4(jw.x) * NOT4(jw.z) * GE(jSw.y, 0.) * (jw.w + GE(jSw.y + jSw.w, jSw.x + jSw.z)), 1.).x;
    res = min(res * (float4(jx.z, jy.w, jz.x, jw.y) + NOT4(res.wxyz * res.yzwx)), 1.);

    float4 clr;
    clr.x = clear(float2(D.z, E.x), float2(D.w, E.y), float2(A.w, D.y));
    clr.y = clear(float2(F.x, E.z), float2(E.w, E.y), float2(B.w, F.y));
    clr.z = clear(float2(H.z, I.x), float2(E.w, H.y), float2(H.w, I.y));
    clr.w = clear(float2(H.x, G.z), float2(D.w, H.y), float2(G.w, G.y));
    float4 h = float4(min(D.w, A.w), min(E.w, B.w), min(E.w, H.w), min(D.w, G.w));
    float4 v = float4(min(E.y, D.y), min(E.y, F.y), min(H.y, I.y), min(H.y, G.y));
    float4 ori = GE(h + float4(D.w, E.w, E.w, D.w), v + float4(E.y, E.y, H.y, H.y));
    float4 hori = LE(h, v) * clr;
    float4 vert = GE(h, v) * clr;
    return (res + 2. * hori + 4. * vert + 8. * ori) / 15.;
}
