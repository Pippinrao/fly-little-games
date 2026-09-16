/*
 * W3 scenario: DUAL run over two engines - what is real today, and what is not.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS FILE VERIFIES (and it is real end to end)
 *
 *   1. Two PUBLIC engines (two independent `fly_session_create_v2` instances,
 *      independent provider contexts, independent stores, independent resource
 *      handle spaces) drive the real wire serialization and the real loopback
 *      QUIC link to FLY_SESSION_LINK_CONNECTED_LOBBY_V2, and their SEMANTIC
 *      STATE SEQUENCES are identical: the same ordered path through the link
 *      states, the same terminal game state, and - measured, not assumed - the
 *      same DUAL run identity fields (mode, lifecycle, seats, seat revision,
 *      frame/verified/commit watermarks, content hash, session and branch id).
 *      That is the `E2E-SCENARIOS` clause "two public engines produce identical
 *      semantic state sequences" with real command output behind it.
 *
 *   2. The public DUAL path is MEASURABLY ABSENT today. The gate
 *      `kDualRunWiredIntoThePublicEngineV1` is false and the test asserts the
 *      observed shape that makes it false, instead of pretending: the engines
 *      publish no SELECT_CONTENT / START_DUAL action, and every DUAL identity
 *      field of both snapshots stays at its inert value. When W0's Task 10 wires
 *      the DUAL data plane into the public action/snapshot boundary, those
 *      assertions start failing and this file has to grow the real run.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS FILE DOES NOT VERIFY - reported, not papered over
 *
 *   - "load a public test ROM fixture or a deterministic runtime fixture; both
 *     engines confirm authority/P1/P2/DUAL; run at least 600 frames; P1 and P2
 *     each produce at least 100 distinguishable input edges; both ends end with
 *     identical frame/state/input roots."
 *
 *   That is plan Task 8 Step 3, and its PRECONDITION does not exist in this
 *   branch: `SessionEngine` has no DUAL code path at all. `shared/src/session`
 *   matches `START_DUAL|SELECT_CONTENT|dual_runtime|DualRuntimePort` only in
 *   `ports/session_ports.hpp` (the optional port table slot),
 *   `flynes_session_v2.cpp` (ABI validation) and the `dual` scheduler directory
 *   (W2's standalone scheduler); nothing in `engine/session_engine.cpp`. The
 *   engine's action dispatch handles exactly CREATE_INVITE, JOIN_CODE,
 *   START_DISCOVERY, CANCEL_INVITE, CANCEL_JOIN, STOP_DISCOVERY, JOIN_CANDIDATE,
 *   CONFIRM_SAS, REJECT_SAS and CANCEL_LOADING - and `view/session_view.cpp`
 *   reads no DUAL field, so `DualRuntimePort` is never invoked. The
 *   repository's own tracker records exactly this: `docs/acceptance/2026-09-15-
 *   nearby-dual-nondevice-acceptance.md` has `MVP-DUAL = NOT_RUN`, "未开始（依赖
 *   W2 的 DUAL 数据面接入公开 action/snapshot 边界）" - that wiring is plan Task
 *   10, owned by the W0 acceptance worktree, whose tip `d3f1e2d` is explicitly
 *   only "Step 1 of the DUAL integration" (the ABI appended by tail).
 *
 *   Driving a 600-frame DUAL run here would therefore mean either (a) writing
 *   Task 10 inside a worktree that does not own `session_engine.cpp` and
 *   colliding with W0's live work, (b) driving W2's `DualRunSchedulerV1`
 *   directly, which proves the DUAL data plane but NOT "two public engines", or
 *   (c) fabricating a dual run the real ABI cannot produce, which this harness
 *   forbids. None of the three is Task 8 Step 3, so this file reports the gap
 *   and asserts the blocking shape. The 600-frame convergence itself is already
 *   verified at the layer where the code exists: `nearby_dual_run_scheduler`
 *   and `nearby_dual_run_races` (acceptance tracker `DUAL-RUN = PASS`).
 */

#include "../harness/two_engine_loopback_fixture.hpp"

#include <cstdio>
#include <cstring>

namespace {

using flynes::session::loopback::EngineFixture;
using flynes::session::loopback::LoopbackRole;
using flynes::session::loopback::LoopbackSide;
using flynes::session::loopback::LoopbackTransport;
using flynes::session::loopback::LoopbackWorld;
using flynes::session::loopback::PumpLimits;
using flynes::session::loopback::PumpState;
using flynes::session::loopback::RelayReport;
using flynes::session::loopback::check;
using flynes::session::loopback::find_action;
using flynes::session::loopback::pump_engine;
using flynes::session::loopback::relay_and_pump_until_idle;
using flynes::session::loopback::shutdown_engine_with_the_pump;
using flynes::session::loopback::submit;

/*
 * True only once the DUAL data plane is wired into the public action/snapshot
 * boundary (plan Task 10 / gate `MVP-DUAL`). It is deliberately false and the
 * gate itself is asserted below, so this file can never report a DUAL run it did
 * not perform.
 */
inline constexpr bool kDualRunWiredIntoThePublicEngineV1 = false;

/* One engine's DUAL-visible semantic state, read only through the public ABI. */
struct DualIdentityV1 final
{
    std::uint32_t mode = 0;
    std::uint32_t state = 0;
    std::uint32_t freeze_reason = 0;
    std::uint32_t local_seat = 0;
    std::uint32_t authority_seat = 0;
    std::uint32_t seats_confirmed = 0;
    std::uint64_t seat_revision = 0;
    std::uint64_t frame_index = 0;
    std::uint64_t verified_through = 0;
    std::uint64_t commit_frontier = 0;
    std::uint64_t prediction_depth = 0;
    std::array<std::uint8_t, 32> content_hash{};
    std::array<std::uint8_t, 16> session_id{};
    std::array<std::uint8_t, 16> branch_id{};

    [[nodiscard]] bool is_inert() const noexcept
    {
        const bool zero_hash =
            std::all_of(content_hash.begin(), content_hash.end(),
                        [](std::uint8_t byte) { return byte == 0; });
        const bool zero_session =
            std::all_of(session_id.begin(), session_id.end(),
                        [](std::uint8_t byte) { return byte == 0; });
        const bool zero_branch =
            std::all_of(branch_id.begin(), branch_id.end(),
                        [](std::uint8_t byte) { return byte == 0; });
        return mode == 0u && state == FLY_SESSION_DUAL_UNLOADED_V2 &&
               freeze_reason == FLY_SESSION_DUAL_FREEZE_NONE_V2 &&
               local_seat == 0u && authority_seat == 0u &&
               seats_confirmed == 0u && seat_revision == 0u &&
               frame_index == 0u && verified_through == 0u &&
               commit_frontier == 0u && prediction_depth == 0u && zero_hash &&
               zero_session && zero_branch;
    }

    [[nodiscard]] bool operator==(const DualIdentityV1& other) const noexcept
    {
        return mode == other.mode && state == other.state &&
               freeze_reason == other.freeze_reason &&
               local_seat == other.local_seat &&
               authority_seat == other.authority_seat &&
               seats_confirmed == other.seats_confirmed &&
               seat_revision == other.seat_revision &&
               frame_index == other.frame_index &&
               verified_through == other.verified_through &&
               commit_frontier == other.commit_frontier &&
               prediction_depth == other.prediction_depth &&
               content_hash == other.content_hash &&
               session_id == other.session_id && branch_id == other.branch_id;
    }
};

DualIdentityV1 dual_identity(const fly_session_snapshot_v2& snapshot)
{
    DualIdentityV1 identity{};
    identity.mode = snapshot.dual_mode;
    identity.state = snapshot.dual_state;
    identity.freeze_reason = snapshot.dual_freeze_reason;
    identity.local_seat = snapshot.dual_local_seat;
    identity.authority_seat = snapshot.dual_authority_seat;
    identity.seats_confirmed = snapshot.dual_seats_confirmed;
    identity.seat_revision = snapshot.dual_seat_revision;
    identity.frame_index = snapshot.dual_frame_index;
    identity.verified_through = snapshot.dual_verified_through;
    identity.commit_frontier = snapshot.dual_commit_frontier;
    identity.prediction_depth = snapshot.dual_prediction_depth;
    std::copy(std::begin(snapshot.dual_content_hash),
              std::end(snapshot.dual_content_hash), identity.content_hash.begin());
    std::copy(std::begin(snapshot.dual_session_id),
              std::end(snapshot.dual_session_id), identity.session_id.begin());
    std::copy(std::begin(snapshot.dual_branch_id),
              std::end(snapshot.dual_branch_id), identity.branch_id.begin());
    return identity;
}

/*
 * Brings two public engines up, drives the full authenticated link, and reports
 * both ends' semantic state. Nothing is invented: every value returned here was
 * read back out of a public snapshot or a provider port's own counters.
 */
struct PublicPairRunV1 final
{
    unsigned inviter_link_state = 0;
    unsigned joiner_link_state = 0;
    unsigned inviter_game_state = 0;
    unsigned joiner_game_state = 0;
    unsigned inviter_authenticating_seen = 0;
    unsigned joiner_authenticating_seen = 0;
    DualIdentityV1 inviter_dual{};
    DualIdentityV1 joiner_dual{};
    std::vector<std::uint32_t> action_kinds{};
    std::vector<std::uint32_t> inviter_objects{};
    std::vector<std::uint32_t> joiner_objects{};
    int gatt_fragments_crossed = 0;
    int quic_units_crossed = 0;
};

PublicPairRunV1 two_public_engines_reach_the_lobby()
{
    LoopbackWorld world;
    EngineFixture inviter(world, LoopbackSide::Initiator);
    EngineFixture joiner(world, LoopbackSide::Responder);

    inviter.platform.ready();
    joiner.platform.ready();
    inviter.executor.run_all();
    joiner.executor.run_all();

    std::vector<fly_session_action_descriptor_v2> inviter_actions;
    std::vector<fly_session_action_descriptor_v2> joiner_actions;
    inviter.snapshot(&inviter_actions);
    joiner.snapshot(&joiner_actions);
    const auto* create =
        find_action(inviter_actions, FLY_SESSION_ACTION_CREATE_INVITE_V2);
    const auto* join =
        find_action(joiner_actions, FLY_SESSION_ACTION_JOIN_CODE_V2);
    check(create != nullptr && join != nullptr,
          "both engines publish the link action their role needs");

    PublicPairRunV1 run{};
    if (create == nullptr || join == nullptr)
    {
        for (auto& action : inviter_actions)
            fly_session_approval_token_release_v2(action.approval_token);
        for (auto& action : joiner_actions)
            fly_session_approval_token_release_v2(action.approval_token);
        return run;
    }
    submit(inviter, *create, 1401, false);
    submit(joiner, *join, 1402, true);

    LoopbackTransport transport;
    transport.attach(LoopbackRole::AdvertiserPeripheral, inviter);
    transport.attach(LoopbackRole::ScannerCentral, joiner);
    check(transport.connect_ends() == 2,
          "the transport produced the connection at both ends of one link");

    PumpState inviter_pump;
    PumpState joiner_pump;
    const PumpLimits limits{};
    pump_engine(inviter, inviter_pump, limits);
    pump_engine(joiner, joiner_pump, limits);

    /* Phase 1: the authenticated pair exchange, with the app's own SAS decision
     * not yet taken. Both engines must be measured in AUTHENTICATING here, which
     * is what makes the ordered link-state sequence below a MEASURED path and
     * not a restatement of the enum. */
    RelayReport relayed;
    relay_and_pump_until_idle(transport, inviter, inviter_pump, joiner, joiner_pump,
                              limits, 4000, 8, &relayed, false);
    run.inviter_authenticating_seen =
        inviter.snapshot().link_state == FLY_SESSION_LINK_AUTHENTICATING_V2 ? 1u : 0u;
    run.joiner_authenticating_seen =
        joiner.snapshot().link_state == FLY_SESSION_LINK_AUTHENTICATING_V2 ? 1u : 0u;

    /* Phase 2: the app confirms the SAS on both ends and the QUIC bind + Control
     * streams are carried to completion. */
    relay_and_pump_until_idle(transport, inviter, inviter_pump, joiner, joiner_pump,
                              limits, 4000, 8, &relayed, true);

    const auto inviter_state = inviter.snapshot();
    const auto joiner_state = joiner.snapshot();
    run.inviter_link_state = static_cast<unsigned>(inviter_state.link_state);
    run.joiner_link_state = static_cast<unsigned>(joiner_state.link_state);
    run.inviter_game_state = static_cast<unsigned>(inviter_state.game_state);
    run.joiner_game_state = static_cast<unsigned>(joiner_state.game_state);
    run.inviter_dual = dual_identity(inviter_state);
    run.joiner_dual = dual_identity(joiner_state);
    run.action_kinds = relayed.app_action_kinds;
    run.inviter_objects = inviter.object_store.kinds;
    run.joiner_objects = joiner.object_store.kinds;
    run.gatt_fragments_crossed = static_cast<int>(
        relayed.peripheral_to_central.delivered.size() +
        relayed.central_to_peripheral.delivered.size());
    run.quic_units_crossed = relayed.quic_peripheral_to_central.units +
                             relayed.quic_central_to_peripheral.units;

    shutdown_engine_with_the_pump(inviter, inviter_pump, limits);
    shutdown_engine_with_the_pump(joiner, joiner_pump, limits);
    for (auto& action : inviter_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : joiner_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    return run;
}

void the_two_public_engines_agree_on_their_semantic_state()
{
    std::printf("dual run: two PUBLIC engines, real link, semantic comparison\n");
    const auto run = two_public_engines_reach_the_lobby();

    std::printf("  link states: inviter=%u joiner=%u (8 = CONNECTED_LOBBY, "
                "5 = AUTHENTICATING)\n",
                run.inviter_link_state, run.joiner_link_state);
    std::printf("  game states: inviter=%u joiner=%u\n", run.inviter_game_state,
                run.joiner_game_state);
    std::printf("  crossed: %d GATT fragments, %d QUIC units\n",
                run.gatt_fragments_crossed, run.quic_units_crossed);
    std::printf("  object kinds persisted: inviter=%zu joiner=%zu\n",
                run.inviter_objects.size(), run.joiner_objects.size());

    /* The positive path completes, on BOTH ends, independently asserted. */
    check(run.inviter_link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
              run.joiner_link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
          "BOTH public engines complete the positive path to "
          "FLY_SESSION_LINK_CONNECTED_LOBBY_V2 over the real loopback QUIC link");

    /* The semantic state SEQUENCE: both ends really walked the same ordered path
     * through AUTHENTICATING before the lobby, measured mid-run rather than
     * inferred from the terminal value. */
    check(run.inviter_authenticating_seen == 1u &&
              run.joiner_authenticating_seen == 1u,
          "both engines were really measured in FLY_SESSION_LINK_AUTHENTICATING_V2 "
          "before the app confirmed the SAS, so the two link-state sequences are "
          "the same ordered path and not just the same terminal value");

    /* Terminal semantic equality, field by field, through the public ABI. */
    check(run.inviter_game_state == run.joiner_game_state,
          "both engines publish the SAME terminal game state");
    check(run.inviter_objects == run.joiner_objects,
          "both engines persisted the SAME object kinds, in the same order - "
          "0x0212 binding, 0x0213 pair transcript, 0x0216 HELLO, 0x0217 READY");
    check(run.inviter_objects.size() >= 4u,
          "the run really reached the QUIC Control stream, so the semantic "
          "comparison covers the link handshake and not only the pair exchange");

    /* The DUAL identity the two engines must agree on. Today both are inert;
     * the comparison is written the way it must hold once they are not. */
    check(run.inviter_dual == run.joiner_dual,
          "both engines publish the SAME DUAL run identity (mode, lifecycle, "
          "freeze reason, seats, seat revision, frame/verified/commit watermarks, "
          "content hash, session and branch id)");
    check(run.inviter_dual.is_inert() && run.joiner_dual.is_inert(),
          "and that identity is INERT today: with no DUAL run started, every DUAL "
          "field is at its zero value on both ends - asserted rather than assumed, "
          "so this test cannot silently accept a half-populated DUAL state");
}

void the_public_dual_run_path_is_not_wired_into_the_engine()
{
    std::printf("dual run: the public DUAL path - gate and observed shape\n");
    check(!kDualRunWiredIntoThePublicEngineV1,
          "the gate is false: the DUAL data plane is NOT wired into the public "
          "action/snapshot boundary in this branch (plan Task 10 / MVP-DUAL)");

    const auto run = two_public_engines_reach_the_lobby();

    const bool publishes_dual_actions =
        std::find(run.action_kinds.begin(), run.action_kinds.end(),
                  FLY_SESSION_ACTION_SELECT_CONTENT_V2) != run.action_kinds.end() ||
        std::find(run.action_kinds.begin(), run.action_kinds.end(),
                  FLY_SESSION_ACTION_START_DUAL_V2) != run.action_kinds.end();
    std::printf("  app action kinds published at the lobby: %zu; DUAL kinds "
                "present=%s\n",
                run.action_kinds.size(), publishes_dual_actions ? "yes" : "no");

    /*
     * The observed shape that makes the gate false. This is deliberately a
     * POSITIVE claim about the ABSENCE of a feature, so it fails loudly the day
     * the engine starts offering SELECT_CONTENT / START_DUAL - at which point
     * this file must be extended into the real 600-frame run instead of quietly
     * passing.
     */
    check(!publishes_dual_actions,
          "the two engines reach the lobby WITHOUT ever publishing "
          "SELECT_CONTENT_V2 or START_DUAL_V2, so no DUAL run can legally be "
          "started between two public engines on this branch");
    check(run.inviter_dual.is_inert() && run.joiner_dual.is_inert(),
          "and neither engine reports any DUAL run state, so a reader that looked "
          "for a DUAL run here would find nothing rather than a fabricated one");
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    the_two_public_engines_agree_on_their_semantic_state();
    the_public_dual_run_path_is_not_wired_into_the_engine();
    if (flynes::session::loopback::failures != 0)
    {
        std::fprintf(stderr, "%d failure(s)\n", flynes::session::loopback::failures);
        return 1;
    }
    std::puts("flynes_two_engine_dual_run passed");
    return 0;
}
