#include "../app/audio/PlaybackAudioFocus.hpp"
#include <cassert>
#include <iostream>
int main() {
    using flynes::ios::playbackAudioFocus;
    // DUCK means lowering this game's volume while preserving other sessions.
    assert(playbackAudioFocus(2, false, false).mixWithOthers);
    assert(playbackAudioFocus(2, false, false).gameVolume == 1.0f);
    assert(playbackAudioFocus(2, true, false).gameVolume == 0.2f);
    assert(playbackAudioFocus(2, false, true).gameVolume == 0.2f);
    assert(playbackAudioFocus(2, true, true).gameVolume == 0.2f);
    // Ending the hint is not enough if another mixable session still plays.
    assert(playbackAudioFocus(2, false, true).gameVolume == 0.2f);
    assert(playbackAudioFocus(2, false, false).gameVolume == 1.0f);
    // IGNORE mixes at full volume even with competing audio.
    assert(playbackAudioFocus(3, true, true).mixWithOthers);
    assert(playbackAudioFocus(3, true, true).gameVolume == 1.0f);
    // PAUSE keeps the exclusive session and interruption-driven pause behavior.
    assert(!playbackAudioFocus(1, true, true).mixWithOthers);
    assert(playbackAudioFocus(1, true, true).gameVolume == 1.0f);
    std::cout << "iOS audio focus policy behavior: PASS\n";
}
