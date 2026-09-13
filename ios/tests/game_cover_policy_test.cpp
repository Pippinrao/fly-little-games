#include "GameCoverPolicy.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace flynes::ios;
int main() {
    std::vector<uint8_t> pixels(256 * 240 * 2, 0);
    assert(game_cover_score(pixels.data(),pixels.size(),256,240) < 18);
    for (unsigned y=0;y<240;++y) for(unsigned x=0;x<256;++x) {
        const uint16_t value = (x/8)%2 ? 0xffff : 0;
        pixels[(y*256+x)*2]=value & 255; pixels[(y*256+x)*2+1]=value>>8;
    }
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
    puts("PASS: Android RGB565 quality score, malformed buffer, 120/240/360/480 sampling, improving-quality threshold");
}
