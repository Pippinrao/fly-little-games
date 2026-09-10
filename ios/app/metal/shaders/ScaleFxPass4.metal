/*
	ScaleFX - Pass 4 (Metal port)
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

float4 loadCrn(float4 x) { return floor(fmod(x * 80. + 0.5, 9.)); }
float4 loadMid(float4 x) { return floor(fmod(x * 8.888888 + 0.055555, 9.)); }

vertex FlyNesVertexOut flynes_scalefx_pass4_vs(uint vid [[vertex_id]]) {
    const float2 positions[4] = {
        float2(-1.0, -1.0), float2(1.0, -1.0),
        float2(-1.0,  1.0), float2(1.0,  1.0)
    };
    FlyNesVertexOut out;
    out.position = float4(positions[vid], 0.0, 1.0);
    out.texcoord = (positions[vid] + 1.0) * 0.5;
    return out;
}

fragment float4 flynes_scalefx_pass4_fs(FlyNesVertexOut in [[stage_in]],
                                        constant ScaleFxUniforms& uniforms [[buffer(0)]],
                                        texture2d<float> packed [[texture(0)]],
                                        texture2d<float> original [[texture(1)]]) {
    constexpr sampler nearest_sampler(address::clamp_to_edge, filter::nearest);
    float4 E = packed.sample(nearest_sampler, in.texcoord);
    float4 crn = loadCrn(E);
    float4 mid = loadMid(E);
    float2 fp = floor(3.0 * fract(in.texcoord * uniforms.textureSize));
    float sp = fp.y == 0. ? (fp.x == 0. ? crn.x : fp.x == 1. ? mid.x : crn.y)
                          : (fp.y == 1. ? (fp.x == 0. ? mid.w : fp.x == 1. ? 0. : mid.y)
                                        : (fp.x == 0. ? crn.w : fp.x == 1. ? mid.z : crn.z));
    float2 res = sp == 0. ? float2(0., 0.) : sp == 1. ? float2(-1., 0.) : sp == 2. ? float2(-2., 0.)
                 : sp == 3. ? float2(1., 0.) : sp == 4. ? float2(2., 0.) : sp == 5. ? float2(0., -1.)
                 : sp == 6. ? float2(0., -2.) : sp == 7. ? float2(0., 1.) : float2(0., 2.);
    return original.sample(nearest_sampler, in.texcoord + res / uniforms.textureSize);
}
