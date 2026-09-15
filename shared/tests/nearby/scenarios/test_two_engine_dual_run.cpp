/*
 * W3 scenario: DUAL run over two public engines.
 *
 * Status against the current baseline (e8703e4 + W0 QUIC wiring e22cbff):
 *   * LINK_HELLO / LINK_READY are contracts only and are NOT wired into
 *     SessionEngine (W0 Task 9), so no engine can reach CONNECTED_LOBBY and no
 *     DUAL run can legally be started between two public engines;
 *   * the DUAL run scheduler itself is W2's Task 5/6 deliverable
 *     (shared/src/session/dual/dual_run_scheduler.*), which does not exist in
 *     this worktree yet.
 *
 * This file therefore asserts exactly the DUAL-facing properties that are real
 * today -- every unsupported path fails closed with an explicit reason, the
 * frozen geometry is honoured, and the mode gate refuses HOST_STREAM -- and
 * carries the full 600-frame scenario behind kDualRunEnabledV1 so that enabling
 * it after W0/W2 land is a single, reviewable edit. It does NOT fabricate a
 * dual run.
 */

#include "../harness/two_engine_fixture.hpp"

#include "dual/dual_runtime_contract.hpp"

#include <cstdio>

namespace flynes::tests::nearby {
namespace {

using flynes::session::dual::DualFreezeReasonV1;
using flynes::session::dual::DualModeV1;
using flynes::session::dual::kDualFullPortMaskV1;
using flynes::session::dual::kDualGuestLeadFramesV1;
using flynes::session::dual::kDualMaxRollbackFramesV1;
using flynes::session::dual::kDualPortCountV1;
using flynes::session::dual::kDualPredictionFreezeDepthV1;
using flynes::session::dual::kDualRollbackSlotCountV1;
using flynes::session::dual::evaluate_dual_mode_v1;
using flynes::session::dual::evaluate_dual_window_v1;
using flynes::session::dual::normalize_dual_port_mask_v1;

/*
 * Enabled only when both of these are true:
 *   * W0 Task 9 has wired LINK_HELLO / LINK_READY into SessionEngine, so two
 *     public engines can reach CONNECTED_LOBBY;
 *   * W2's dual_run_scheduler is available to drive a 12-slot rollback run.
 */
inline constexpr bool kDualRunEnabledV1 = false;

/* ------------------------------------------------------- reachable today */

void only_dual_mode_is_selectable()
{
    check(evaluate_dual_mode_v1(DualModeV1::Dual) == FLY_SESSION_V2_OK,
          "DUAL is the only selectable runtime mode");
    check(evaluate_dual_mode_v1(DualModeV1::HostStream) ==
              FLY_SESSION_V2_UNAVAILABLE,
          "HOST_STREAM is refused with an explicit unsupported result");
}

void frozen_dual_geometry_holds()
{
    check(kDualPortCountV1 == 4, "DUAL has exactly four canonical ports");
    check(kDualRollbackSlotCountV1 == 12, "DUAL keeps exactly twelve slots");
    check(kDualMaxRollbackFramesV1 == 12,
          "twelve frames is the hard rollback bound");
    check(kDualPredictionFreezeDepthV1 == 10,
          "prediction freezes at ten frames, before the hard bound");
    check(kDualGuestLeadFramesV1 < kDualPredictionFreezeDepthV1,
          "the guest lead stays inside the prediction budget");
    check(kDualFullPortMaskV1 == 0xFFu,
          "a canonical port mask is the full eight-bit pad state");
}

void impossible_dpad_states_are_normalized_before_the_wire()
{
    /* UP|DOWN and LEFT|RIGHT must never survive into a canonical bundle. */
    constexpr std::uint32_t kUp = 0x10u;
    constexpr std::uint32_t kDown = 0x20u;
    constexpr std::uint32_t kLeft = 0x40u;
    constexpr std::uint32_t kRight = 0x80u;
    check(normalize_dual_port_mask_v1(kUp | kDown) == 0,
          "opposing vertical directions cancel out");
    check(normalize_dual_port_mask_v1(kLeft | kRight) == 0,
          "opposing horizontal directions cancel out");
    const std::uint32_t mixed = normalize_dual_port_mask_v1(
        kUp | kDown | kLeft | kRight | 0x01u | 0x8000u);
    check(mixed == 0x01u,
          "normalization keeps held buttons and masks to eight bits");
}

void the_window_gate_freezes_instead_of_extrapolating()
{
    /* On time: inside the ring and inside the prediction budget. */
    check(evaluate_dual_window_v1(100, 100, 0) == DualFreezeReasonV1::None,
          "a confirmed, unpredicted frame is inside the window");
    check(evaluate_dual_window_v1(100, 89, 0) == DualFreezeReasonV1::None,
          "eleven frames behind is still inside the twelve-frame ring");
    /* One frame past the ring: a hard freeze, never a silent extrapolation. */
    check(evaluate_dual_window_v1(100, 88, 0) ==
              DualFreezeReasonV1::InputWindowExceeded,
          "a target outside the retained window freezes the session");
    /* Future targets are refused outright. */
    check(evaluate_dual_window_v1(100, 101, 0) ==
              DualFreezeReasonV1::InputWindowExceeded,
          "a target ahead of the confirmed frame freezes the session");
    /* Prediction depth at the frozen threshold. */
    check(evaluate_dual_window_v1(100, 92, kDualPredictionFreezeDepthV1) ==
              DualFreezeReasonV1::PredictionDepthExceeded,
          "prediction at the freeze depth freezes the session");
    check(evaluate_dual_window_v1(100, 92,
                                  kDualPredictionFreezeDepthV1 - 1u) ==
              DualFreezeReasonV1::None,
          "prediction one frame below the freeze depth still runs");
}

void two_engines_cannot_reach_a_dual_run_yet()
{
    TwoEngineFixtureV1 pair;
    pair.bring_up();

    const auto left = pair.left().snapshot();
    const auto right = pair.right().snapshot();
    check(left.link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
              right.link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
          "no DUAL run starts while neither engine can reach the lobby");
    check(left.game_state == right.game_state,
          "both engines publish the same pre-run game state");

    pair.release_engines();
    pair.check_resources_balanced("dual run refusal");
}

/* ------------------------------------------------------- PENDING (W0 + W2) */

/*
 * When kDualRunEnabledV1 flips to true, this function must become a real
 * 600-frame DUAL run and assert:
 *
 *   1. both engines confirm authority/P1/P2/DUAL through the public ABI;
 *   2. at least 600 frames are committed on both sides from a deterministic
 *      runtime fixture (no ROM dependency, no wall clock, no randomness);
 *   3. P1 and P2 each produce at least 100 distinguishable input edges;
 *   4. the final frame index, state digest and input root are identical on both
 *      engines;
 *   5. every fault below ends in the documented freeze/recover/end state and
 *      never in a silent single-player fallback and never in STREAM:
 *        duplicate/reordered/lost input, arrival within 12 frames, arrival past
 *        the window, digest mismatch, 300 ms without authenticated activity,
 *        a 2 s link interruption, pause, and exit;
 *   6. the resource ledger is balanced at the end of every fault case.
 */
void dual_run_is_not_enabled_in_this_baseline()
{
    check(!kDualRunEnabledV1,
          "the DUAL run scenario stays gated until W0 and W2 land");
}

} // namespace
} // namespace flynes::tests::nearby

int main()
{
    using namespace flynes::tests::nearby;
    only_dual_mode_is_selectable();
    frozen_dual_geometry_holds();
    impossible_dpad_states_are_normalized_before_the_wire();
    the_window_gate_freezes_instead_of_extrapolating();
    two_engines_cannot_reach_a_dual_run_yet();
    dual_run_is_not_enabled_in_this_baseline();
    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("flynes_two_engine_dual_run passed");
    return 0;
}
