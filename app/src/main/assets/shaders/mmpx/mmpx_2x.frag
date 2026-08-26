precision highp float;

uniform sampler2D uTexture;
uniform vec2 uTextureSize;

vec4 src(vec2 pixel) {
    vec2 clampedPixel = clamp(pixel, vec2(0.0), uTextureSize - vec2(1.0));
    return texture2D(uTexture, (clampedPixel + vec2(0.5)) / uTextureSize);
}

bool eq(vec4 a, vec4 b) { return all(equal(a, b)); }
bool allEq2(vec4 v, vec4 a, vec4 b) { return eq(v, a) && eq(v, b); }
bool allEq3(vec4 v, vec4 a, vec4 b, vec4 c) {
    return eq(v, a) && eq(v, b) && eq(v, c);
}
bool allEq4(vec4 v, vec4 a, vec4 b, vec4 c, vec4 d) {
    return eq(v, a) && eq(v, b) && eq(v, c) && eq(v, d);
}
bool anyEq3(vec4 v, vec4 a, vec4 b, vec4 c) {
    return eq(v, a) || eq(v, b) || eq(v, c);
}
bool noneEq2(vec4 v, vec4 a, vec4 b) { return !eq(v, a) && !eq(v, b); }
bool noneEq4(vec4 v, vec4 a, vec4 b, vec4 c, vec4 d) {
    return !eq(v, a) && !eq(v, b) && !eq(v, c) && !eq(v, d);
}

float luma(vec4 color) {
    return (color.r + color.g + color.b + (1.0 / 255.0)) * (256.0 - 255.0 * color.a);
}

void main() {
    vec2 outputPixel = floor(gl_FragCoord.xy - vec2(0.5));
    vec2 center = floor(outputPixel * 0.5);

    vec4 A = src(center + vec2(-1.0, -1.0));
    vec4 B = src(center + vec2( 0.0, -1.0));
    vec4 C = src(center + vec2( 1.0, -1.0));
    vec4 D = src(center + vec2(-1.0,  0.0));
    vec4 E = src(center);
    vec4 F = src(center + vec2( 1.0,  0.0));
    vec4 G = src(center + vec2(-1.0,  1.0));
    vec4 H = src(center + vec2( 0.0,  1.0));
    vec4 I = src(center + vec2( 1.0,  1.0));
    vec4 Q = src(center + vec2(-2.0,  0.0));
    vec4 R = src(center + vec2( 2.0,  0.0));
    vec4 P = src(center + vec2( 0.0, -2.0));
    vec4 S = src(center + vec2( 0.0,  2.0));

    vec4 J = E;
    vec4 K = E;
    vec4 L = E;
    vec4 M = E;

    if (!(eq(A,E) && eq(B,E) && eq(C,E) && eq(D,E)
            && eq(F,E) && eq(G,E) && eq(H,E) && eq(I,E))) {
        float Bl = luma(B);
        float Dl = luma(D);
        float El = luma(E);
        float Fl = luma(F);
        float Hl = luma(H);

        if (eq(D,B) && !eq(D,H) && !eq(D,F)
                && (El >= Dl || eq(E,A)) && anyEq3(E,A,C,G)
                && (El < Dl || !eq(A,D) || !eq(E,P) || !eq(E,Q))) J = D;
        if (eq(B,F) && !eq(B,D) && !eq(B,H)
                && (El >= Bl || eq(E,C)) && anyEq3(E,A,C,I)
                && (El < Bl || !eq(C,B) || !eq(E,P) || !eq(E,R))) K = B;
        if (eq(H,D) && !eq(H,F) && !eq(H,B)
                && (El >= Hl || eq(E,G)) && anyEq3(E,A,G,I)
                && (El < Hl || !eq(G,H) || !eq(E,S) || !eq(E,Q))) L = H;
        if (eq(F,H) && !eq(F,B) && !eq(F,D)
                && (El >= Fl || eq(E,I)) && anyEq3(E,C,G,I)
                && (El < Fl || !eq(I,H) || !eq(E,R) || !eq(E,S))) M = F;

        if (!eq(E,F) && allEq4(E,C,I,D,Q) && allEq2(F,B,H)
                && !eq(F,src(center + vec2(3.0,0.0)))) { K = F; M = F; }
        if (!eq(E,D) && allEq4(E,A,G,F,R) && allEq2(D,B,H)
                && !eq(D,src(center + vec2(-3.0,0.0)))) { J = D; L = D; }
        if (!eq(E,H) && allEq4(E,G,I,B,P) && allEq2(H,D,F)
                && !eq(H,src(center + vec2(0.0,3.0)))) { L = H; M = H; }
        if (!eq(E,B) && allEq4(E,A,C,H,S) && allEq2(B,D,F)
                && !eq(B,src(center + vec2(0.0,-3.0)))) { J = B; K = B; }

        if (Bl < El && allEq4(E,G,H,I,S) && noneEq4(E,A,D,C,F)) { J = B; K = B; }
        if (Hl < El && allEq4(E,A,B,C,P) && noneEq4(E,D,G,I,F)) { L = H; M = H; }
        if (Fl < El && allEq4(E,A,D,G,Q) && noneEq4(E,B,C,I,H)) { K = F; M = F; }
        if (Dl < El && allEq4(E,C,F,I,R) && noneEq4(E,B,A,G,H)) { J = D; L = D; }

        if (!eq(H,B)) {
            if (!eq(H,A) && !eq(H,E) && !eq(H,C)) {
                if (allEq3(H,G,F,R) && noneEq2(H,D,src(center + vec2(2.0,-1.0)))) L = M;
                if (allEq3(H,I,D,Q) && noneEq2(H,F,src(center + vec2(-2.0,-1.0)))) M = L;
            }
            if (!eq(B,I) && !eq(B,G) && !eq(B,E)) {
                if (allEq3(B,A,F,R) && noneEq2(B,D,src(center + vec2(2.0,1.0)))) J = K;
                if (allEq3(B,C,D,Q) && noneEq2(B,F,src(center + vec2(-2.0,1.0)))) K = J;
            }
        }

        if (!eq(F,D)) {
            if (!eq(D,I) && !eq(D,E) && !eq(D,C)) {
                if (allEq3(D,A,H,S) && noneEq2(D,B,src(center + vec2(1.0,2.0)))) J = L;
                if (allEq3(D,G,B,P) && noneEq2(D,H,src(center + vec2(1.0,-2.0)))) L = J;
            }
            if (!eq(F,E) && !eq(F,A) && !eq(F,G)) {
                if (allEq3(F,C,H,S) && noneEq2(F,B,src(center + vec2(-1.0,2.0)))) K = M;
                if (allEq3(F,I,B,P) && noneEq2(F,H,src(center + vec2(-1.0,-2.0)))) M = K;
            }
        }
    }

    bool right = mod(outputPixel.x, 2.0) >= 1.0;
    bool upper = mod(outputPixel.y, 2.0) >= 1.0;
    gl_FragColor = upper ? (right ? M : L) : (right ? K : J);
}
