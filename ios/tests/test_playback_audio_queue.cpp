#include "../app/audio/PlaybackAudioQueue.hpp"
#include <cassert>
#include <iostream>
int main() {
    flynes::ios::PlaybackAudioQueue queue;
    const auto old = queue.generation();
    for (int i = 0; i < 4; ++i) assert(queue.reserve());
    assert(!queue.reserve());
    queue.finish(old);
    assert(queue.reserve());
    queue.flush();
    assert(queue.count() == 0);
    assert(queue.reserve());
    queue.finish(old); // completion from stopped player cannot consume new audio
    assert(queue.count() == 1);
    queue.finish(queue.generation());
    queue.finish(queue.generation());
    assert(queue.count() == 0);
    std::cout << "iOS bounded PCM queue behavior: PASS\n";
}
