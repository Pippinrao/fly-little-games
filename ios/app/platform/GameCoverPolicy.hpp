#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace flynes::ios {
// Literal port of Android cover/FrameQuality.java. Input is compact little-endian RGB565.
inline double game_cover_score(const uint8_t *pixels, size_t length, unsigned width, unsigned height) {
    if (!pixels || !width || !height || width > 4096 || height > 4096
        || length != size_t(width)*height*2) return -std::numeric_limits<double>::infinity();
    const unsigned step_x=std::max(1u,width/32), step_y=std::max(1u,height/30);
    double sum=0, squares=0, transitions=0, count=0;
    for (unsigned y=0;y<height;y+=step_y) {
        int previous=-1;
        for(unsigned x=0;x<width;x+=step_x) {
            const size_t index=(size_t(y)*width+x)*2;
            const unsigned value=pixels[index] | (unsigned(pixels[index+1])<<8);
            const unsigned red=((value>>11)&31)*255/31, green=((value>>5)&63)*255/63, blue=(value&31)*255/31;
            const int luma=(red*77+green*150+blue*29)>>8;
            sum+=luma; squares+=double(luma)*luma;
            if(previous>=0 && std::abs(luma-previous)>=20) transitions+=1;
            previous=luma; ++count;
        }
    }
    const double mean=sum/count;
    return std::sqrt(std::max(0.0,squares/count-mean*mean)) + transitions/count*45.0
        - (mean<8 || mean>247 ? 20.0 : 0.0);
}
struct GameCoverSamples {
    uint64_t first=0;
    unsigned next=0;
    bool started=false;
    bool due(uint64_t sequence) {
        if(!started) { first=sequence; started=true; return false; }
        constexpr uint64_t offsets[]={120,240,360,480};
        if(next>=4 || sequence<first || sequence-first<offsets[next]) return false;
        ++next; return true;
    }
};
struct GameCoverQualityGate {
    double best=-std::numeric_limits<double>::infinity();
    bool accept(double score) {
        if(!std::isfinite(score) || score<18.0 || score<=best+1.0) return false;
        best=score; return true;
    }
};
}
