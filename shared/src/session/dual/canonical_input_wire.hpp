#ifndef FLYNES_SESSION_DUAL_CANONICAL_INPUT_WIRE_HPP
#define FLYNES_SESSION_DUAL_CANONICAL_INPUT_WIRE_HPP

/*
 * Task 10 / step 2: the canonical input unit on the wire.
 *
 * The DUAL seam's input unit is `DualInputBundleV1` (canonical_input.hpp, 160
 * bytes in memory). The frozen wire object for the same unit is
 * `CanonicalInputBundleV1` (message tag 0xFF02, exactly 154 bytes, legal on the
 * State Commit channel only — wire/app_frame.cpp:165), whose byte layout and
 * validator already exist in the schema and in
 * `wire::session_codec::check()`.
 *
 * This translation unit adds only the missing encoder/decoder. It invents no
 * field, no offset and no domain: every byte is written at the offset the frozen
 * schema names, and every decode first passes the frozen validator, so a record
 * this decoder accepts is one the release's own codec already accepts.
 *
 * Three asymmetries are handled explicitly rather than papered over:
 *   - the wire record carries `authority_term`, `mode_generation` and
 *     `batch_sequence`, which the in-memory bundle does not have, so the caller
 *     must supply them (they are the engine's session facts, never the bundle's);
 *   - the wire record carries per-port `source_kind` and `clear_reason`, which
 *     the in-memory bundle does not have either; the encoder derives them from
 *     the bundle's prediction marker unless the caller states them, and the
 *     decoder derives the prediction marker back from `source_kind`;
 *   - the wire record carries no `logical_seat` and no owner signing key, so a
 *     decoder must be told which authenticated seat and which bound owner key
 *     the arriving record belongs to. Nothing on the wire may be trusted to
 *     name its own owner.
 */

#include "canonical_input.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::session::dual {

/* The frozen wire length of CanonicalInputBundleV1. */
inline constexpr std::size_t kDualInputWireBytesV1 = 154;

/* The frozen source_kind / clear_reason enums (schema 0xFF02 port slots). */
enum class DualPortSourceKindV1 : std::uint8_t
{
    RealSample = 1,
    HeldPrime = 2,
    PortClear = 3,
    NeutralUnassigned = 4,
    Predicted = 5,
    TerminalHold = 6,
    HeldSample = 7
};

enum class DualPortClearReasonV1 : std::uint8_t
{
    None = 0,
    Stop = 1,
    Pause = 2,
    PeerDisconnect = 3,
    SeatReassign = 4
};

/* The session facts the wire record needs and the bundle does not carry. */
struct DualInputWireContextV1 final
{
    std::uint64_t authority_term = 0;
    std::uint64_t mode_generation = 0;
    std::uint64_t batch_sequence = 0;
    /* Optional per-port override; when `stated` is false the encoder derives the
     * kind from the bundle's prediction marker (PREDICTED for a predicted port,
     * REAL_SAMPLE otherwise) and the clear reason 0. */
    std::array<DualPortSourceKindV1, kDualPortCountV1> source_kind{};
    std::array<DualPortClearReasonV1, kDualPortCountV1> clear_reason{};
    std::array<bool, kDualPortCountV1> stated{};
};

enum class DualInputWireStatusV1 : std::uint8_t
{
    Ok = 0,
    InvalidArgument = 1,
    InvalidBundle = 2,
    InvalidBytes = 3,
    InvalidContext = 4
};

/*
 * Encode one canonical bundle as the frozen 154-byte record.
 *
 * Every mask is normalized first (UP+DOWN and LEFT+RIGHT cleared together,
 * dual_runtime_contract.hpp: normalize_dual_port_mask_v1) and a mask with bits
 * above the eight real pad bits is refused rather than truncated; the produced
 * bytes are then re-checked with the frozen validator before they are returned,
 * so an encoder bug cannot produce a record the release would reject on arrival.
 */
DualInputWireStatusV1 encode_dual_input_bundle_v1(
    const DualInputBundleV1& bundle, const DualInputWireContextV1& context,
    std::uint8_t out[kDualInputWireBytesV1]) noexcept;

/*
 * Decode a frozen 154-byte record into a canonical bundle.
 *
 * `logical_seat` and `owner_signing_key_id` are the receiving engine's
 * authenticated binding for the arriving record; they are never read from the
 * bytes. The record is refused when the frozen validator rejects it, when the
 * key tuple cannot be formed, or when the decoded bundle is not canonical.
 */
DualInputWireStatusV1 decode_dual_input_bundle_v1(
    const std::uint8_t* bytes, std::size_t size, std::uint8_t logical_seat,
    const std::array<std::uint8_t, 32>& owner_signing_key_id,
    DualInputBundleV1* out) noexcept;

} // namespace flynes::session::dual

#endif
