#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace flynes::ios {
// Literal port of Android cover/FrameQuality.java. Input is compact little-endian RGB565.
//
// Each cell of the 32x30 grid averages its pixels instead of sampling one. Homebrew
// titles open on a black screen carrying a few lines of one-pixel white credit text;
// point sampling stepped over those strokes, scored the frame as an empty black
// screen, and refused every bundled game a cover. Averaging keeps thin text energy
// while a blank screen still averages to a flat, rejected grid.
inline int game_cover_luma(const uint8_t *pixels, unsigned width, unsigned x, unsigned y) {
    const size_t index=(size_t(y)*width+x)*2;
    const unsigned value=pixels[index] | (unsigned(pixels[index+1])<<8);
    const unsigned red=((value>>11)&31)*255/31, green=((value>>5)&63)*255/63, blue=(value&31)*255/31;
    return (red*77+green*150+blue*29)>>8;
}
inline double game_cover_block_luma(const uint8_t *pixels, unsigned width, unsigned height,
                                    unsigned originX, unsigned originY,
                                    unsigned blockWidth, unsigned blockHeight) {
    const unsigned endY=std::min(height, originY+blockHeight);
    const unsigned endX=std::min(width, originX+blockWidth);
    double total=0; unsigned samples=0;
    for(unsigned y=originY;y<endY;y++)
        for(unsigned x=originX;x<endX;x++) { total+=game_cover_luma(pixels,width,x,y); ++samples; }
    return samples==0 ? 0.0 : total/samples;
}
inline double game_cover_score(const uint8_t *pixels, size_t length, unsigned width, unsigned height) {
    if (!pixels || !width || !height || width > 4096 || height > 4096
        || length != size_t(width)*height*2) return -std::numeric_limits<double>::infinity();
    constexpr unsigned columns=32, rows=30;
    const unsigned blockWidth=std::max(1u,width/columns), blockHeight=std::max(1u,height/rows);
    double sum=0, squares=0, transitions=0, count=0;
    for (unsigned blockY=0;blockY<rows;blockY++) {
        double previous=-1.0;
        for(unsigned blockX=0;blockX<columns;blockX++) {
            const double luma=game_cover_block_luma(pixels,width,height,
                blockX*blockWidth, blockY*blockHeight, blockWidth, blockHeight);
            sum+=luma; squares+=luma*luma;
            if(previous>=0.0 && std::abs(luma-previous)>=20.0) transitions+=1;
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
