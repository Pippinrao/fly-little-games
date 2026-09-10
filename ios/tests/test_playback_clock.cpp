#include "../app/audio/PlaybackClock.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    flynes::ios::PlaybackClock clock;
    assert(clock.beginTick(100.0));
    assert(clock.frameDue());
    clock.didProduceSamples(798, 48000);
    assert(!clock.frameDue());
    assert(clock.beginTick(100.0 + 1.0 / 120.0));
    assert(!clock.frameDue());
    assert(clock.beginTick(100.0 + 2.0 / 120.0));
    assert(clock.frameDue());

    // The same source clock follows either core timing, independent of display Hz.
    for (double sourceHz : {60.0988, 50.0070}) {
        for (double displayHz : {30.0, 60.0, 120.0}) {
            clock.reset();
            double remainder = 0;
            int frames = 0;
            for (int tick = 0; tick <= static_cast<int>(displayHz * 60); ++tick) {
                clock.beginTick(10.0 + tick / displayHz);
                while (clock.frameDue()) {
                    const double exact = 48000.0 / sourceHz + remainder;
                    const auto samples = static_cast<unsigned>(exact);
                    remainder = exact - samples;
                    clock.didProduceSamples(samples, 48000);
                    ++frames;
                }
            }
            assert(std::abs(frames - sourceHz * 60) <= 2);
        }
    }
    clock.reset();
    clock.beginTick(1.0);
    clock.didProduceSamples(800, 48000);
    clock.beginTick(100.0); // suspended app must not replay 99 seconds
    int catchup = 0;
    while (clock.frameDue()) {
        ++catchup;
        clock.didProduceSamples(800, 48000);
    }
    assert(catchup <= 3);
    clock.reset();
    assert(clock.beginTick(200.0) && clock.frameDue());
    clock.didProduceSamples(0, 48000); // malformed duration cannot cause infinite loop
    assert(!clock.frameDue());
    assert(!clock.beginTick(NAN));
    std::cout << "iOS playback source-clock behavior: PASS\n";
}
