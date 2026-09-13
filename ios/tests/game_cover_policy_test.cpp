#include "GameCoverPolicy.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace flynes::ios;

static void put_pixel(std::vector<uint8_t> &pixels, unsigned x, unsigned y, uint16_t value)
{
    pixels[(y * 256 + x) * 2] = uint8_t(value & 255);
    pixels[(y * 256 + x) * 2 + 1] = uint8_t(value >> 8);
}

static uint16_t get_pixel(const std::vector<uint8_t> &pixels, unsigned x, unsigned y)
{
    return uint16_t(pixels[(y * 256 + x) * 2] | (unsigned(pixels[(y * 256 + x) * 2 + 1]) << 8));
}

// Thirteen rows of 8x8 white glyphs on black - the opening credit screen of every
// bundled homebrew title. The glyph strokes are drawn on odd columns, which is
// where the 32x30 point grid (every 8th pixel, starting at 0) never looks.
static std::vector<uint8_t> credit_text_frame()
{
    std::vector<uint8_t> pixels(256 * 240 * 2, 0);
    for (unsigned line = 0; line < 13; ++line) {
        const unsigned top = 21u + line * 16u;
        for (unsigned y = top; y < top + 8; ++y)
            for (unsigned x = 20; x < 236; ++x)
                if (x % 2 == 1) put_pixel(pixels, x, y, 0xffff);
    }
    return pixels;
}

int main() {
    std::vector<uint8_t> pixels(256 * 240 * 2, 0);
    assert(game_cover_score(pixels.data(),pixels.size(),256,240) < 18);

    std::vector<uint8_t> credits = credit_text_frame();
    // Proof that this is the case the old metric could not see: at point-sampling
    // resolution the frame is indistinguishable from a black screen, which is why
    // a real Thwaite title screen scored -11.68 and every bundled game was
    // refused a cover.
    unsigned visible = 0;
    for (unsigned sample = 0; sample * 8u < 240u; ++sample)
        for (unsigned x = 0; x < 256; x += 8)
            if (get_pixel(credits, x, sample * 8u) != 0) ++visible;
    assert(visible == 0);
    const double credits_score = game_cover_score(credits.data(), credits.size(), 256, 240);
    assert(credits_score >= 18);
    // A blank screen is still a blank screen: averaging must not make black pass.
    assert(game_cover_score(pixels.data(),pixels.size(),256,240) < 18);

    for (unsigned y=0;y<240;++y) for(unsigned x=0;x<256;++x) {
        const uint16_t value = (x/8)%2 ? 0xffff : 0;
        pixels[(y*256+x)*2]=value & 255; pixels[(y*256+x)*2+1]=value>>8;
    }
    // Grid cells average their pixels, so a coarse cell is fully white or fully
    // black over that stripe pitch and a one-column shift still lands the same
    // split of cells on each side.
    const double expected = 127.5 + (31.0/32.0)*45.0;
    assert(std::abs(game_cover_score(pixels.data(),pixels.size(),256,240)-expected)<1e-8);
    assert(game_cover_score(pixels.data(),10,256,240) < 18);

    GameCoverSamples samples;
    assert(!samples.due(1000)); assert(!samples.due(1119)); assert(samples.due(1120));
    assert(!samples.due(1120)); assert(samples.due(1240)); assert(samples.due(1360));
    assert(samples.due(1480)); assert(!samples.due(5000));
    GameCoverQualityGate gate;
    assert(!gate.accept(17.999)); assert(gate.accept(18)); assert(!gate.accept(19));
    assert(gate.accept(19.001)); assert(!gate.accept(NAN));
    printf("PASS: credit text scores %.3f where point sampling sees nothing, "
           "stripe frame %.3f, malformed buffer, 120/240/360/480 sampling, improving threshold\n",
           credits_score, expected);
}
