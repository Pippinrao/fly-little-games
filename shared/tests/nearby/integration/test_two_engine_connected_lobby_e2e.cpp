/*
 * W0 two-engine loopback end-to-end acceptance (MVP-LOBBY).
 *
 * This file is grown in steps, and each step must leave the repository green. It
 * currently contains step 1 only:
 *
 *   step 1 (this revision)  two independent public engines come up side by side
 *                           through the carved harness, each projecting READY /
 *                           IDLE with its own approval-token action. Nothing is
 *                           exchanged yet: the two engines are NOT connected and
 *                           no byte crosses between them.
 *   step 2                  P-256 public-key table + the shared deterministic
 *                           provider world (crypto/key), so that both engines can
 *                           compute matching pair state.
 *   step 3                  byte-accurate GATT/discovery loopback.
 *   step 4                  byte-accurate QUIC loopback (bind stream + Control).
 *   step 5                  alternate pumping until BOTH engines project
 *                           FLY_SESSION_LINK_CONNECTED_LOBBY_V2.
 *
 * The deliverable this file is working towards is the MVP-LOBBY acceptance line:
 * two public engines, real byte-level loopback, no ROM, zero fabricated peer
 * bytes, both sides in CONNECTED_LOBBY. Until step 5 lands, this file makes no
 * claim about CONNECTED_LOBBY and the test names say so.
 */

#include "../harness/two_engine_loopback_fixture.hpp"

namespace {

using flynes::session::loopback::EngineFixture;
using flynes::session::loopback::check;
using flynes::session::loopback::failures;
using flynes::session::loopback::find_action;

/*
 * step 1: two public engines are constructed and driven to their READY
 * projection independently. This is the foundation the later steps build on: it
 * proves the carved harness really instantiates two fully separate engines (two
 * port sets, two executives, two views) rather than sharing any state.
 */
void two_engines_come_up_independently()
{
    EngineFixture inviter;
    EngineFixture joiner;

    check(inviter.engine != nullptr && joiner.engine != nullptr,
          "two public engines are constructed side by side");
    check(inviter.engine != joiner.engine,
          "the two engines are distinct instances");

    inviter.platform.ready();
    joiner.platform.ready();
    inviter.executor.run_all();
    joiner.executor.run_all();

    std::vector<fly_session_action_descriptor_v2> inviter_actions;
    std::vector<fly_session_action_descriptor_v2> joiner_actions;
    const auto inviter_ready = inviter.snapshot(&inviter_actions);
    const auto joiner_ready = joiner.snapshot(&joiner_actions);

    check(inviter_ready.engine_state == FLY_SESSION_ENGINE_READY_V2 &&
              inviter_ready.link_state == FLY_SESSION_LINK_IDLE_V2 &&
              joiner_ready.engine_state == FLY_SESSION_ENGINE_READY_V2 &&
              joiner_ready.link_state == FLY_SESSION_LINK_IDLE_V2,
          "both public engines project ready idle link facts");

    const auto* create =
        find_action(inviter_actions, FLY_SESSION_ACTION_CREATE_INVITE_V2);
    const auto* join =
        find_action(joiner_actions, FLY_SESSION_ACTION_JOIN_CODE_V2);
    check(create != nullptr && create->enabled && create->approval_token != 0,
          "the inviter engine publishes its create-invite approval token");
    check(join != nullptr && join->enabled && join->approval_token != 0,
          "the joiner engine publishes its join-code approval token");

    /* The two port sets really are separate: neither engine has touched its
     * discovery port just by coming up. */
    check(inviter.discovery.advertisements == 0 &&
              inviter.discovery.scans == 0 &&
              joiner.discovery.advertisements == 0 &&
              joiner.discovery.scans == 0,
          "neither engine has started any discovery operation yet");

    if (create != nullptr)
        fly_session_approval_token_release_v2(create->approval_token);
    if (join != nullptr)
        fly_session_approval_token_release_v2(join->approval_token);
}

} // namespace

int main()
{
    two_engines_come_up_independently();

    if (failures != 0)
    {
        std::fprintf(stderr,
                     "%d two-engine loopback checks failed\n", failures);
        return 1;
    }
    std::puts("two-engine loopback (step 1: two engines come up) passed");
    return 0;
}
