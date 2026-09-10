/*
 * MMPX 2x Metal port of FlyNES GLSL ES 2.0 (app/src/main/assets/shaders/mmpx/mmpx_2x.frag).
 * Upstream reference: Morgan McGuire and Mara Gagiu, 2020, MIT License.
 * Paper: https://www.jcgt.org/published/0010/02/04/paper.pdf
 */
#include <metal_stdlib>
using namespace metal;

struct FlyNesVertexOut {
    float4 position [[position]];
    float2 texcoord;
};

struct MmpxUniforms {
    float2 textureSize;
};

bool eq(float4 a, float4 b) { return all(a == b); }
bool allEq2(float4 v, float4 a, float4 b) { return eq(v, a) && eq(v, b); }
bool allEq3(float4 v, float4 a, float4 b, float4 c) {
    return eq(v, a) && eq(v, b) && eq(v, c);
}
bool allEq4(float4 v, float4 a, float4 b, float4 c, float4 d) {
    return eq(v, a) && eq(v, b) && eq(v, c) && eq(v, d);
}
bool anyEq3(float4 v, float4 a, float4 b, float4 c) {
    return eq(v, a) || eq(v, b) || eq(v, c);
}
bool noneEq2(float4 v, float4 a, float4 b) { return !eq(v, a) && !eq(v, b); }
bool noneEq4(float4 v, float4 a, float4 b, float4 c, float4 d) {
    return !eq(v, a) && !eq(v, b) && !eq(v, c) && !eq(v, d);
}

float luma(float4 color) {
    return (color.r + color.g + color.b + (1.0 / 255.0)) * (256.0 - 255.0 * color.a);
}

float4 src(texture2d<float> tex, float2 textureSize, float2 pixel) {
    constexpr sampler nearest_sampler(address::clamp_to_edge, filter::nearest);
    float2 clampedPixel = clamp(pixel, float2(0.0), textureSize - float2(1.0));
    float4 color = tex.sample(nearest_sampler, (clampedPixel + float2(0.5)) / textureSize);
    // The upload uses RGBA8 for Simulator support; recover the original
    // normalized RGB565 channels before equality and luma comparisons.
    color.rgb = round(color.rgb * float3(31., 63., 31.)) / float3(31., 63., 31.);
    return color;
}

vertex FlyNesVertexOut flynes_mmpx_vs(uint vid [[vertex_id]]) {
    const float2 positions[4] = {
        float2(-1.0, -1.0), float2(1.0, -1.0),
        float2(-1.0,  1.0), float2(1.0,  1.0)
    };
    FlyNesVertexOut out;
    out.position = float4(positions[vid], 0.0, 1.0);
    out.texcoord = float2((positions[vid].x + 1.0) * 0.5, (1.0 - positions[vid].y) * 0.5);
    return out;
}

fragment float4 flynes_mmpx_fs(FlyNesVertexOut in [[stage_in]],
                               constant MmpxUniforms& uniforms [[buffer(0)]],
                               texture2d<float> tex [[texture(0)]]) {
    float2 outputPixel = floor(in.position.xy - float2(0.5));
    float2 center = floor(outputPixel * 0.5);
    float2 textureSize = uniforms.textureSize;

    float4 A = src(tex, textureSize, center + float2(-1.0, -1.0));
    float4 B = src(tex, textureSize, center + float2( 0.0, -1.0));
    float4 C = src(tex, textureSize, center + float2( 1.0, -1.0));
    float4 D = src(tex, textureSize, center + float2(-1.0,  0.0));
    float4 E = src(tex, textureSize, center);
    float4 F = src(tex, textureSize, center + float2( 1.0,  0.0));
    float4 G = src(tex, textureSize, center + float2(-1.0,  1.0));
    float4 H = src(tex, textureSize, center + float2( 0.0,  1.0));
    float4 I = src(tex, textureSize, center + float2( 1.0,  1.0));
    float4 Q = src(tex, textureSize, center + float2(-2.0,  0.0));
    float4 R = src(tex, textureSize, center + float2( 2.0,  0.0));
    float4 P = src(tex, textureSize, center + float2( 0.0, -2.0));
    float4 S = src(tex, textureSize, center + float2( 0.0,  2.0));

    float4 J = E;
    float4 K = E;
    float4 L = E;
    float4 M = E;

    if (!(eq(A, E) && eq(B, E) && eq(C, E) && eq(D, E)
          && eq(F, E) && eq(G, E) && eq(H, E) && eq(I, E))) {
        float Bl = luma(B);
        float Dl = luma(D);
        float El = luma(E);
        float Fl = luma(F);
        float Hl = luma(H);

        if (eq(D, B) && !eq(D, H) && !eq(D, F)
            && (El >= Dl || eq(E, A)) && anyEq3(E, A, C, G)
            && (El < Dl || !eq(A, D) || !eq(E, P) || !eq(E, Q))) J = D;
        if (eq(B, F) && !eq(B, D) && !eq(B, H)
            && (El >= Bl || eq(E, C)) && anyEq3(E, A, C, I)
            && (El < Bl || !eq(C, B) || !eq(E, P) || !eq(E, R))) K = B;
        if (eq(H, D) && !eq(H, F) && !eq(H, B)
            && (El >= Hl || eq(E, G)) && anyEq3(E, A, G, I)
            && (El < Hl || !eq(G, H) || !eq(E, S) || !eq(E, Q))) L = H;
        if (eq(F, H) && !eq(F, B) && !eq(F, D)
            && (El >= Fl || eq(E, I)) && anyEq3(E, C, G, I)
            && (El < Fl || !eq(I, H) || !eq(E, R) || !eq(E, S))) M = F;

        if (!eq(E, F) && allEq4(E, C, I, D, Q) && allEq2(F, B, H)
            && !eq(F, src(tex, textureSize, center + float2(3.0, 0.0)))) { K = F; M = F; }
        if (!eq(E, D) && allEq4(E, A, G, F, R) && allEq2(D, B, H)
            && !eq(D, src(tex, textureSize, center + float2(-3.0, 0.0)))) { J = D; L = D; }
        if (!eq(E, H) && allEq4(E, G, I, B, P) && allEq2(H, D, F)
            && !eq(H, src(tex, textureSize, center + float2(0.0, 3.0)))) { L = H; M = H; }
        if (!eq(E, B) && allEq4(E, A, C, H, S) && allEq2(B, D, F)
            && !eq(B, src(tex, textureSize, center + float2(0.0, -3.0)))) { J = B; K = B; }

        if (Bl < El && allEq4(E, G, H, I, S) && noneEq4(E, A, D, C, F)) { J = B; K = B; }
        if (Hl < El && allEq4(E, A, B, C, P) && noneEq4(E, D, G, I, F)) { L = H; M = H; }
        if (Fl < El && allEq4(E, A, D, G, Q) && noneEq4(E, B, C, I, H)) { K = F; M = F; }
        if (Dl < El && allEq4(E, C, F, I, R) && noneEq4(E, B, A, G, H)) { J = D; L = D; }

        if (!eq(H, B)) {
            if (!eq(H, A) && !eq(H, E) && !eq(H, C)) {
                if (allEq3(H, G, F, R) && noneEq2(H, D, src(tex, textureSize, center + float2(2.0, -1.0)))) L = M;
                if (allEq3(H, I, D, Q) && noneEq2(H, F, src(tex, textureSize, center + float2(-2.0, -1.0)))) M = L;
            }
            if (!eq(B, I) && !eq(B, G) && !eq(B, E)) {
                if (allEq3(B, A, F, R) && noneEq2(B, D, src(tex, textureSize, center + float2(2.0, 1.0)))) J = K;
                if (allEq3(B, C, D, Q) && noneEq2(B, F, src(tex, textureSize, center + float2(-2.0, 1.0)))) K = J;
            }
        }

        if (!eq(F, D)) {
            if (!eq(D, I) && !eq(D, E) && !eq(D, C)) {
                if (allEq3(D, A, H, S) && noneEq2(D, B, src(tex, textureSize, center + float2(1.0, 2.0)))) J = L;
                if (allEq3(D, G, B, P) && noneEq2(D, H, src(tex, textureSize, center + float2(1.0, -2.0)))) L = J;
            }
            if (!eq(F, E) && !eq(F, A) && !eq(F, G)) {
                if (allEq3(F, C, H, S) && noneEq2(F, B, src(tex, textureSize, center + float2(-1.0, 2.0)))) K = M;
                if (allEq3(F, I, B, P) && noneEq2(F, H, src(tex, textureSize, center + float2(-1.0, -2.0)))) M = K;
            }
        }
    }

    bool right = fmod(outputPixel.x, 2.0) >= 1.0;
    bool upper = fmod(outputPixel.y, 2.0) >= 1.0;
    return upper ? (right ? M : L) : (right ? K : J);
}
