#pragma once
namespace flynes::ios {
struct PlaybackAudioFocus {
    bool mixWithOthers;
    float gameVolume;
};
inline PlaybackAudioFocus playbackAudioFocus(unsigned policy, bool secondaryHint, bool otherAudioPlaying) {
    return {policy == 2 || policy == 3,
            policy == 2 && (secondaryHint || otherAudioPlaying) ? 0.2f : 1.0f};
}
}
