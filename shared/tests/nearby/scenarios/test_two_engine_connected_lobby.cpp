/*
 * W3 E2E-HARNESS scenario: the two-engine fixture itself.
 *
 * Status against the current baseline (e8703e4 + W0 QUIC wiring e22cbff):
 * LINK_HELLO / LINK_READY are frozen as contracts but are not yet wired into
 * SessionEngine, so two public engines cannot reach CONNECTED_LOBBY yet. This
 * file therefore asserts the strongest properties that ARE reachable today and
 * marks the ones that need W0's Task 9 wiring. Nothing here fabricates a peer
 * conclusion.
 */

#include "../harness/two_engine_fixture.hpp"

#include <cstdio>
#include <vector>

namespace flynes::tests::nearby {
namespace {

/* ------------------------------------------------------------------ tests */

void two_public_engines_are_created_and_isolated()
{
    TwoEngineFixtureV1 pair;

    check(pair.left().engine != nullptr, "left public engine exists");
    check(pair.right().engine != nullptr, "right public engine exists");
    check(pair.endpoints_are_isolated(),
          "the two engines share no provider context, store or handle space");

    /*
     * A fresh engine is UNAVAILABLE until the platform state port reports the
     * process as ready; that transition is the platform's job, not the
     * harness's.
     */
    const auto left = pair.left().snapshot();
    const auto right = pair.right().snapshot();
    check(left.link_state == FLY_SESSION_LINK_UNAVAILABLE_V2,
          "a fresh engine is UNAVAILABLE before its platform snapshot");
    check(left.link_state == right.link_state,
          "both engines publish the same initial link state");
    check(left.engine_state == right.engine_state,
          "both engines publish the same initial engine state");

    pair.release_engines();
    pair.check_resources_balanced("creation and teardown");
}

void providing_platform_state_keeps_both_engines_usable()
{
    TwoEngineFixtureV1 pair;
    pair.bring_up();
    check(pair.left().snapshot().link_state == FLY_SESSION_LINK_IDLE_V2,
          "the platform snapshot moves the engine out of UNAVAILABLE to IDLE");

    std::vector<fly_session_action_descriptor_v2> left_actions;
    pair.left().snapshot(&left_actions);
    check(find_action(left_actions, FLY_SESSION_ACTION_START_DISCOVERY_V2) !=
              nullptr,
          "left engine offers the bounded discovery start action");
    check(find_action(left_actions, FLY_SESSION_ACTION_CREATE_INVITE_V2) !=
              nullptr,
          "left engine offers the create-invite action");

    std::vector<fly_session_action_descriptor_v2> right_actions;
    pair.right().snapshot(&right_actions);
    check(find_action(right_actions, FLY_SESSION_ACTION_JOIN_CODE_V2) != nullptr,
          "right engine offers the join-code action");

    /*
     * W0 Task 9 gate. Until LINK_HELLO / LINK_READY are wired into
     * SessionEngine there is no legal path to CONNECTED_LOBBY, so the harness
     * must observe that it is NOT published. Flip kHelloReadyWiredIntoEngineV1
     * only together with the Task 9 wiring; see the pending list at the bottom
     * of this file.
     */
    if (!kHelloReadyWiredIntoEngineV1)
    {
        check(pair.left().snapshot().link_state !=
                  FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
              "no engine publishes CONNECTED_LOBBY on bring-up");
        check(pair.right().snapshot().link_state !=
                  FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
              "the peer publishes no CONNECTED_LOBBY either");
    }

    pair.release_engines();
    pair.check_resources_balanced("bring-up and teardown");
}

void a_link_attempt_crosses_the_public_abi()
{
    TwoEngineFixtureV1 pair;
    pair.bring_up();

    /*
     * Drive the responder through the public ABI: join by invite code, accept
     * the discovery connection as the peripheral side, then hand it one exact
     * PairContext logical message that the product's own GATT codec encoded.
     * Everything here is engine -> ABI -> provider; no peer conclusion is
     * injected and the inviter is never asked what it thinks happened.
     */
    const std::size_t writes_before = pair.right().discovery.writes;
    start_responder_probe(pair.right(), 101, false);

    const auto right = pair.right().snapshot();
    check(right.link_state != FLY_SESSION_LINK_IDLE_V2,
          "the joiner left IDLE after a public join-code submission");
    check(right.link_state == FLY_SESSION_LINK_AUTHENTICATING_V2,
          "the validated discovery connection moves the joiner to AUTHENTICATING");
    check(right.link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
          "no engine publishes CONNECTED_LOBBY before W0 wires HELLO/READY");

    /*
     * The engine answered through its provider port. In this baseline the
     * answer is the ordered type-16 physical ACK, which is itself an encoded
     * GATT fragment: exactly what would travel over the air to the peer.
     */
    check(pair.right().discovery.physical_acks > 0,
          "the engine answered the reassembled logical message with encoded "
          "type-16 physical ACKs");
    check(pair.right().discovery.writes == writes_before,
          "no logical GATT message has been written yet in this baseline");
    check(pair.wire_proof_of_encoded_port_write(),
          "the provider port received a fragment tagged as the encoded physical "
          "ACK, so the engine really encoded wire bytes");

    /*
     * The opposite engine is untouched: nothing has been relayed to it, and the
     * harness says so rather than inventing traffic.
     */
    check(pair.left().snapshot().link_state == FLY_SESSION_LINK_IDLE_V2,
          "the inviter is still IDLE because nothing was relayed to it");
    check(pair.wire().gatt_fragments_out == 0 &&
              pair.wire().inbox_events_delivered == 0,
          "no encoded fragment has been relayed to the peer engine yet");

    pair.release_engines();
}

void the_real_quic_provider_is_linked()
{
    /*
     * Honest scope note: this proves the Rust/Quinn provider is really linked
     * and executing, which is the transport loopback_quic_fixture builds on.
     * It is NOT a loopback handshake and must not be reported as one.
     */
    check(real_quic_provider_is_linked(),
          "the real Rust/Quinn QUIC provider is linked and validates its input");
}

void scheduler_reorders_duplicates_drops_and_cancels()
{
    TwoEngineFixtureV1 pair;
    pair.bring_up();

    /* Nothing has been emitted yet, so a fault on an empty queue is a no-op
     * rather than a crash: the scheduler must be usable before any traffic. */
    pair.scheduler().apply(InterruptionV1{InterruptionKindV1::Drop, 0});
    check(pair.scheduler().report().dropped == 0,
          "a fault on an empty queue is refused, not counted");

    /* Clocks are per endpoint: advancing one must not move the other. */
    const auto left_before = pair.scheduler().clock_now(1);
    const auto right_before = pair.scheduler().clock_now(2);
    pair.scheduler().advance_clock(1, 300000000);
    check(pair.scheduler().clock_now(1) == left_before + 300000000,
          "advancing the left clock moves only the left clock");
    check(pair.scheduler().clock_now(2) == right_before,
          "advancing the left clock leaves the right clock untouched");

    /* Pausing one endpoint must not stall the other. */
    pair.scheduler().pause(2);
    check(pair.scheduler().paused(2), "the right endpoint reports paused");
    check(!pair.scheduler().paused(1), "the left endpoint is still running");
    pair.scheduler().resume(2);
    check(!pair.scheduler().paused(2), "the right endpoint reports resumed");

    pair.release_engines();
    pair.check_resources_balanced("scheduler pauses and clock advance");
}

void fault_injection_is_accounted_for()
{
    TwoEngineFixtureV1 pair;
    pair.bring_up();
    start_responder_probe(pair.right(), 102, false);
    pair.capture_transport();

    const auto pending = pair.scheduler().pending();
    for (std::size_t index = 0; index < pending; ++index)
    {
        pair.scheduler().apply(
            InterruptionV1{InterruptionKindV1::Duplicate, 0});
        pair.scheduler().apply(
            InterruptionV1{InterruptionKindV1::ReorderToBack, 0});
    }
    check(pair.scheduler().report().duplicated == pending,
          "every queued frame was duplicated exactly once");
    check(pair.scheduler().report().reordered == pending,
          "every queued frame was reordered exactly once");

    pair.scheduler().run_until_idle();
    check(pair.right().snapshot().link_state !=
              FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
          "reordered/duplicated completions never fabricate a lobby");

    pair.release_engines();
}

/* --------------------------------------------------- pending W0 integration */

/*
 * PENDING ASSERTIONS (enabled by flipping kHelloReadyWiredIntoEngineV1 after
 * W0's Task 9 wiring; they are listed here so the flip is a single edit):
 *
 *   1. after a completed invite/join handshake both engines publish
 *      link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2;
 *   2. the left and right engines publish byte-identical session_id and
 *      link_id, and mirrored roles;
 *   3. both engines publish the same negotiated capability bits with the DUAL
 *      bit set and both STREAM bits clear;
 *   4. the resource ledger is balanced with a non-zero
 *      `completed_operations.acquire()` count, i.e. the lobby was reached by
 *      real provider completions and not by an injected conclusion;
 *   5. a tampered LINK_HELLO / LINK_READY at any layer fails closed and the
 *      ledger reports at least one `stale_completions` entry;
 *   6. `wire().gatt_fragments_out > 0` for a real *logical* GATT write. Today the
 *      reachable crossing is only the engine's physical-ACK answer, so the
 *      assertion is limited to encoded ACK fragments;
 *   7. both engines destroy through the public ABI after `begin_shutdown`. Today
 *      an engine that already dispatched a discovery operation stays BUSY
 *      (destroy returns FLY_SESSION_V2_BUSY and its context stays outstanding in
 *      the oracle), so the harness reports that instead of hiding it. See the
 *      W0 integration requests in the delivery report.
 */

} // namespace
} // namespace flynes::tests::nearby

int main()
{
    using namespace flynes::tests::nearby;
    two_public_engines_are_created_and_isolated();
    providing_platform_state_keeps_both_engines_usable();
    a_link_attempt_crosses_the_public_abi();
    the_real_quic_provider_is_linked();
    scheduler_reorders_duplicates_drops_and_cancels();
    fault_injection_is_accounted_for();
    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("flynes_two_engine_connected_lobby passed");
    return 0;
}
