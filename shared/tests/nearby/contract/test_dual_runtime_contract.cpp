#include "flynes/flynes_runtime.h"
#include "flynes/flynes_session.h"

#include "dual/dual_runtime_contract.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

namespace dual = flynes::session::dual;

int failures = 0;

void check(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

/* ------------------------------------------------------- static contract */

/* Frozen window geometry. */
static_assert(dual::kDualPortCountV1 == 4, "DUAL is a four-port unit");
static_assert(dual::kDualRollbackSlotCountV1 == 12, "twelve rollback slots");
static_assert(dual::kDualMaxRollbackFramesV1 == 12, "hard twelve frame ring");
static_assert(dual::kDualPredictionFreezeDepthV1 == 10,
              "prediction freezes at ten frames");
static_assert(dual::kDualGuestLeadFramesV1 == 4, "guest lead is four frames");
static_assert(dual::kDualMaxCatchUpFramesPerQuantumV1 == 3,
              "at most three catch-up frames per quantum");
static_assert(dual::kDualDigestGateFramesV1 == 120, "digest gate is 120 frames");
static_assert(dual::kDualDigestSampleIntervalFramesV1 == 60,
              "digest sampling is every 60 frames");
static_assert(dual::kDualFullPortMaskV1 == 0xFFu, "full eight button mask");

/* Public ABI agreement, so a drift fails the build rather than the wire. */
static_assert(dual::kDualPortCountV1 == FLY_RUNTIME_PORT_COUNT,
              "port count mirrors the runtime ABI");
static_assert(dual::kDualRollbackSlotCountV1 == FLY_RUNTIME_ROLLBACK_SLOTS,
              "rollback slots mirror the runtime ABI");

/* Mode discriminants are frozen and only DUAL is selectable this release. */
static_assert(static_cast<std::uint8_t>(dual::DualModeV1::Dual) == 1,
              "DUAL mode discriminant");
static_assert(static_cast<std::uint8_t>(dual::DualModeV1::HostStream) == 2,
              "HOST_STREAM discriminant reserved");
static_assert((dual::kDualSupportedModeMaskV1 &
               (1u << (static_cast<std::uint8_t>(dual::DualModeV1::HostStream) - 1u))) == 0,
              "STREAM mode must not be selectable in this release");

/* Freeze reason discriminants: they are part of the projection contract. */
static_assert(static_cast<std::uint8_t>(dual::DualFreezeReasonV1::None) == 0,
              "no-freeze discriminant");
static_assert(static_cast<std::uint8_t>(
                  dual::DualFreezeReasonV1::InputWindowExceeded) == 1,
              "window freeze discriminant");
static_assert(static_cast<std::uint8_t>(
                  dual::DualFreezeReasonV1::PredictionDepthExceeded) == 2,
              "prediction freeze discriminant");
static_assert(static_cast<std::uint8_t>(
                  dual::DualFreezeReasonV1::DigestMismatch) == 4,
              "digest freeze discriminant");
static_assert(static_cast<std::uint8_t>(
                  dual::DualFreezeReasonV1::TransportTerminal) == 6,
              "transport freeze discriminant");

/* Exact structural layout. */
static_assert(sizeof(dual::DualInputKeyV1) == 56, "input key is 56 bytes");
static_assert(offsetof(dual::DualInputKeyV1, timeline_epoch) == 32,
              "timeline epoch offset");
static_assert(offsetof(dual::DualInputKeyV1, frame_index) == 40,
              "frame index offset");
static_assert(offsetof(dual::DualInputKeyV1, seat_revision) == 48,
              "seat revision offset");

static_assert(sizeof(dual::DualPortSampleV1) == 16, "port sample is 16 bytes");
static_assert(offsetof(dual::DualPortSampleV1, input_sequence) == 8,
              "port sample sequence offset");

static_assert(sizeof(dual::DualInputBundleV1) == 160, "bundle is 160 bytes");
static_assert(offsetof(dual::DualInputBundleV1, predicted_port_mask) == 60,
              "predicted port mask offset");
static_assert(offsetof(dual::DualInputBundleV1, ports) == 64,
              "ports offset");
static_assert(offsetof(dual::DualInputBundleV1, owner_signing_key_id) == 128,
              "owner signing key offset");

static_assert(sizeof(dual::DualStateDigestV1) == 96, "digest is 96 bytes");
static_assert(sizeof(dual::DualFrameOutcomeV1) == 48, "outcome is 48 bytes");
static_assert(sizeof(dual::DualContentRefV1) == 72, "content ref is 72 bytes");

/* ------------------------------------------------------ callback contract */

static_assert(std::is_abstract_v<dual::DualRuntimePort>,
              "the runtime port is a pure interface");
static_assert(!std::is_copy_constructible_v<dual::DualRuntimePort>,
              "the runtime port is not copyable");
static_assert(std::has_virtual_destructor_v<dual::DualRuntimePort>,
              "the runtime port is polymorphic");

static_assert(std::is_same_v<
                  decltype(std::declval<dual::DualRuntimePort&>().load(
                      std::declval<const dual::DualContentRefV1&>())),
                  fly_session_result_v2>,
              "load signature");
static_assert(std::is_same_v<
                  decltype(std::declval<dual::DualRuntimePort&>().step(
                      std::declval<const dual::DualInputBundleV1&>(),
                      std::declval<dual::DualFrameOutcomeV1*>())),
                  fly_session_result_v2>,
              "step signature");
static_assert(std::is_same_v<
                  decltype(std::declval<dual::DualRuntimePort&>().export_state(
                      std::declval<std::uint8_t*>(),
                      std::declval<std::size_t>(),
                      std::declval<std::size_t*>(),
                      std::declval<std::array<std::uint8_t, 32>*>())),
                  fly_session_result_v2>,
              "export_state signature");
static_assert(std::is_same_v<
                  decltype(std::declval<dual::DualRuntimePort&>().import_state(
                      std::declval<const std::uint8_t*>(),
                      std::declval<std::size_t>())),
                  fly_session_result_v2>,
              "import_state signature");
static_assert(std::is_same_v<
                  decltype(std::declval<dual::DualRuntimePort&>().state_digest(
                      std::declval<std::uint64_t>(),
                      std::declval<dual::DualStateDigestV1*>())),
                  fly_session_result_v2>,
              "state_digest signature");

/* ------------------------------------------------------ input normalization */

void normalization_contract()
{
    constexpr std::uint32_t kUp = 0x10u;
    constexpr std::uint32_t kDown = 0x20u;
    constexpr std::uint32_t kLeft = 0x40u;
    constexpr std::uint32_t kRight = 0x80u;
    constexpr std::uint32_t kA = 0x01u;
    constexpr std::uint32_t kB = 0x02u;

    /* Opposing directions are cleared as a pair, never resolved arbitrarily. */
    check(dual::normalize_dual_port_mask_v1(kUp | kDown) == 0,
          "UP+DOWN normalizes to neutral");
    check(dual::normalize_dual_port_mask_v1(kLeft | kRight) == 0,
          "LEFT+RIGHT normalizes to neutral");
    check(dual::normalize_dual_port_mask_v1(kUp | kDown | kA) == kA,
          "conflict clears only the opposing d-pad pair");
    check(dual::normalize_dual_port_mask_v1(kUp | kA | kB) == (kUp | kA | kB),
          "a non-conflicting mask is preserved");
    check(dual::normalize_dual_port_mask_v1(kUp | kDown | kLeft | kRight) == 0,
          "both conflicts clear together");
    /* 0xFFFF has both d-pad conflicts set, so only A|B|SELECT|START survives. */
    check(dual::normalize_dual_port_mask_v1(0xFFFFu) == 0x0Fu,
          "conflicts are cleared and bits above the pad mask are dropped");
    check(dual::normalize_dual_port_mask_v1(0x0100u | kUp) == kUp,
          "bits above the pad mask are dropped without touching the d-pad");

    /* The normalizer is idempotent. */
    const auto once = dual::normalize_dual_port_mask_v1(kUp | kDown | kLeft | kA);
    check(dual::normalize_dual_port_mask_v1(once) == once,
          "normalization is idempotent");
}

/* ------------------------------------------------------------ key validity */

void key_contract()
{
    dual::DualInputKeyV1 key{};
    check(!dual::dual_input_key_is_valid_v1(key),
          "an all-zero key is invalid");

    key.session_id[0] = 1;
    check(!dual::dual_input_key_is_valid_v1(key),
          "zero timeline epoch is invalid");

    key.timeline_epoch = 1;
    check(!dual::dual_input_key_is_valid_v1(key),
          "zero seat revision is invalid");

    key.seat_revision = 1;
    check(dual::dual_input_key_is_valid_v1(key),
          "nonzero session, epoch and revision is valid");

    /* Structural equality drives idempotency: same key must compare equal. */
    dual::DualInputKeyV1 same = key;
    check(same == key, "identical keys compare equal");
    same.frame_index = 1;
    check(same != key, "a differing frame index makes the keys differ");
    same = key;
    same.branch_id[0] = 1;
    check(same != key, "a differing branch makes the keys differ");
    same = key;
    same.seat_revision = 2;
    check(same != key, "a differing seat revision makes the keys differ");
}

/* ------------------------------------------------------ checked arithmetic */

void checked_arithmetic_contract()
{
    std::uint64_t next = 0;

    check(!dual::checked_dual_next_sequence_v1(0, &next),
          "sequence 0 is never a legal base");
    check(!dual::checked_dual_next_sequence_v1(UINT64_MAX, &next),
          "sequence overflow is rejected, not wrapped");
    check(!dual::checked_dual_next_sequence_v1(3, nullptr),
          "null out pointer is rejected");
    next = 0;
    check(dual::checked_dual_next_sequence_v1(99, &next) && next == 100,
          "sequence advances by exactly one");
    next = 0;
    check(dual::checked_dual_next_sequence_v1(100, &next) && next == 101,
          "sequence 100 to 101 advances by one");

    next = 0;
    check(!dual::checked_dual_next_generation_v1(0, &next),
          "generation 0 is never a legal base");
    check(!dual::checked_dual_next_generation_v1(UINT64_MAX, &next),
          "generation overflow is rejected, not wrapped");
    next = 0;
    check(dual::checked_dual_next_generation_v1(5, &next) && next == 6,
          "generation advances by exactly one");
}

/* ------------------------------------------------------------ window gates */

void window_contract()
{
    std::uint32_t slot = 99;

    check(dual::dual_rollback_slot_v1(20, 20, &slot) && slot == 0,
          "the confirmed frame maps to slot 0");
    slot = 99;
    check(dual::dual_rollback_slot_v1(20, 9, &slot) && slot == 11,
          "eleven frames behind maps to slot 11");
    check(!dual::dual_rollback_slot_v1(20, 8, &slot),
          "twelve frames behind is outside the ring");
    check(!dual::dual_rollback_slot_v1(20, 21, &slot),
          "a future frame cannot be rolled back to");
    check(!dual::dual_rollback_slot_v1(20, 20, nullptr),
          "null out pointer is rejected");

    check(dual::evaluate_dual_window_v1(20, 20, 0) ==
              dual::DualFreezeReasonV1::None,
          "an in-window, unpredicted frame does not freeze");
    check(dual::evaluate_dual_window_v1(20, 20, 9) ==
              dual::DualFreezeReasonV1::None,
          "prediction depth nine is still tolerated");
    check(dual::evaluate_dual_window_v1(20, 20, 10) ==
              dual::DualFreezeReasonV1::PredictionDepthExceeded,
          "prediction depth ten freezes");
    check(dual::evaluate_dual_window_v1(20, 20, 12) ==
              dual::DualFreezeReasonV1::PredictionDepthExceeded,
          "prediction depth above the ring freezes");
    check(dual::evaluate_dual_window_v1(100, 80, 0) ==
              dual::DualFreezeReasonV1::InputWindowExceeded,
          "a target outside the ring freezes");

    dual::DualStateDigestV1 local{};
    dual::DualStateDigestV1 peer{};
    check(dual::evaluate_dual_digest_v1(local, peer) ==
              dual::DualFreezeReasonV1::None,
          "matching digests do not freeze");
    peer.frame[0] = 1;
    check(dual::evaluate_dual_digest_v1(local, peer) ==
              dual::DualFreezeReasonV1::DigestMismatch,
          "a frame digest mismatch freezes");
    peer = local;
    peer.pcm[31] = 1;
    check(dual::evaluate_dual_digest_v1(local, peer) ==
              dual::DualFreezeReasonV1::DigestMismatch,
          "a pcm digest mismatch freezes");
    peer = local;
    check(local == peer, "identical digests compare equal");
}

/* -------------------------------------------------------------- mode gate */

void mode_contract()
{
    check(dual::evaluate_dual_mode_v1(dual::DualModeV1::Dual) ==
              FLY_SESSION_V2_OK,
          "DUAL mode is selectable");
    check(dual::evaluate_dual_mode_v1(dual::DualModeV1::HostStream) ==
              FLY_SESSION_V2_UNAVAILABLE,
          "HOST_STREAM mode is explicitly unavailable");
    check(dual::evaluate_dual_mode_v1(static_cast<dual::DualModeV1>(0)) ==
              FLY_SESSION_V2_UNAVAILABLE,
          "an unknown mode is rejected");
    check(dual::evaluate_dual_mode_v1(static_cast<dual::DualModeV1>(3)) ==
              FLY_SESSION_V2_UNAVAILABLE,
          "a mode outside the enum is rejected");
}

} // namespace

int main()
{
    normalization_contract();
    key_contract();
    checked_arithmetic_contract();
    window_contract();
    mode_contract();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d dual runtime contract checks failed\n",
                     failures);
        return 1;
    }
    std::puts("dual runtime contract tests passed");
    return 0;
}
