#ifndef FLYNES_SESSION_DUAL_DUAL_RUNTIME_CONTRACT_HPP
#define FLYNES_SESSION_DUAL_DUAL_RUNTIME_CONTRACT_HPP

/*
 * W0 frozen contract: the DUAL internal seam.
 *
 * Provenance. The approved interface design freezes the runtime seam as
 * "RuntimePort" (IF12) plus the frame-targeted input bundle from
 * flynes_runtime.h; the parallel-worktree plan (Task 2, step 4) requires W0 to
 * freeze the concrete C++ names, key tuple, window sizes and gates here so that
 * W2 never invents an approximation and W3 never has to guess.
 *
 * Scope for this release. DUAL only. STREAM is deferred: the mode enum keeps
 * the HOST_STREAM discriminant so the seam does not have to change later, but
 * kDualSupportedModeMaskV1 admits DUAL alone and no code path in this release
 * may select HOST_STREAM. Every unsupported path must fail closed with an
 * explicit reason.
 *
 * What this seam is. The engine stays the only reducer owner. A DualRuntimePort
 * is a per-engine simulation worker that the DUAL scheduler drives serially; it
 * never sees UI touch events, sockets or provider callbacks.
 */

#include "flynes/flynes_runtime.h"
#include "flynes/flynes_session.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::session::dual {

/* --------------------------------------------------------- frozen geometry */

/* Already codified by the public runtime ABI; restated here as the seam's own
 * contract so a drift is a compile error rather than a runtime surprise. */
inline constexpr std::uint32_t kDualPortCountV1 = 4;
inline constexpr std::uint32_t kDualRollbackSlotCountV1 = 12;

/* Input window / prediction gates (approved repair design: keep a 12-frame
 * ring, freeze once prediction reaches 10 frames, never exceed the hard 12). */
inline constexpr std::uint32_t kDualMaxRollbackFramesV1 = 12;
inline constexpr std::uint32_t kDualPredictionFreezeDepthV1 = 10;
inline constexpr std::uint32_t kDualGuestLeadFramesV1 = 4;
inline constexpr std::uint32_t kDualMaxCatchUpFramesPerQuantumV1 = 3;

/* Determinism gate: two isolated runtimes replay from a shared checkpoint and
 * must agree on state/frame/PCM digests. */
inline constexpr std::uint64_t kDualDigestGateFramesV1 = 120;
inline constexpr std::uint64_t kDualDigestSampleIntervalFramesV1 = 60;

/* A NES frame input mask is the full 8-bit pad state. The canonical bundle is
 * always a complete 4-port bundle; a single mask or an implicit port 0 is
 * never a valid DUAL input unit. */
inline constexpr std::uint32_t kDualFullPortMaskV1 = 0xFFu;

static_assert(kDualPortCountV1 == FLY_RUNTIME_PORT_COUNT,
              "DUAL port count must match the public runtime ABI");
static_assert(kDualRollbackSlotCountV1 == FLY_RUNTIME_ROLLBACK_SLOTS,
              "DUAL rollback slots must match the public runtime ABI");
static_assert(kDualPredictionFreezeDepthV1 <= kDualMaxRollbackFramesV1,
              "prediction must freeze at or before the hard ring bound");

/* ----------------------------------------------------------------- enums */

/*
 * Simulation mode. Only DUAL may be selected in this release. The value 2 is
 * reserved for the deferred HOST_STREAM mode so that a later release can add
 * the media data plane without rewriting pairing, identity, QUIC binding or the
 * lobby state machine.
 */
enum class DualModeV1 : std::uint8_t
{
    Dual = 1,
    HostStream = 2
};

inline constexpr std::uint8_t kDualSupportedModeMaskV1 =
    1u << (static_cast<std::uint8_t>(DualModeV1::Dual) - 1u);

enum class DualFreezeReasonV1 : std::uint8_t
{
    None = 0,
    InputWindowExceeded = 1,
    PredictionDepthExceeded = 2,
    CommittedHistoryRewrite = 3,
    DigestMismatch = 4,
    AuthenticatedActivityTimeout = 5,
    TransportTerminal = 6,
    Paused = 7,
    Shutdown = 8,
    SeatOrAuthorityChanged = 9
};

/* ------------------------------------------------------------------ input */

/*
 * Canonical input key. A DUAL input unit is identified by the tuple
 * (session_id, branch_id, timeline_epoch, frame_index, seat_revision) plus the
 * logical seat and the monotonic input sequence carried by the bundle below.
 * Identical key AND identical bytes is idempotent; identical key with different
 * bytes is an authenticated equivocation and must fail closed.
 */
struct DualInputKeyV1 final
{
    std::array<std::uint8_t, 16> session_id{};
    std::array<std::uint8_t, 16> branch_id{};
    std::uint64_t timeline_epoch = 0;
    std::uint64_t frame_index = 0;
    std::uint64_t seat_revision = 0;
};

inline bool operator==(const DualInputKeyV1& lhs,
                       const DualInputKeyV1& rhs) noexcept
{
    return lhs.session_id == rhs.session_id && lhs.branch_id == rhs.branch_id &&
           lhs.timeline_epoch == rhs.timeline_epoch &&
           lhs.frame_index == rhs.frame_index &&
           lhs.seat_revision == rhs.seat_revision;
}

inline bool operator!=(const DualInputKeyV1& lhs,
                       const DualInputKeyV1& rhs) noexcept
{
    return !(lhs == rhs);
}

struct DualPortSampleV1 final
{
    std::uint32_t mask = 0;
    std::uint64_t input_sequence = 0;
};

/*
 * One canonical, frame-targeted input bundle. ports[0..4) is always complete;
 * predicted_port_mask marks the ports whose sample is a prediction rather than
 * a confirmed peer sample, so a prediction can never be mistaken for a
 * confirmed input.
 */
struct DualInputBundleV1 final
{
    DualInputKeyV1 key{};
    std::uint8_t logical_seat = 0;
    std::uint8_t reserved_zero0[3]{};
    std::uint32_t predicted_port_mask = 0;
    DualPortSampleV1 ports[kDualPortCountV1]{};
    std::array<std::uint8_t, 32> owner_signing_key_id{};
};

/* --------------------------------------------------------------- outcomes */

struct DualStateDigestV1 final
{
    std::array<std::uint8_t, 32> state{};
    std::array<std::uint8_t, 32> frame{};
    std::array<std::uint8_t, 32> pcm{};
};

inline bool operator==(const DualStateDigestV1& lhs,
                       const DualStateDigestV1& rhs) noexcept
{
    return lhs.state == rhs.state && lhs.frame == rhs.frame &&
           lhs.pcm == rhs.pcm;
}

inline bool operator!=(const DualStateDigestV1& lhs,
                       const DualStateDigestV1& rhs) noexcept
{
    return !(lhs == rhs);
}

struct DualFrameOutcomeV1 final
{
    std::uint64_t frame_index = 0;
    std::uint64_t applied_input_sequence[kDualPortCountV1]{};
    std::uint32_t honoured_port_mask = 0;
};

/* Content reference. Raw ROM bytes never cross this seam; content transfer has
 * its own low-priority bulk path and its own authorization state machine. */
struct DualContentRefV1 final
{
    std::array<std::uint8_t, 16> session_id{};
    std::array<std::uint8_t, 16> branch_id{};
    std::array<std::uint8_t, 32> content_hash{};
    std::uint64_t timeline_epoch = 0;
};

/* ------------------------------------------------------------------- port */

/*
 * The simulation worker contract. The DUAL scheduler owns one of these and
 * calls it serially; implementations must be deterministic and must never
 * advance the core on a failure. Every method returns the public result code so
 * that a frozen or failed runtime can report the same vocabulary the engine
 * already uses.
 *
 * Freezing rules the implementation must honour (enforced by W2's scheduler,
 * not by this interface):
 *   - after pause, disconnect, transport terminal, authenticated-activity
 *     timeout or shutdown, step() MUST NOT be called again;
 *   - a committed frame's input history is immutable;
 *   - state_digest() covers the committed frame only, never a prediction.
 */
class DualRuntimePort
{
public:
    virtual ~DualRuntimePort() = default;

    DualRuntimePort(const DualRuntimePort&) = delete;
    DualRuntimePort& operator=(const DualRuntimePort&) = delete;

    virtual fly_session_result_v2 load(const DualContentRefV1& content) noexcept = 0;

    virtual fly_session_result_v2 step(const DualInputBundleV1& input,
                                       DualFrameOutcomeV1* out) noexcept = 0;

    virtual fly_session_result_v2 export_state(std::uint8_t* out,
                                               std::size_t capacity,
                                               std::size_t* out_written,
                                               std::array<std::uint8_t, 32>* out_hash) noexcept = 0;

    virtual fly_session_result_v2 import_state(const std::uint8_t* bytes,
                                               std::size_t size) noexcept = 0;

    virtual fly_session_result_v2 state_digest(std::uint64_t frame_index,
                                               DualStateDigestV1* out) noexcept = 0;

protected:
    DualRuntimePort() = default;
};

/* ----------------------------------------------------------- pure helpers */

/*
 * Normalize a port mask before it is ever transmitted. Opposing directions on
 * the same pad are cleared together (UP+DOWN -> neither, LEFT+RIGHT ->
 * neither); the canonical bundle must never carry an impossible d-pad state.
 */
inline std::uint32_t normalize_dual_port_mask_v1(std::uint32_t mask) noexcept
{
    constexpr std::uint32_t kUp = 0x10u;
    constexpr std::uint32_t kDown = 0x20u;
    constexpr std::uint32_t kLeft = 0x40u;
    constexpr std::uint32_t kRight = 0x80u;
    if ((mask & kUp) != 0 && (mask & kDown) != 0)
        mask &= ~(kUp | kDown);
    if ((mask & kLeft) != 0 && (mask & kRight) != 0)
        mask &= ~(kLeft | kRight);
    return mask & kDualFullPortMaskV1;
}

inline bool dual_input_key_is_valid_v1(const DualInputKeyV1& key) noexcept
{
    if (key.timeline_epoch == 0 || key.seat_revision == 0)
        return false;
    for (const auto byte : key.session_id)
        if (byte != 0)
            return true;
    return false;
}

/* Zero is never a legal sequence and saturation is rejected, not wrapped. */
inline bool checked_dual_next_sequence_v1(std::uint64_t current,
                                          std::uint64_t* out_next) noexcept
{
    if (out_next == nullptr || current == 0 || current == UINT64_MAX)
        return false;
    *out_next = current + 1u;
    return true;
}

/* Monotonic generation/epoch bump with the same rules. */
inline bool checked_dual_next_generation_v1(std::uint64_t current,
                                            std::uint64_t* out_next) noexcept
{
    return checked_dual_next_sequence_v1(current, out_next);
}

/*
 * Map a target frame onto the rollback ring. Returns false when the target is
 * not inside the retained window, which is a hard freeze rather than a silent
 * extrapolation.
 */
inline bool dual_rollback_slot_v1(std::uint64_t confirmed_frame,
                                  std::uint64_t target_frame,
                                  std::uint32_t* out_slot) noexcept
{
    if (out_slot == nullptr || target_frame > confirmed_frame)
        return false;
    const std::uint64_t behind = confirmed_frame - target_frame;
    if (behind >= kDualMaxRollbackFramesV1)
        return false;
    *out_slot = static_cast<std::uint32_t>(behind);
    return true;
}

/*
 * The single window gate. A prediction depth of kDualPredictionFreezeDepthV1
 * or more, or a target outside the ring, freezes the session: the first
 * release has no STREAM fallback, so the only legal outcomes are resync,
 * reconnect, end the save, or fail explicitly.
 */
inline DualFreezeReasonV1 evaluate_dual_window_v1(
    std::uint64_t confirmed_frame, std::uint64_t target_frame,
    std::uint32_t prediction_depth) noexcept
{
    std::uint32_t slot = 0;
    if (!dual_rollback_slot_v1(confirmed_frame, target_frame, &slot))
        return DualFreezeReasonV1::InputWindowExceeded;
    if (prediction_depth >= kDualPredictionFreezeDepthV1)
        return DualFreezeReasonV1::PredictionDepthExceeded;
    return DualFreezeReasonV1::None;
}

/* A digest mismatch always freezes at the committed actual frame. */
inline DualFreezeReasonV1 evaluate_dual_digest_v1(
    const DualStateDigestV1& local, const DualStateDigestV1& peer) noexcept
{
    return local == peer ? DualFreezeReasonV1::None
                         : DualFreezeReasonV1::DigestMismatch;
}

/* Only DUAL is selectable in this release; STREAM is explicit unsupported. */
inline fly_session_result_v2 evaluate_dual_mode_v1(DualModeV1 mode) noexcept
{
    return mode == DualModeV1::Dual ? FLY_SESSION_V2_OK
                                    : FLY_SESSION_V2_UNAVAILABLE;
}

/*
 * Required DUAL start-condition preimages for NestopiaUE 1.53.2 / fly_runtime.
 * These identify the required configuration, not a loaded ROM's actual region
 * or AutoSelect controller topology. Providers must verify content support.
 * Identity hashes are domain_hash of these exact little-endian bytes.
 */
inline constexpr std::size_t kDualCanonicalProfileBytesV1 = 20;
inline constexpr std::size_t kDualCanonicalOptionsBytesV1 = 12;

inline void write_u32le_v1(std::uint8_t* bytes, std::uint32_t value) noexcept
{
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8u);
    bytes[2] = static_cast<std::uint8_t>(value >> 16u);
    bytes[3] = static_cast<std::uint8_t>(value >> 24u);
}

inline void write_canonical_dual_profile_bytes_v1(
    std::uint8_t out[kDualCanonicalProfileBytesV1]) noexcept
{
    write_u32le_v1(out + 0, 0u); /* NES_FAVORED_NES_NTSC */
    write_u32le_v1(out + 4, 2u); /* P1+P2 */
    write_u32le_v1(out + 8, 0u); /* NES_PIXFMT_RGB565 */
    write_u32le_v1(out + 12, FLY_RUNTIME_FRAME_WIDTH);
    write_u32le_v1(out + 16, FLY_RUNTIME_FRAME_HEIGHT);
}

inline void write_canonical_dual_options_bytes_v1(
    std::uint8_t out[kDualCanonicalOptionsBytesV1]) noexcept
{
    write_u32le_v1(out + 0, 0u); /* SetRamPowerState(0) */
    write_u32le_v1(out + 4, FLY_RUNTIME_DEFAULT_SAMPLE_RATE);
    write_u32le_v1(out + 8, 0u); /* mono */
}

} // namespace flynes::session::dual

#endif
