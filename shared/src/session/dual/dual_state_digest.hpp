#ifndef FLYNES_SESSION_DUAL_DUAL_STATE_DIGEST_HPP
#define FLYNES_SESSION_DUAL_DUAL_STATE_DIGEST_HPP

/*
 * W2 / Task 6: DUAL consistency digests.
 *
 * The digest gate compares two isolated runtimes over the same committed frame.
 * A digest covers the committed state only, never a prediction, and the
 * comparison always freezes at the committed actual frame; there is no
 * automatic fallback because STREAM is not implemented in this release.
 */

#include "dual_runtime_contract.hpp"

#include <array>
#include <cstdint>

namespace flynes::session::dual {

/*
 * Recompute the canonical state digest for one committed frame from the port's
 * exported state bytes. Deterministic for equal bytes and frame index.
 */
DualStateDigestV1 dual_state_digest_v1(const std::uint8_t* state_bytes,
                                       std::size_t state_size,
                                       std::uint64_t frame_index) noexcept;

/*
 * Build the frame digest half of the gate: a pure function of the frame index
 * and the canonical input bundle the frame was stepped with.
 */
std::array<std::uint8_t, 32> dual_frame_digest_v1(
    std::uint64_t frame_index, const DualInputBundleV1& input) noexcept;

/*
 * Compare two digests for the same frame. Any differing component  -  state,
 * frame or PCM  -  is a mismatch and freezes. Returns the reason.
 */
DualFreezeReasonV1 dual_digest_gate_v1(const DualStateDigestV1& local,
                                       const DualStateDigestV1& peer) noexcept;

/* True exactly when all three components are byte identical. */
bool dual_digest_equal_v1(const DualStateDigestV1& lhs,
                          const DualStateDigestV1& rhs) noexcept;

} // namespace flynes::session::dual

#endif
