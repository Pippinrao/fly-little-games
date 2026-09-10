/*
	ScaleFX - Pass 3 (Metal port)
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

constant float SFX_SCN = 1.0;

bool4 loadCorn(float4 x) { return bool4(floor(fmod(x * 15. + 0.5, 2.))); }
bool4 loadHori(float4 x) { return bool4(floor(fmod(x * 7.5 + 0.25, 2.))); }
bool4 loadVert(float4 x) { return bool4(floor(fmod(x * 3.75 + 0.125, 2.))); }
bool4 loadOr(float4 x) { return bool4(floor(fmod(x * 1.875 + 0.0625, 2.))); }

vertex FlyNesVertexOut flynes_scalefx_pass3_vs(uint vid [[vertex_id]]) {
    const float2 positions[4] = {
        float2(-1.0, -1.0), float2(1.0, -1.0),
        float2(-1.0,  1.0), float2(1.0,  1.0)
    };
    FlyNesVertexOut out;
    out.position = float4(positions[vid], 0.0, 1.0);
    out.texcoord = (positions[vid] + 1.0) * 0.5;
    return out;
}

fragment float4 flynes_scalefx_pass3_fs(FlyNesVertexOut in [[stage_in]],
                                        constant ScaleFxUniforms& uniforms [[buffer(0)]],
                                        texture2d<float> tex [[texture(0)]]) {
    constexpr sampler nearest_sampler(address::clamp_to_edge, filter::nearest);
    float2 t = 1.0 / uniforms.textureSize;
    float4 E = tex.sample(nearest_sampler, in.texcoord);
    float4 D = tex.sample(nearest_sampler, in.texcoord + float2(-t.x, 0.0));
    float4 D0 = tex.sample(nearest_sampler, in.texcoord + float2(-2.0 * t.x, 0.0));
    float4 D1 = tex.sample(nearest_sampler, in.texcoord + float2(-3.0 * t.x, 0.0));
    float4 F = tex.sample(nearest_sampler, in.texcoord + float2(t.x, 0.0));
    float4 F0 = tex.sample(nearest_sampler, in.texcoord + float2(2.0 * t.x, 0.0));
    float4 F1 = tex.sample(nearest_sampler, in.texcoord + float2(3.0 * t.x, 0.0));
    float4 B = tex.sample(nearest_sampler, in.texcoord + float2(0.0, -t.y));
    float4 B0 = tex.sample(nearest_sampler, in.texcoord + float2(0.0, -2.0 * t.y));
    float4 B1 = tex.sample(nearest_sampler, in.texcoord + float2(0.0, -3.0 * t.y));
    float4 H = tex.sample(nearest_sampler, in.texcoord + float2(0.0, t.y));
    float4 H0 = tex.sample(nearest_sampler, in.texcoord + float2(0.0, 2.0 * t.y));
    float4 H1 = tex.sample(nearest_sampler, in.texcoord + float2(0.0, 3.0 * t.y));

    bool4 Ec = loadCorn(E), Eh = loadHori(E), Ev = loadVert(E), Eo = loadOr(E);
    bool4 Dc = loadCorn(D), Dh = loadHori(D), Do = loadOr(D), D0c = loadCorn(D0), D0h = loadHori(D0), D1h = loadHori(D1);
    bool4 Fc = loadCorn(F), Fh = loadHori(F), Fo = loadOr(F), F0c = loadCorn(F0), F0h = loadHori(F0), F1h = loadHori(F1);
    bool4 Bc = loadCorn(B), Bv = loadVert(B), Bo = loadOr(B), B0c = loadCorn(B0), B0v = loadVert(B0), B1v = loadVert(B1);
    bool4 Hc = loadCorn(H), Hv = loadVert(H), Ho = loadOr(H), H0c = loadCorn(H0), H0v = loadVert(H0), H1v = loadVert(H1);

    bool lvl1x = Ec.x && (Dc.z || Bc.z || SFX_SCN == 1.);
    bool lvl1y = Ec.y && (Fc.w || Bc.w || SFX_SCN == 1.);
    bool lvl1z = Ec.z && (Fc.x || Hc.x || SFX_SCN == 1.);
    bool lvl1w = Ec.w && (Dc.y || Hc.y || SFX_SCN == 1.);

    float2 lvl2x = float2((Ec.x && Eh.y) && Dc.z, (Ec.y && Eh.x) && Fc.w);
    float2 lvl2y = float2((Ec.y && Ev.z) && Bc.w, (Ec.z && Ev.y) && Hc.x);
    float2 lvl2z = float2((Ec.w && Eh.z) && Dc.y, (Ec.z && Eh.w) && Fc.x);
    float2 lvl2w = float2((Ec.x && Ev.w) && Bc.z, (Ec.w && Ev.x) && Hc.y);

    float4 crn;
    crn.x = (lvl1x && Eo.x) ? 5. : lvl1x ? 1. : 0.;
    crn.y = (lvl1y && Eo.y) ? 5. : lvl1y ? 3. : 0.;
    crn.z = (lvl1z && Eo.z) ? 7. : lvl1z ? 3. : 0.;
    crn.w = (lvl1w && Eo.w) ? 7. : lvl1w ? 1. : 0.;
    float4 mid;
    mid.x = lvl2x.x ? (Eo.x ? 5. : 1.) : lvl2x.y ? 3. : 0.;
    mid.y = lvl2y.x ? 5. : lvl2y.y ? 7. : 0.;
    mid.z = lvl2z.x ? 1. : lvl2z.y ? 3. : 0.;
    mid.w = lvl2w.x ? 5. : lvl2w.y ? 7. : 0.;
    (void)D0c; (void)D0h; (void)D1h; (void)F0c; (void)F0h; (void)F1h;
    (void)B0c; (void)B0v; (void)B1v; (void)H0c; (void)H0v; (void)H1v;
    (void)Dh; (void)Do; (void)Fh; (void)Fo; (void)Bv; (void)Bo; (void)Hv; (void)Ho;
    return (crn + 9. * mid) / 80.;
}
