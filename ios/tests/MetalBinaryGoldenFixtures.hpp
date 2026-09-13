#pragma once

// Test-only binary geometry derived from the independent Android golden fixtures.
// Original fixture pixels are packed AABBGGRR. The only permitted mapping is
// ff15171c -> black (0), fff4efe6 -> white (1), for BOTH input and expected.
// MMPX provenance: docs/acceptance/display-quality/mmpx-oracle-manifest.json.
// ScaleFX provenance: libretro/glsl-shaders revision
// 4f4eb801b2dbcaed0a9669a9deec1a098f3623d8, frozen Android GLES reference output.
// SHA-256 below hashes each original fixture as UTF-8 with LF newlines.
// These are binary shape oracles, not full-color RGB565 threshold oracles.

namespace flynes::ios::tests {
struct MetalBinaryGoldenFixture {
    const char* name;
    int width;
    int height;
    const char* input;
    const char* mmpx;
    const char* scalefx;
};

inline constexpr MetalBinaryGoldenFixture metalBinaryGoldens[] = {
    // app/src/androidTest/assets/spatial-golden/oracle/diagonal-3x3.fixture
    // SHA-256: 71b55886249ed25cdd17203b75b4960e1f9adb52b89df856e055fc915548b860
    // app/src/androidTest/assets/spatial-golden/scalefx/diagonal-3x3.fixture
    // SHA-256: 0f4ef154abeff0fd74d891bc4358f35170093a1b8708827bdd4add1bc89ded19
    {"diagonal-3x3", 3, 3,
     "100010001",
     "110000101000011100001110000101000011",
     "111000000111100000111100000001110000000111000000011110000001111000000111000000111"},
    // app/src/androidTest/assets/spatial-golden/oracle/checkerboard-4x3.fixture
    // SHA-256: a64cabd0ce6843d6b6d06a989acff329df6648174925595427d7366fbeaa4f7e
    // app/src/androidTest/assets/spatial-golden/scalefx/checkerboard-4x3.fixture
    // SHA-256: 1c1417f287ab93cf7f9e19693df2820a55576748a030934a9d0264bc53a43824
    {"checkerboard-4x3", 4, 3,
     "010110100101",
     "001100110100110111001100110011000100110100110011",
     "000111000111001111000111001111000111111100111000111000111000110000111000000011000111000111000111001111000111"},
    // app/src/androidTest/assets/spatial-golden/oracle/thin-line-3x5.fixture
    // SHA-256: f863a42a8a598151934f64ce2e304aa02f8773b390ee98db071a86469ea78ac9
    // app/src/androidTest/assets/spatial-golden/scalefx/thin-line-3x5.fixture
    // SHA-256: fa624ccd19b2b69ad2e28aba9e6452a4f01c83712cd7488a4a4ad58c9239b06c
    {"thin-line-3x5", 3, 5,
     "010010010010010",
     "001100001100001100001100001100001100001100001100001100001100",
     "000111000000111000000111000000111000000111000000111000000111000000111000000111000000111000000111000000111000000111000000111000000111000"},
};
} // namespace flynes::ios::tests
