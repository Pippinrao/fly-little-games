#ifndef FLYNES_SESSION_DUAL_CANONICAL_INPUT_HPP
#define FLYNES_SESSION_DUAL_CANONICAL_INPUT_HPP

/*
 * W2 / Task 5: canonical DUAL input.
 *
 * The canonical bundle is the only input unit the DUAL seam carries. It binds
 * the exact input key (session, branch, timeline epoch, frame, global seat
 * revision), the logical seat, the owner session signing key, a monotonic
 * per-port input sequence and the normalized complete four-port mask.
 *
 * Two rules are absolute:
 *   - an impossible d-pad state never leaves the send path: UP+DOWN and
 *     LEFT+RIGHT are cleared together by normalize_dual_port_mask_v1 before the
 *     bundle is built;
 *   - identical key AND identical bytes is idempotent; identical key with
 *     different bytes is authenticated equivocation and must fail closed.
 *
 * Layout, key tuple and pure gates live in the frozen W0 contract
 * (dual_runtime_contract.hpp); this header adds no competing definition.
 */

#include "dual_runtime_contract.hpp"

#include <array>
#include <cstdint>

namespace flynes::session::dual {

/* The four raw per-port samples a caller observes for one frame. */
struct DualPortInputV1 final
{
    std::uint32_t mask = 0;
    std::uint64_t sequence = 0;
};

using DualPortInputArrayV1 = std::array<DualPortInputV1, kDualPortCountV1>;

/* Build rejection vocabulary. Every value is a refusal before encoding. */
enum class DualInputStatusV1 : std::uint8_t
{
    Ok = 0,
    InvalidArgument = 1,
    InvalidKey = 2,
    InvalidSequence = 3,
    InvalidMask = 4
};

/* Structural equality of a canonical bundle. Two bundles are the same input
 * unit only when the key, the logical seat, the owner and every port sample are
 * byte identical; the prediction marker is part of the value. */
inline bool operator==(const DualInputBundleV1& lhs,
                       const DualInputBundleV1& rhs) noexcept
{
    if (lhs.key != rhs.key || lhs.logical_seat != rhs.logical_seat)
        return false;
    if (lhs.owner_signing_key_id != rhs.owner_signing_key_id)
        return false;
    if (lhs.predicted_port_mask != rhs.predicted_port_mask)
        return false;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
        if (lhs.ports[port].mask != rhs.ports[port].mask ||
            lhs.ports[port].input_sequence != rhs.ports[port].input_sequence)
            return false;
    }
    return true;
}

inline bool operator!=(const DualInputBundleV1& lhs,
                       const DualInputBundleV1& rhs) noexcept
{
    return !(lhs == rhs);
}

/*
 * Build the canonical bundle for one frame's local samples. Raw masks are
 * normalized; the sample sequences must be nonzero and strictly increasing by
 * port. The result is always a complete four-port bundle.
 */
DualInputStatusV1 canonical_input_build_v1(
    const DualInputKeyV1& key, std::uint8_t logical_seat,
    const std::array<std::uint8_t, 32>& owner_signing_key_id,
    const DualPortInputArrayV1& samples,
    DualInputBundleV1* out) noexcept;

/* Canonical form check: exact masks, nonzero monotone sequences, valid key and
 * a present owner signing key. A prediction marker is allowed. */
bool canonical_input_is_canonical_v1(const DualInputBundleV1& bundle) noexcept;

/*
 * Idempotency. True only for the same key, the same logical seat, the same
 * owner and byte-identical port state.
 */
bool canonical_input_is_duplicate_v1(const DualInputBundleV1& stored,
                                     const DualInputBundleV1& incoming) noexcept;

/* Deterministic digest of a canonical bundle; a non-canonical bundle yields the
 * digest of the canonical-form predicate plus its own bytes, so it can never
 * collide with a canonical bundle by accident. */
std::array<std::uint8_t, 32> canonical_input_digest_v1(
    const DualInputBundleV1& bundle) noexcept;

} // namespace flynes::session::dual

#endif
