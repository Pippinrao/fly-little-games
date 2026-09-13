#pragma once
// Orchestration half of the Android cover pipeline. `GameCoverPolicy.hpp` holds the
// literal sampling offsets and quality gate; this type applies them once per play
// session, exactly like Android `CoverCaptureCoordinator`: the first observed frame
// only starts the session clock, the best score restarts on every new session, and a
// persisted cover is never compared against.
#include "GameCoverPolicy.hpp"

namespace flynes::ios {

class CoverCaptureSession final {
public:
    /// Observe every produced native game frame, including frames skipped by display
    /// presentation. Returns true when this frame is one of the 120/240/360/480
    /// samples and should be copied and scored.
    bool note_frame(uint64_t sequence) { return samples_.due(sequence); }

    /// Score a copied sample. Returns true when it is good enough to persist.
    bool consider(double score) { return gate_.accept(score); }

    bool started() const { return samples_.started; }
    unsigned sampled() const { return samples_.next; }

private:
    GameCoverSamples samples_{};
    GameCoverQualityGate gate_{};
};

} // namespace flynes::ios
