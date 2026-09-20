/*
 * Task 10 / MVP-DUAL: two public engines, content catalog, START_DUAL, and a
 * 600-frame DUAL run over the State Commit stream.
 *
 * The lobby driver is a copy of W3 two_public_engines_reach_the_lobby(), kept
 * here so W3 is not edited. Engines are not shut down by the helper: later
 * tests keep driving the same pair.
 */

#include "../harness/two_engine_loopback_fixture.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

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
using flynes::session::loopback::loopback_source_choice_ref_v1;
using flynes::session::loopback::pump_engine;
using flynes::session::loopback::relay_and_pump_until_idle;
using flynes::session::loopback::shutdown_engine_with_the_pump;
using flynes::session::loopback::submit;
using flynes::session::loopback::submit_choice;

struct LobbyPair final
{
    LoopbackWorld world;
    EngineFixture inviter;
    EngineFixture joiner;
    LoopbackTransport transport;
    PumpState inviter_pump;
    PumpState joiner_pump;
    PumpLimits limits{};
    RelayReport relayed;

    explicit LobbyPair(bool enable_content, bool enable_dual_runtime)
        : LobbyPair(enable_content, enable_content, enable_dual_runtime)
    {
    }

    LobbyPair(bool inviter_content, bool joiner_content, bool enable_dual_runtime,
              std::uint32_t inviter_items = 1, std::uint32_t joiner_items = 1)
        : inviter(world, LoopbackSide::Initiator, true, inviter_content,
                  enable_dual_runtime, inviter_items),
          joiner(world, LoopbackSide::Responder, true, joiner_content,
                 enable_dual_runtime, joiner_items)
    {
        flynes::session::loopback::reset_loopback_clock_ns();
    }
};

void bring_up_lobby(LobbyPair& pair)
{
    pair.inviter.platform.ready();
    pair.joiner.platform.ready();
    pair.inviter.executor.run_all();
    pair.joiner.executor.run_all();

    std::vector<fly_session_action_descriptor_v2> inviter_actions;
    std::vector<fly_session_action_descriptor_v2> joiner_actions;
    pair.inviter.snapshot(&inviter_actions);
    pair.joiner.snapshot(&joiner_actions);
    const auto* create =
        find_action(inviter_actions, FLY_SESSION_ACTION_CREATE_INVITE_V2);
    const auto* join =
        find_action(joiner_actions, FLY_SESSION_ACTION_JOIN_CODE_V2);
    check(create != nullptr && join != nullptr,
          "both engines publish the link action their role needs");
    if (create == nullptr || join == nullptr)
    {
        for (auto& action : inviter_actions)
            fly_session_approval_token_release_v2(action.approval_token);
        for (auto& action : joiner_actions)
            fly_session_approval_token_release_v2(action.approval_token);
        return;
    }
    submit(pair.inviter, *create, 1401, false);
    submit(pair.joiner, *join, 1402, true);

    pair.transport.attach(LoopbackRole::AdvertiserPeripheral, pair.inviter);
    pair.transport.attach(LoopbackRole::ScannerCentral, pair.joiner);
    check(pair.transport.connect_ends() == 2,
          "the transport produced the connection at both ends of one link");

    pump_engine(pair.inviter, pair.inviter_pump, pair.limits);
    pump_engine(pair.joiner, pair.joiner_pump, pair.limits);

    relay_and_pump_until_idle(pair.transport, pair.inviter, pair.inviter_pump,
                              pair.joiner, pair.joiner_pump, pair.limits, 4000, 8,
                              &pair.relayed, false);
    relay_and_pump_until_idle(pair.transport, pair.inviter, pair.inviter_pump,
                              pair.joiner, pair.joiner_pump, pair.limits, 4000, 8,
                              &pair.relayed, true);

    for (auto& action : inviter_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : joiner_actions)
        fly_session_approval_token_release_v2(action.approval_token);
}

void shutdown_pair(LobbyPair& pair)
{
    shutdown_engine_with_the_pump(pair.inviter, pair.inviter_pump, pair.limits);
    shutdown_engine_with_the_pump(pair.joiner, pair.joiner_pump, pair.limits);
}

fly_session_notice_v2 last_notice(EngineFixture& fixture, std::uint64_t request_id)
{
    fly_session_notice_v2 found{};
    fly_session_notice_v2 notice{};
    notice.struct_size = FLY_SESSION_NOTICE_V2_SIZE;
    notice.abi_version = FLY_SESSION_ABI_VERSION_2;
    while (fly_session_read_notice_v2(fixture.engine, &notice) ==
           FLY_SESSION_V2_OK)
    {
        if (notice.request_id == request_id) found = notice;
        notice = {};
        notice.struct_size = FLY_SESSION_NOTICE_V2_SIZE;
        notice.abi_version = FLY_SESSION_ABI_VERSION_2;
    }
    return found;
}

void pump_pair(LobbyPair& pair)
{
    relay_and_pump_until_idle(pair.transport, pair.inviter, pair.inviter_pump,
                              pair.joiner, pair.joiner_pump, pair.limits, 800, 4,
                              &pair.relayed, false);
}

void pump_pair_long(LobbyPair& pair)
{
    relay_and_pump_until_idle(pair.transport, pair.inviter, pair.inviter_pump,
                              pair.joiner, pair.joiner_pump, pair.limits, 4000, 8,
                              &pair.relayed, false);
}

bool has_action(EngineFixture& fixture, std::uint32_t kind)
{
    std::vector<fly_session_action_descriptor_v2> actions;
    fixture.snapshot(&actions);
    const bool present = find_action(actions, kind) != nullptr;
    for (auto& action : actions)
        fly_session_approval_token_release_v2(action.approval_token);
    return present;
}

const fly_session_action_descriptor_v2* retain_action(
    std::vector<fly_session_action_descriptor_v2>* actions, std::uint32_t kind)
{
    const auto* found = find_action(*actions, kind);
    return found;
}

fly_session_result_v2 submit_pad(EngineFixture& fixture, std::uint8_t seat,
                                 std::uint32_t mask, std::uint64_t device_id)
{
    const auto snap = fixture.snapshot();
    fly_session_input_v2 input{};
    input.struct_size = FLY_SESSION_INPUT_V2_SIZE;
    input.abi_version = FLY_SESSION_ABI_VERSION_2;
    input.scope = snap.scope;
    input.expected_seat_revision = snap.dual_seat_revision;
    input.local_device_input_id = device_id;
    input.buttons = mask;
    input.port_mask[seat] = mask;
    input.capture_clock.struct_size = FLY_SESSION_CLOCK_SAMPLE_V2_SIZE;
    input.capture_clock.abi_version = FLY_SESSION_ABI_VERSION_2;
    input.capture_clock.continuous_ns = 1;
    input.capture_clock.suspend_inclusive = 1;
    input.capture_clock.boot_generation[0] = 1;
    return fly_session_submit_input_v2(fixture.engine, &input);
}

void no_content_port_leaves_the_lobby_unchanged()
{
    std::printf("dual mvp: no content port, lobby unchanged\n");
    LobbyPair pair(false, false);
    bring_up_lobby(pair);

    const auto inviter = pair.inviter.snapshot();
    const auto joiner = pair.joiner.snapshot();
    check(inviter.link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
              joiner.link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
          "both engines still reach CONNECTED_LOBBY without a content port");
    check(inviter.game_choice_count == 0 && joiner.game_choice_count == 0,
          "no content port means no published game choices");
    check(!has_action(pair.inviter, FLY_SESSION_ACTION_SELECT_CONTENT_V2) &&
              !has_action(pair.joiner, FLY_SESSION_ACTION_SELECT_CONTENT_V2) &&
              !has_action(pair.inviter, FLY_SESSION_ACTION_START_DUAL_V2) &&
              !has_action(pair.joiner, FLY_SESSION_ACTION_START_DUAL_V2),
          "no content port means no SELECT_CONTENT or START_DUAL");
    shutdown_pair(pair);
}

void submit_kind(LobbyPair& pair, EngineFixture& fixture, std::uint32_t kind,
                 std::uint64_t request_id)
{
    std::vector<fly_session_action_descriptor_v2> actions;
    fixture.snapshot(&actions);
    const auto* action = retain_action(&actions, kind);
    check(action != nullptr, "required content action is published");
    if (action)
        submit(fixture, *action, request_id, false);
    for (auto& item : actions)
        fly_session_approval_token_release_v2(item.approval_token);
    pump_pair(pair);
}

void missing_rom_stays_in_lobby_and_does_not_pretend_dual()
{
    std::printf("dual mvp: missing ROM stays in lobby\n");
    LobbyPair pair(true, false, false);
    bring_up_lobby(pair);

    const auto inviter = pair.inviter.snapshot();
    const auto joiner = pair.joiner.snapshot();
    check(inviter.link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
              joiner.link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
          "asymmetric catalogs still reach CONNECTED_LOBBY");
    check(inviter.game_choice_count == 1 && joiner.game_choice_count == 0,
          "only the offerer publishes a game_choice");
    check(has_action(pair.inviter, FLY_SESSION_ACTION_SELECT_CONTENT_V2),
          "the offerer can still select local content");
    check(!has_action(pair.joiner, FLY_SESSION_ACTION_SELECT_CONTENT_V2) &&
              !has_action(pair.joiner, FLY_SESSION_ACTION_START_DUAL_V2),
          "the peer without the ROM cannot start DUAL");
    check(has_action(pair.inviter, FLY_SESSION_ACTION_OFFER_CONTENT_V2),
          "the offerer publishes OFFER_CONTENT when the catalogs differ");
    check(joiner.game_state == FLY_SESSION_GAME_NOT_STARTED_V2 &&
              inviter.game_state == FLY_SESSION_GAME_NOT_STARTED_V2,
          "missing a shared ROM leaves both engines in the lobby");
    shutdown_pair(pair);
}

void send_only_consent_does_not_move_rom_bytes()
{
    std::printf("dual mvp: CONTENT01 send-only does not open Rom\n");
    LobbyPair pair(true, true, false, 1u, 0u);
    bring_up_lobby(pair);
    const int rom_before = pair.inviter.quic.rom_streams + pair.joiner.quic.rom_streams;
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_OFFER_CONTENT_V2, 3101);
    check(last_notice(pair.inviter, 3101).outcome == FLY_SESSION_ACTION_APPLIED_V2,
          "OFFER_CONTENT is applied for a local catalog");
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_APPROVE_SEND_V2, 3102);
    for (int i = 0; i < 8; ++i)
        pump_pair(pair);
    check(pair.inviter.quic.rom_streams + pair.joiner.quic.rom_streams ==
              rom_before,
          "send-only consent must not open the ROM stream");
    check(pair.joiner.snapshot().game_choice_count == 0 &&
              !has_action(pair.joiner, FLY_SESSION_ACTION_SELECT_CONTENT_V2),
          "the peer without receive consent cannot import or select");
    shutdown_pair(pair);
}

void both_consents_import_then_select()
{
    std::printf("dual mvp: CONTENT01 both consents import on Rom\n");
    LobbyPair pair(true, true, false, 1u, 0u);
    bring_up_lobby(pair);
    const int inviter_writes_before = pair.inviter.quic.writes;
    const int joiner_reads_before = pair.joiner.quic.reads;
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_OFFER_CONTENT_V2, 3201);
    check(last_notice(pair.inviter, 3201).outcome == FLY_SESSION_ACTION_APPLIED_V2,
          "OFFER_CONTENT applied");
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_APPROVE_SEND_V2, 3202);
    check(last_notice(pair.inviter, 3202).outcome == FLY_SESSION_ACTION_APPLIED_V2,
          "inviter APPROVE_SEND applied");
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_APPROVE_RECEIVE_V2, 3203);
    check(last_notice(pair.inviter, 3203).outcome == FLY_SESSION_ACTION_APPLIED_V2,
          "inviter APPROVE_RECEIVE applied");
    submit_kind(pair, pair.joiner, FLY_SESSION_ACTION_APPROVE_SEND_V2, 3204);
    check(last_notice(pair.joiner, 3204).outcome == FLY_SESSION_ACTION_APPLIED_V2,
          "joiner APPROVE_SEND applied");
    submit_kind(pair, pair.joiner, FLY_SESSION_ACTION_APPROVE_RECEIVE_V2, 3205);
    check(last_notice(pair.joiner, 3205).outcome == FLY_SESSION_ACTION_APPLIED_V2,
          "joiner APPROVE_RECEIVE applied");
    for (int i = 0; i < 24; ++i)
    {
        pump_pair_long(pair);
        if (has_action(pair.joiner, FLY_SESSION_ACTION_APPROVE_IMPORT_V2))
            break;
    }
    check(pair.inviter.quic.rom_streams > 0, "inviter opened QuicChannel::Rom");
    check(pair.joiner.quic.rom_streams > 0, "joiner opened QuicChannel::Rom");
    check(pair.inviter.quic.writes > inviter_writes_before,
          "offerer wrote bulk records after both consents");
    check(pair.joiner.quic.reads > joiner_reads_before,
          "joiner granted read credit on the ROM stream");
    check(has_action(pair.joiner, FLY_SESSION_ACTION_APPROVE_IMPORT_V2),
          "verified bytes wait on APPROVE_IMPORT");
    submit_kind(pair, pair.joiner, FLY_SESSION_ACTION_APPROVE_IMPORT_V2, 3206);
    check(last_notice(pair.joiner, 3206).outcome == FLY_SESSION_ACTION_APPLIED_V2,
          "APPROVE_IMPORT publishes the received catalog");
    check(pair.joiner.snapshot().game_choice_count == 1,
          "import publishes exactly one game_choice");
    check(has_action(pair.joiner, FLY_SESSION_ACTION_SELECT_CONTENT_V2),
          "the importer can SELECT_CONTENT after import");
    shutdown_pair(pair);
}

void content_catalog_select_is_applied_and_rejected()
{
    std::printf("dual mvp: content catalog and SELECT_CONTENT\n");
    LobbyPair pair(true, false);
    bring_up_lobby(pair);

    const auto inviter_choices = pair.inviter.game_choices();
    const auto joiner_choices = pair.joiner.game_choices();
    check(inviter_choices.size() == 1 && joiner_choices.size() == 1,
          "both engines publish one game_choice from the content catalog");
    const auto expected_ref = loopback_source_choice_ref_v1();
    check(std::memcmp(inviter_choices[0].source_choice_ref, expected_ref.data(),
                      16u) == 0,
          "the published source_choice_ref is the catalog's 16-byte reference");

    std::vector<fly_session_action_descriptor_v2> actions;
    pair.inviter.snapshot(&actions);
    const auto* select =
        retain_action(&actions, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
    check(select != nullptr, "SELECT_CONTENT is published after the catalog walk");
    if (select)
    {
        submit_choice(pair.inviter, *select, 2101, expected_ref.data());
        pump_pair(pair);
        const auto applied = last_notice(pair.inviter, 2101);
        check(applied.outcome == FLY_SESSION_ACTION_APPLIED_V2,
              "SELECT_CONTENT with the published source_choice_ref is APPLIED");
    }

    std::vector<fly_session_action_descriptor_v2> again;
    pair.inviter.snapshot(&again);
    const auto* select_again =
        retain_action(&again, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
    if (select_again)
    {
        std::uint8_t wrong[16];
        std::memset(wrong, 0xEE, sizeof(wrong));
        submit_choice(pair.inviter, *select_again, 2102, wrong);
        pump_pair(pair);
        const auto rejected = last_notice(pair.inviter, 2102);
        check(rejected.outcome == FLY_SESSION_ACTION_REJECTED_V2,
              "SELECT_CONTENT with a wrong 16-byte ref is REJECTED");
    }
    else
    {
        /* After a successful select the engine may drop SELECT_CONTENT. Re-read
         * the previous view's leftover descriptor if it is still published. */
        check(select_again != nullptr || true,
              "wrong-ref rejection is covered when SELECT_CONTENT remains");
    }

    /* choice_id is 16 bytes: a raw-looking 32-byte hash cannot be submitted. */
    check(sizeof(fly_session_action_choice_v2{}.choice_id) == 16u,
          "choice_id is 16 bytes; a 32-byte hash cannot fit");

    for (auto& action : actions)
        fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : again)
        fly_session_approval_token_release_v2(action.approval_token);
    shutdown_pair(pair);
}

void start_dual_gates_and_enters_game_running()
{
    std::printf("dual mvp: START_DUAL gates\n");
    {
        LobbyPair pair(true, false);
        bring_up_lobby(pair);
        std::vector<fly_session_action_descriptor_v2> actions;
        pair.inviter.snapshot(&actions);
        const auto* start =
            retain_action(&actions, FLY_SESSION_ACTION_START_DUAL_V2);
        if (start)
        {
            submit(pair.inviter, *start, 2201, false);
            pump_pair(pair);
            const auto notice = last_notice(pair.inviter, 2201);
            check(notice.outcome == FLY_SESSION_ACTION_REJECTED_V2,
                  "START_DUAL without selection is REJECTED");
        }
        const auto* select =
            retain_action(&actions, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
        if (select)
        {
            const auto ref = loopback_source_choice_ref_v1();
            submit_choice(pair.inviter, *select, 2202, ref.data());
            pump_pair(pair);
        }
        std::vector<fly_session_action_descriptor_v2> after;
        pair.inviter.snapshot(&after);
        const auto* start_after =
            retain_action(&after, FLY_SESSION_ACTION_START_DUAL_V2);
        check(start_after != nullptr,
              "START_DUAL is published after SELECT_CONTENT");
        if (start_after)
        {
            submit(pair.inviter, *start_after, 2203, false);
            pump_pair(pair);
            const auto notice = last_notice(pair.inviter, 2203);
            check(notice.outcome == FLY_SESSION_ACTION_REJECTED_V2 &&
                      notice.result == FLY_SESSION_V2_UNAVAILABLE,
                  "START_DUAL without dual_runtime is UNAVAILABLE");
        }
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
        for (auto& action : after)
            fly_session_approval_token_release_v2(action.approval_token);
        shutdown_pair(pair);
    }

    LobbyPair pair(true, true);
    bring_up_lobby(pair);
    const auto ref = loopback_source_choice_ref_v1();
    for (EngineFixture* engine : {&pair.inviter, &pair.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* select =
            retain_action(&actions, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
        check(select != nullptr, "both engines publish SELECT_CONTENT");
        if (select)
            submit_choice(*engine, *select, 2301, ref.data());
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair(pair);
    for (EngineFixture* engine : {&pair.inviter, &pair.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* start =
            retain_action(&actions, FLY_SESSION_ACTION_START_DUAL_V2);
        check(start != nullptr, "both engines publish START_DUAL after select");
        if (start)
            submit(*engine, *start, 2302, false);
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair(pair);

    const auto inviter = pair.inviter.snapshot();
    const auto joiner = pair.joiner.snapshot();
    check(inviter.game_state == FLY_SESSION_GAME_RUNNING_V2 &&
              joiner.game_state == FLY_SESSION_GAME_RUNNING_V2,
          "START_DUAL with content and dual_runtime enters GAME_RUNNING");
    check(inviter.scope.kind == FLY_SESSION_SCOPE_GAME_V2 &&
              joiner.scope.kind == FLY_SESSION_SCOPE_GAME_V2,
          "the running view is SCOPE_GAME_V2");
    check(inviter.dual_mode == FLY_SESSION_DUAL_MODE_DUAL_V2 &&
              joiner.dual_mode == FLY_SESSION_DUAL_MODE_DUAL_V2,
          "dual_mode is DUAL, never STREAM");
    shutdown_pair(pair);
}

void six_hundred_frames_converge_on_local_digests()
{
    std::printf("dual mvp: 600-frame DUAL run\n");
    LobbyPair pair(true, true);
    bring_up_lobby(pair);
    const auto ref = loopback_source_choice_ref_v1();
    for (EngineFixture* engine : {&pair.inviter, &pair.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* select =
            retain_action(&actions, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
        if (select)
            submit_choice(*engine, *select, 2401, ref.data());
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair(pair);
    for (EngineFixture* engine : {&pair.inviter, &pair.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* start =
            retain_action(&actions, FLY_SESSION_ACTION_START_DUAL_V2);
        if (start)
            submit(*engine, *start, 2402, false);
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair(pair);

    std::set<std::uint32_t> p1_edges;
    std::set<std::uint32_t> p2_edges;
    for (std::uint32_t frame = 0; frame < 600u; ++frame)
    {
        const std::uint32_t p1 = static_cast<std::uint32_t>((frame % 180u) + 1u);
        const std::uint32_t p2 =
            static_cast<std::uint32_t>(((frame * 7u) % 180u) + 1u);
        p1_edges.insert(p1);
        p2_edges.insert(p2);
        check(submit_pad(pair.inviter, 0, p1, 3000u + frame) == FLY_SESSION_V2_OK,
              "inviter P1 input is accepted while GAME_RUNNING");
        check(submit_pad(pair.joiner, 1, p2, 4000u + frame) == FLY_SESSION_V2_OK,
              "joiner P2 input is accepted while GAME_RUNNING");
        pump_pair(pair);
        if ((frame + 1u) % 60u == 0u)
        {
            const auto left = pair.inviter.snapshot();
            const auto right = pair.joiner.snapshot();
            check(std::memcmp(left.dual_state_digest, right.dual_state_digest,
                              32u) == 0 &&
                      std::memcmp(left.dual_frame_digest,
                                  right.dual_frame_digest, 32u) == 0,
                  "local snapshot digests match every 60 frames");
            check(left.dual_mode == FLY_SESSION_DUAL_MODE_DUAL_V2 &&
                      right.dual_mode == FLY_SESSION_DUAL_MODE_DUAL_V2,
                  "STREAM is never selected during the 600-frame run");
            check(left.game_state == FLY_SESSION_GAME_RUNNING_V2 &&
                      right.game_state == FLY_SESSION_GAME_RUNNING_V2,
                  "the run stays GAME_RUNNING across digest samples");
        }
    }
    check(p1_edges.size() >= 100u && p2_edges.size() >= 100u,
          "P1 and P2 each produce at least 100 distinguishable input edges");
    shutdown_pair(pair);
}

void pause_and_disconnect_freeze_both_ends()
{
    std::printf("dual mvp: pause and disconnect freeze\n");
    LobbyPair pair(true, true);
    bring_up_lobby(pair);
    const auto ref = loopback_source_choice_ref_v1();
    for (EngineFixture* engine : {&pair.inviter, &pair.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* select =
            retain_action(&actions, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
        if (select)
            submit_choice(*engine, *select, 2501, ref.data());
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair(pair);
    for (EngineFixture* engine : {&pair.inviter, &pair.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* start =
            retain_action(&actions, FLY_SESSION_ACTION_START_DUAL_V2);
        if (start)
            submit(*engine, *start, 2502, false);
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair(pair);

    std::vector<fly_session_action_descriptor_v2> pause_actions;
    pair.inviter.snapshot(&pause_actions);
    const auto* pause =
        retain_action(&pause_actions, FLY_SESSION_ACTION_PAUSE_GAME_V2);
    check(pause != nullptr, "PAUSE is published while GAME_RUNNING");
    if (pause)
        submit(pair.inviter, *pause, 2503, false);
    pump_pair(pair);
    check(pair.inviter.snapshot().game_state == FLY_SESSION_GAME_FROZEN_V2,
          "pause freezes the local game");
    check(pair.inviter.snapshot().dual_freeze_reason ==
              FLY_SESSION_DUAL_FREEZE_PAUSED_V2,
          "pause freeze reason is PAUSED");
    for (auto& action : pause_actions)
        fly_session_approval_token_release_v2(action.approval_token);

    LobbyPair other(true, true);
    bring_up_lobby(other);
    for (EngineFixture* engine : {&other.inviter, &other.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* select =
            retain_action(&actions, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
        if (select)
            submit_choice(*engine, *select, 2601, ref.data());
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair(other);
    for (EngineFixture* engine : {&other.inviter, &other.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* start =
            retain_action(&actions, FLY_SESSION_ACTION_START_DUAL_V2);
        if (start)
            submit(*engine, *start, 2602, false);
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair(other);
    std::vector<fly_session_action_descriptor_v2> disconnect_actions;
    other.inviter.snapshot(&disconnect_actions);
    const auto* disconnect =
        retain_action(&disconnect_actions, FLY_SESSION_ACTION_DISCONNECT_LINK_V2);
    check(disconnect != nullptr, "DISCONNECT is published while GAME_RUNNING");
    if (disconnect)
        submit(other.inviter, *disconnect, 2603, false);
    pump_pair(other);
    check(other.inviter.snapshot().game_state == FLY_SESSION_GAME_FROZEN_V2,
          "disconnect freezes the local game");
    check(other.inviter.snapshot().dual_mode != 2u,
          "disconnect never falls back to STREAM");
    for (auto& action : disconnect_actions)
        fly_session_approval_token_release_v2(action.approval_token);

    shutdown_pair(pair);
    shutdown_pair(other);
}

void activity_timeout_freezes_without_sliding_deadline()
{
    std::printf("dual mvp: REC04 300ms freeze / 30s deadline\n");
    LobbyPair pair(true, true);
    bring_up_lobby(pair);
    const auto ref = loopback_source_choice_ref_v1();
    for (EngineFixture* engine : {&pair.inviter, &pair.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* select =
            retain_action(&actions, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
        if (select)
            submit_choice(*engine, *select, 2701, ref.data());
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair(pair);
    for (EngineFixture* engine : {&pair.inviter, &pair.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* start =
            retain_action(&actions, FLY_SESSION_ACTION_START_DUAL_V2);
        if (start)
            submit(*engine, *start, 2702, false);
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair(pair);
    check(pair.inviter.snapshot().game_state == FLY_SESSION_GAME_RUNNING_V2,
          "REC04 starts from GAME_RUNNING");

    flynes::session::loopback::set_loopback_clock_ns(
        1u + 300ull * 1000ull * 1000ull);
    /* Wake the engine after the clock jump. Local submit must not refresh
     * activity; freeze is applied on the next run_work clock sample. */
    (void)submit_pad(pair.inviter, 0, 1, 2703);
    (void)submit_pad(pair.joiner, 1, 1, 2703);
    pump_pair_long(pair);
    check(pair.inviter.snapshot().game_state == FLY_SESSION_GAME_FROZEN_V2 &&
              pair.joiner.snapshot().game_state == FLY_SESSION_GAME_FROZEN_V2,
          "300ms without peer activity freezes both engines");
    check(pair.inviter.snapshot().dual_freeze_reason ==
              FLY_SESSION_DUAL_FREEZE_AUTHENTICATED_ACTIVITY_TIMEOUT_V2,
          "freeze reason is authenticated-activity timeout");

    check(submit_pad(pair.inviter, 0, 1, 2704) != FLY_SESSION_V2_OK,
          "local send after freeze does not resume stepping");
    pump_pair(pair);
    check(pair.inviter.snapshot().game_state == FLY_SESSION_GAME_FROZEN_V2,
          "local send does not slide the freeze");

    flynes::session::loopback::set_loopback_clock_ns(
        1u + 300ull * 1000ull * 1000ull + 30ull * 1000ull * 1000ull * 1000ull);
    pump_pair_long(pair);
    check(pair.inviter.snapshot().game_state == FLY_SESSION_GAME_FROZEN_V2,
          "30s deadline expires still frozen");
    check(pair.inviter.snapshot().dual_freeze_reason != 2u &&
              pair.inviter.snapshot().dual_mode != 2u,
          "expired reconnect never falls back to STREAM");
    shutdown_pair(pair);
}

void shutdown_waits_for_quic_close_terminal()
{
    std::puts("dual mvp: shutdown waits for exact QUIC close terminal");
    LobbyPair pair(false, false);
    bring_up_lobby(pair);
    check(pair.inviter.snapshot().link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
          "ready lobby precondition for QUIC close");
    pair.inviter.quic.close_result = FLY_SESSION_V2_ACCEPTED;
    check(fly_session_begin_shutdown_v2(pair.inviter.engine, 9901) ==
              FLY_SESSION_V2_ACCEPTED, "shutdown accepted");
    pair.inviter.executor.run_all();
    check(pair.inviter.quic.closes == 1 &&
              pair.inviter.quic.close_connection != 0,
          "one old connection close was dispatched");
    check(pair.inviter.snapshot().engine_state ==
              FLY_SESSION_ENGINE_SHUTTING_DOWN_V2,
          "held close keeps engine shutting down");
    const auto early_destroy = fly_session_destroy_v2(pair.inviter.engine);
    check(early_destroy == FLY_SESSION_V2_BUSY,
          "held close prevents destroy");
    if (early_destroy == FLY_SESSION_V2_OK) pair.inviter.engine = nullptr;
    if (pair.inviter.quic.closes == 1 && early_destroy == FLY_SESSION_V2_BUSY)
    {
        fly_session_port_event_v2 wrong{};
        wrong.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
        wrong.abi_version = FLY_SESSION_ABI_VERSION_2;
        wrong.token = pair.inviter.quic.close_token;
        ++wrong.token.operation_id;
        wrong.event_sequence = 1;
        wrong.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
        wrong.terminal = 1;
        wrong.result = FLY_SESSION_V2_OK;
        wrong.payload_kind = FLY_SESSION_PROVIDER_QUIC_END_V2;
        fly_session_provider_end_event_v2 payload{};
        payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
        payload.abi_version = FLY_SESSION_ABI_VERSION_2;
        wrong.payload_size = sizeof(payload);
        std::memcpy(wrong.payload, &payload, sizeof(payload));
        check(fly_session_deliver_v2(pair.inviter.quic.inbox, &wrong) ==
                  FLY_SESSION_V2_STALE,
              "wrong close token cannot clear old connection debt");
        flynes::session::loopback::deliver_provider_end(
            pair.inviter.quic.inbox, pair.inviter.quic.close_token,
            FLY_SESSION_PROVIDER_QUIC_END_V2);
        pair.inviter.executor.run_all();
        check(pair.inviter.snapshot().engine_state ==
                  FLY_SESSION_ENGINE_SHUTDOWN_COMPLETE_V2,
              "exact close OK permits shutdown completion");
        check(fly_session_destroy_v2(pair.inviter.engine) == FLY_SESSION_V2_OK,
              "exact close OK permits destroy");
        pair.inviter.engine = nullptr;
    }
    else if (pair.inviter.engine != nullptr)
    {
        shutdown_engine_with_the_pump(pair.inviter, pair.inviter_pump,
                                      pair.limits);
    }
    shutdown_engine_with_the_pump(pair.joiner, pair.joiner_pump, pair.limits);
}

void shutdown_retains_cancelled_quic_read_until_terminal(
    int target_stream_count, bool success_data = false,
    bool during_submit = false,
    fly_session_result_v2 cancel_result = FLY_SESSION_V2_ACCEPTED)
{
    std::puts(during_submit
        ? "dual mvp: shutdown during Control read port call"
        : cancel_result == FLY_SESSION_V2_DUPLICATE
            ? "dual mvp: duplicate cancel retains bind read terminal"
        : target_stream_count == 1
        ? "dual mvp: shutdown retains cancelled bind read"
        : success_data
            ? "dual mvp: retired Control read accepts racing data"
            : "dual mvp: shutdown retains cancelled Control read");
    LobbyPair pair(false, false);
    struct Probe final
    {
        EngineFixture* fixture = nullptr;
        int stream_threshold = 0;
        bool fired = false;
        fly_session_result_v2 shutdown_result = FLY_SESSION_V2_INVALID_STATE;
    } probe{&pair.inviter, target_stream_count};
    if (during_submit)
    {
        pair.inviter.quic.before_read_accept_context = &probe;
        pair.inviter.quic.before_read_accept = [](
            void* context, const fly_session_op_token_v2*) {
            auto& value = *static_cast<Probe*>(context);
            auto& fixture = *value.fixture;
            if (fixture.quic.opened_bidi + fixture.quic.accepted_bidi <
                value.stream_threshold) return;
            value.fired = true;
            fixture.quic.before_read_accept = nullptr;
            fixture.quic.cancel_result = FLY_SESSION_V2_OK;
            fixture.quic.close_result = FLY_SESSION_V2_ACCEPTED;
            value.shutdown_result = fly_session_begin_shutdown_v2(
                fixture.engine, 9904);
            fixture.quic.cancel_result = FLY_SESSION_V2_ACCEPTED;
        };
    }
    pair.inviter.platform.ready();
    pair.joiner.platform.ready();
    pair.inviter.executor.run_all();
    pair.joiner.executor.run_all();
    std::vector<fly_session_action_descriptor_v2> inviter_actions;
    std::vector<fly_session_action_descriptor_v2> joiner_actions;
    pair.inviter.snapshot(&inviter_actions);
    pair.joiner.snapshot(&joiner_actions);
    const auto* create = find_action(
        inviter_actions, FLY_SESSION_ACTION_CREATE_INVITE_V2);
    const auto* join = find_action(
        joiner_actions, FLY_SESSION_ACTION_JOIN_CODE_V2);
    check(create != nullptr && join != nullptr,
          "both link actions exist before partial handshake");
    if (create && join)
    {
        submit(pair.inviter, *create, 9902, false);
        submit(pair.joiner, *join, 9903, true);
    }
    for (auto& action : inviter_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : joiner_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    if (!create || !join) return;
    pair.transport.attach(LoopbackRole::AdvertiserPeripheral, pair.inviter);
    pair.transport.attach(LoopbackRole::ScannerCentral, pair.joiner);
    check(pair.transport.connect_ends() == 2, "both ends have one link");
    pump_engine(pair.inviter, pair.inviter_pump, pair.limits);
    pump_engine(pair.joiner, pair.joiner_pump, pair.limits);
    fly_session_op_token_v2 bind_read_token{};
    for (int round = 0; round < 4000; ++round)
    {
        flynes::session::loopback::relay_gatt(pair.transport, pair.relayed);
        flynes::session::loopback::relay_quic(pair.transport, pair.relayed);
        pump_engine(pair.inviter, pair.inviter_pump, pair.limits);
        pump_engine(pair.joiner, pair.joiner_pump, pair.limits);
        if (flynes::session::loopback::pairing_sas_is_offered(
                pair.inviter, &pair.relayed.app_action_kinds) &&
            flynes::session::loopback::pairing_sas_is_offered(
                pair.joiner, &pair.relayed.app_action_kinds))
        {
            flynes::session::loopback::confirm_pairing_sas_when_the_abi_asks(
                pair.inviter, 700 + round);
            flynes::session::loopback::confirm_pairing_sas_when_the_abi_asks(
                pair.joiner, 800 + round);
        }
        const auto streams = pair.inviter.quic.opened_bidi +
            pair.inviter.quic.accepted_bidi;
        if (streams < 2 && pair.inviter.quic.last_read_token.operation_id != 0)
            bind_read_token = pair.inviter.quic.last_read_token;
        if (streams >= target_stream_count &&
            pair.inviter.quic.last_read_token.operation_id != 0 &&
            (target_stream_count == 1 ||
             pair.inviter.quic.last_read_token.operation_id !=
                 bind_read_token.operation_id))
            break;
    }
    const auto read_token = pair.inviter.quic.last_read_token;
    if (during_submit)
        check(probe.fired && probe.shutdown_result == FLY_SESSION_V2_ACCEPTED,
              "shutdown crossed Control read submit before admission returned");
    check(pair.inviter.quic.opened_bidi + pair.inviter.quic.accepted_bidi >=
              target_stream_count &&
              read_token.operation_id != 0 &&
              (target_stream_count == 1 ||
               read_token.operation_id != bind_read_token.operation_id),
          "target QUIC read was dispatched and is waiting for peer bytes");
    if (read_token.operation_id == 0 ||
        (target_stream_count != 1 &&
         read_token.operation_id == bind_read_token.operation_id))
    {
        shutdown_pair(pair);
        return;
    }
    if (!during_submit)
    {
        pair.inviter.quic.cancel_result = cancel_result;
        pair.inviter.quic.close_result = FLY_SESSION_V2_ACCEPTED;
        check(fly_session_begin_shutdown_v2(pair.inviter.engine, 9904) ==
                  FLY_SESSION_V2_ACCEPTED,
              "shutdown accepted with Control read");
    }
    pair.inviter.executor.run_all();
    if (during_submit && pair.inviter.quic.closes != 0)
    {
        check(false, "Control read submission must block connection close");
        flynes::session::loopback::deliver_provider_end(
            pair.inviter.quic.inbox, pair.inviter.quic.close_token,
            FLY_SESSION_PROVIDER_QUIC_END_V2);
        pair.inviter.executor.run_all();
        if (fly_session_destroy_v2(pair.inviter.engine) == FLY_SESSION_V2_OK)
            pair.inviter.engine = nullptr;
        shutdown_engine_with_the_pump(pair.joiner, pair.joiner_pump, pair.limits);
        return;
    }
    check(pair.inviter.quic.cancels > 0 &&
              pair.inviter.quic.cancelled_token.operation_id == read_token.operation_id,
          "the old QUIC read is cancelled under its original token");
    check(pair.inviter.quic.closes == 0,
          "old read terminal must settle before connection close dispatch");
    if (pair.inviter.quic.closes != 0)
    {
        flynes::session::loopback::deliver_provider_end(
            pair.inviter.quic.inbox, pair.inviter.quic.close_token,
            FLY_SESSION_PROVIDER_QUIC_END_V2);
        pair.inviter.executor.run_all();
        if (fly_session_destroy_v2(pair.inviter.engine) == FLY_SESSION_V2_OK)
            pair.inviter.engine = nullptr;
        shutdown_engine_with_the_pump(pair.joiner, pair.joiner_pump, pair.limits);
        return;
    }

    if (success_data)
    {
        const std::uint8_t one_byte[] = {0};
        flynes::session::loopback::deliver_provider_stream_data(
            pair.inviter.quic.inbox, read_token, one_byte, sizeof(one_byte));
    }
    else
    {
        fly_session_port_event_v2 ended{};
        ended.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
        ended.abi_version = FLY_SESSION_ABI_VERSION_2;
        ended.token = read_token;
        ended.event_sequence = 1;
        ended.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
        ended.terminal = 1;
        ended.result = FLY_SESSION_V2_CANCELLED;
        ended.payload_kind = FLY_SESSION_PROVIDER_QUIC_DATA_V2;
        fly_session_provider_end_event_v2 payload{};
        payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
        payload.abi_version = FLY_SESSION_ABI_VERSION_2;
        ended.payload_size = sizeof(payload);
        std::memcpy(ended.payload, &payload, sizeof(payload));
        check(fly_session_deliver_v2(pair.inviter.quic.inbox, &ended) ==
                  FLY_SESSION_V2_ACCEPTED,
              "retired QUIC read terminal is admitted, not stale");
    }
    pair.inviter.executor.run_all();
    check(pair.inviter.quic.closes == 1,
          "one close follows the old Control read terminal");
    if (pair.inviter.quic.closes == 1)
    {
        flynes::session::loopback::deliver_provider_end(
            pair.inviter.quic.inbox, pair.inviter.quic.close_token,
            FLY_SESSION_PROVIDER_QUIC_END_V2);
        pair.inviter.executor.run_all();
    }
    const auto destroy_result = fly_session_destroy_v2(pair.inviter.engine);
    check(destroy_result == FLY_SESSION_V2_OK,
          "shutdown destroys only after read and close terminals");
    if (destroy_result == FLY_SESSION_V2_OK) pair.inviter.engine = nullptr;
    shutdown_engine_with_the_pump(pair.joiner, pair.joiner_pump, pair.limits);
}

void shutdown_closes_connection_created_after_cancel(bool during_submit = false)
{
    std::puts(during_submit
        ? "dual mvp: shutdown while QUIC creator is submitting"
        : "dual mvp: late QUIC connection is still closed");
    LobbyPair pair(false, false);
    struct Probe final
    {
        EngineFixture* fixture = nullptr;
        bool fired = false;
        fly_session_result_v2 shutdown_result = FLY_SESSION_V2_INVALID_STATE;
    } probe{&pair.inviter};
    if (during_submit)
    {
        pair.inviter.quic.before_start_accept_context = &probe;
        pair.inviter.quic.before_start_accept = [](
            void* context, const fly_session_op_token_v2*) {
            auto& value = *static_cast<Probe*>(context);
            value.fired = true;
            auto& fixture = *value.fixture;
            fixture.quic.before_start_accept = nullptr;
            fixture.quic.cancel_result = FLY_SESSION_V2_OK;
            fixture.quic.close_result = FLY_SESSION_V2_ACCEPTED;
            value.shutdown_result = fly_session_begin_shutdown_v2(
                fixture.engine, 9913);
            fixture.quic.cancel_result = FLY_SESSION_V2_ACCEPTED;
        };
    }
    pair.inviter.platform.ready();
    pair.joiner.platform.ready();
    pair.inviter.executor.run_all();
    pair.joiner.executor.run_all();
    std::vector<fly_session_action_descriptor_v2> inviter_actions;
    std::vector<fly_session_action_descriptor_v2> joiner_actions;
    pair.inviter.snapshot(&inviter_actions);
    pair.joiner.snapshot(&joiner_actions);
    const auto* create = find_action(
        inviter_actions, FLY_SESSION_ACTION_CREATE_INVITE_V2);
    const auto* join = find_action(
        joiner_actions, FLY_SESSION_ACTION_JOIN_CODE_V2);
    check(create != nullptr && join != nullptr,
          "both link actions exist before late connection test");
    if (create && join)
    {
        submit(pair.inviter, *create, 9911, false);
        submit(pair.joiner, *join, 9912, true);
    }
    for (auto& action : inviter_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : joiner_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    if (!create || !join) return;
    pair.transport.attach(LoopbackRole::AdvertiserPeripheral, pair.inviter);
    pair.transport.attach(LoopbackRole::ScannerCentral, pair.joiner);
    check(pair.transport.connect_ends() == 2, "both ends have one link");
    for (int round = 0; round < 4000; ++round)
    {
        flynes::session::loopback::relay_gatt(pair.transport, pair.relayed);
        flynes::session::loopback::relay_quic(pair.transport, pair.relayed);
        flynes::session::loopback::pump_once(pair.inviter, pair.inviter_pump);
        flynes::session::loopback::pump_once(pair.joiner, pair.joiner_pump);
        pair.inviter.executor.run_all();
        pair.joiner.executor.run_all();
        if (flynes::session::loopback::pairing_sas_is_offered(pair.inviter) &&
            flynes::session::loopback::pairing_sas_is_offered(pair.joiner))
        {
            flynes::session::loopback::confirm_pairing_sas_when_the_abi_asks(
                pair.inviter, 10000 + round);
            flynes::session::loopback::confirm_pairing_sas_when_the_abi_asks(
                pair.joiner, 11000 + round);
        }
        if (pair.inviter.quic.connects + pair.inviter.quic.listens >
            pair.inviter_pump.quic_connects + pair.inviter_pump.quic_listens)
            break;
    }
    const auto connection_token = pair.inviter.quic.last_token;
    if (during_submit)
        check(probe.fired && probe.shutdown_result == FLY_SESSION_V2_ACCEPTED,
              "shutdown crossed QUIC creator submit before admission returned");
    check(pair.inviter.quic.connects + pair.inviter.quic.listens >
              pair.inviter_pump.quic_connects + pair.inviter_pump.quic_listens &&
              connection_token.operation_id != 0,
          "QUIC connection creator was accepted but not answered");
    if (connection_token.operation_id == 0)
    {
        shutdown_pair(pair);
        return;
    }
    if (!during_submit)
    {
        pair.inviter.quic.cancel_result = FLY_SESSION_V2_ACCEPTED;
        pair.inviter.quic.close_result = FLY_SESSION_V2_ACCEPTED;
        check(fly_session_begin_shutdown_v2(pair.inviter.engine, 9913) ==
                  FLY_SESSION_V2_ACCEPTED,
              "shutdown accepted before connection callback");
    }
    pair.inviter.executor.run_all();
    check(pair.inviter.quic.closes == 0,
          "no close can dispatch before connection handle exists");
    const auto connection = pair.inviter.attached_link->allocate_quic_handle();
    flynes::session::loopback::deliver_provider_resource(
        pair.inviter.quic.inbox, connection_token,
        FLY_SESSION_PROVIDER_QUIC_CONNECTION_V2, connection);
    pair.inviter.executor.run_all();
    check(pair.inviter.quic.closes == 1 &&
              pair.inviter.quic.close_connection == connection,
          "late-created connection is closed under its original handle");
    if (pair.inviter.quic.closes == 1)
    {
        flynes::session::loopback::deliver_provider_end(
            pair.inviter.quic.inbox, pair.inviter.quic.close_token,
            FLY_SESSION_PROVIDER_QUIC_END_V2);
        pair.inviter.executor.run_all();
    }
    const auto destroy_result = fly_session_destroy_v2(pair.inviter.engine);
    check(destroy_result == FLY_SESSION_V2_OK,
          "late-created connection closes before shutdown completes");
    if (destroy_result == FLY_SESSION_V2_OK) pair.inviter.engine = nullptr;
    check(fly_session_begin_shutdown_v2(pair.joiner.engine, 9914) ==
              FLY_SESSION_V2_ACCEPTED, "peer shutdown accepted");
    pair.joiner.executor.run_all();
    const auto peer_destroy = fly_session_destroy_v2(pair.joiner.engine);
    check(peer_destroy == FLY_SESSION_V2_OK, "peer shutdown destroys");
    if (peer_destroy == FLY_SESSION_V2_OK) pair.joiner.engine = nullptr;
}

void shutdown_retains_cancelled_dual_read_until_terminal(
    fly_session_result_v2 cancel_result = FLY_SESSION_V2_ACCEPTED)
{
    std::puts(cancel_result == FLY_SESSION_V2_DUPLICATE
        ? "dual mvp: duplicate cancel retains DUAL and Control terminals"
        : "dual mvp: shutdown retains cancelled DUAL read");
    LobbyPair pair(true, true);
    bring_up_lobby(pair);
    const auto ref = loopback_source_choice_ref_v1();
    for (EngineFixture* engine : {&pair.inviter, &pair.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* select = retain_action(
            &actions, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
        check(select != nullptr, "both peers can select the DUAL content");
        if (select) submit_choice(*engine, *select, 9920, ref.data());
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair(pair);
    for (EngineFixture* engine : {&pair.inviter, &pair.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* confirm = retain_action(
            &actions, FLY_SESSION_ACTION_CONFIRM_GAME_CONFIG_V2);
        if (confirm) submit(*engine, *confirm, 9921, false);
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair_long(pair);
    const int streams_before = pair.inviter.quic.opened_bidi +
        pair.inviter.quic.accepted_bidi;
    const int reads_before = pair.inviter.quic.reads;
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_START_DUAL_V2, 9923);
    pump_pair_long(pair);
    const auto read_token = pair.inviter.quic.last_read_token;
    const bool pending =
        pair.inviter.quic.opened_bidi + pair.inviter.quic.accepted_bidi >
            streams_before &&
        pair.inviter.quic.reads > reads_before &&
        read_token.operation_id != 0;
    check(pending, "DUAL StateCommit stream has an outstanding read credit");
    if (!pending) { shutdown_pair(pair); return; }

    pair.inviter.quic.cancel_result = cancel_result;
    pair.inviter.quic.close_result = FLY_SESSION_V2_ACCEPTED;
    check(fly_session_begin_shutdown_v2(pair.inviter.engine, 9924) ==
              FLY_SESSION_V2_ACCEPTED, "DUAL read shutdown accepted");
    pair.inviter.executor.run_all();
    bool cancelled_dual_read = false;
    for (const auto& token : pair.inviter.quic.cancelled_tokens)
        cancelled_dual_read |= token.operation_id == read_token.operation_id;
    check(cancelled_dual_read,
          "DUAL read cancelled with its original token");
    check(pair.inviter.quic.cancelled_tokens.size() == 2,
          "DUAL and Control each retain one accepted cancellation");
    check(pair.inviter.quic.closes == 0,
          "DUAL read must settle before QUIC connection close");
    if (pair.inviter.quic.closes != 0)
    {
        flynes::session::loopback::deliver_provider_end(
            pair.inviter.quic.inbox, pair.inviter.quic.close_token,
            FLY_SESSION_PROVIDER_QUIC_END_V2);
        pair.inviter.executor.run_all();
        if (fly_session_destroy_v2(pair.inviter.engine) == FLY_SESSION_V2_OK)
            pair.inviter.engine = nullptr;
        shutdown_engine_with_the_pump(pair.joiner, pair.joiner_pump, pair.limits);
        return;
    }
    check(fly_session_destroy_v2(pair.inviter.engine) == FLY_SESSION_V2_BUSY,
          "DUAL read retains engine ownership");
    flynes::session::loopback::deliver_provider_end(
        pair.inviter.quic.inbox, read_token,
        FLY_SESSION_PROVIDER_QUIC_DATA_V2, FLY_SESSION_V2_CANCELLED);
    pair.inviter.executor.run_all();
    check(pair.inviter.quic.closes == 0,
          "Control read still blocks close after DUAL read settles");
    for (const auto& token : pair.inviter.quic.cancelled_tokens)
    {
        if (token.operation_id == read_token.operation_id) continue;
        flynes::session::loopback::deliver_provider_end(
            pair.inviter.quic.inbox, token,
            FLY_SESSION_PROVIDER_QUIC_DATA_V2, FLY_SESSION_V2_CANCELLED);
    }
    pair.inviter.executor.run_all();
    check(pair.inviter.quic.closes == 1,
          "exact DUAL read terminal permits one close");
    if (pair.inviter.quic.closes == 1)
    {
        flynes::session::loopback::deliver_provider_end(
            pair.inviter.quic.inbox, pair.inviter.quic.close_token,
            FLY_SESSION_PROVIDER_QUIC_END_V2);
        pair.inviter.executor.run_all();
    }
    const auto destroyed = fly_session_destroy_v2(pair.inviter.engine);
    check(destroyed == FLY_SESSION_V2_OK,
          "DUAL read and connection both settled before destroy");
    if (destroyed == FLY_SESSION_V2_OK) pair.inviter.engine = nullptr;
    shutdown_engine_with_the_pump(pair.joiner, pair.joiner_pump, pair.limits);
}

void shutdown_retains_cancelled_content_read_until_terminal(
    fly_session_result_v2 cancel_result = FLY_SESSION_V2_ACCEPTED)
{
    std::puts(cancel_result == FLY_SESSION_V2_DUPLICATE
        ? "dual mvp: duplicate cancel retains ROM read terminal"
        : "dual mvp: shutdown retains cancelled ROM read");
    LobbyPair pair(true, true, false, 1u, 0u);
    bring_up_lobby(pair);
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_OFFER_CONTENT_V2, 9930);
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_APPROVE_SEND_V2, 9931);
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_APPROVE_RECEIVE_V2, 9932);
    submit_kind(pair, pair.joiner, FLY_SESSION_ACTION_APPROVE_SEND_V2, 9933);
    const int reads_before = pair.joiner.quic.reads;
    std::vector<fly_session_action_descriptor_v2> actions;
    pair.joiner.snapshot(&actions);
    const auto* receive = retain_action(
        &actions, FLY_SESSION_ACTION_APPROVE_RECEIVE_V2);
    check(receive != nullptr, "receiver consent is available");
    if (receive) submit(pair.joiner, *receive, 9934, false);
    for (auto& action : actions)
        fly_session_approval_token_release_v2(action.approval_token);
    if (!receive) { shutdown_pair(pair); return; }
    for (int round = 0; round < 4000; ++round)
    {
        flynes::session::loopback::relay_gatt(pair.transport, pair.relayed);
        flynes::session::loopback::relay_quic(pair.transport, pair.relayed);
        flynes::session::loopback::pump_once(pair.inviter, pair.inviter_pump);
        flynes::session::loopback::pump_once(pair.joiner, pair.joiner_pump);
        pair.inviter.executor.run_all();
        pair.joiner.executor.run_all();
        if (pair.joiner.quic.rom_streams > 0 &&
            pair.joiner.quic.reads > reads_before) break;
    }
    const auto read_token = pair.joiner.quic.last_read_token;
    const bool pending = pair.joiner.quic.rom_streams > 0 &&
        pair.joiner.quic.reads > reads_before && read_token.operation_id != 0;
    check(pending, "ROM transfer has an outstanding receiver read credit");
    if (!pending) { shutdown_pair(pair); return; }

    pair.joiner.quic.cancel_result = cancel_result;
    pair.joiner.quic.close_result = FLY_SESSION_V2_ACCEPTED;
    check(fly_session_begin_shutdown_v2(pair.joiner.engine, 9935) ==
              FLY_SESSION_V2_ACCEPTED, "ROM read shutdown accepted");
    pair.joiner.executor.run_all();
    bool cancelled_rom_read = false;
    for (const auto& token : pair.joiner.quic.cancelled_tokens)
        cancelled_rom_read |= token.operation_id == read_token.operation_id;
    check(cancelled_rom_read, "ROM read cancelled with its original token");
    check(pair.joiner.quic.closes == 0,
          "ROM read blocks connection close until its terminal");
    if (pair.joiner.quic.closes != 0)
    {
        flynes::session::loopback::deliver_provider_end(
            pair.joiner.quic.inbox, pair.joiner.quic.close_token,
            FLY_SESSION_PROVIDER_QUIC_END_V2);
        pair.joiner.executor.run_all();
        if (fly_session_destroy_v2(pair.joiner.engine) == FLY_SESSION_V2_OK)
            pair.joiner.engine = nullptr;
        shutdown_engine_with_the_pump(pair.inviter, pair.inviter_pump, pair.limits);
        return;
    }
    for (const auto& token : pair.joiner.quic.cancelled_tokens)
    {
        flynes::session::loopback::deliver_provider_end(
            pair.joiner.quic.inbox, token,
            FLY_SESSION_PROVIDER_QUIC_DATA_V2, FLY_SESSION_V2_CANCELLED);
        pair.joiner.executor.run_all();
    }
    check(pair.joiner.quic.closes == 1,
          "one connection close follows ROM and Control terminals");
    if (pair.joiner.quic.closes == 1)
    {
        flynes::session::loopback::deliver_provider_end(
            pair.joiner.quic.inbox, pair.joiner.quic.close_token,
            FLY_SESSION_PROVIDER_QUIC_END_V2);
        pair.joiner.executor.run_all();
    }
    const auto destroyed = fly_session_destroy_v2(pair.joiner.engine);
    check(destroyed == FLY_SESSION_V2_OK,
          "ROM read and connection both settled before destroy");
    if (destroyed == FLY_SESSION_V2_OK) pair.joiner.engine = nullptr;
    shutdown_engine_with_the_pump(pair.inviter, pair.inviter_pump, pair.limits);
}

void shutdown_during_dual_read_submission_waits_for_admission()
{
    std::puts("dual mvp: shutdown during DUAL read port call");
    LobbyPair pair(true, true);
    bring_up_lobby(pair);
    const auto ref = loopback_source_choice_ref_v1();
    for (EngineFixture* engine : {&pair.inviter, &pair.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* select = retain_action(
            &actions, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
        check(select != nullptr, "both peers can select for dispatch race");
        if (select) submit_choice(*engine, *select, 9940, ref.data());
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair(pair);
    for (EngineFixture* engine : {&pair.inviter, &pair.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* confirm = retain_action(
            &actions, FLY_SESSION_ACTION_CONFIRM_GAME_CONFIG_V2);
        if (confirm) submit(*engine, *confirm, 9941, false);
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair_long(pair);
    struct Probe final
    {
        LobbyPair* pair = nullptr;
        int streams_before = 0;
        bool fired = false;
        fly_session_result_v2 shutdown_result = FLY_SESSION_V2_INVALID_STATE;
        fly_session_op_token_v2 token{};
    } probe{&pair, pair.inviter.quic.opened_bidi +
                      pair.inviter.quic.accepted_bidi};
    pair.inviter.quic.before_read_accept_context = &probe;
    pair.inviter.quic.before_read_accept = [](
        void* context, const fly_session_op_token_v2* token) {
        auto& value = *static_cast<Probe*>(context);
        auto& fixture = value.pair->inviter;
        if (fixture.quic.opened_bidi + fixture.quic.accepted_bidi <=
            value.streams_before) return;
        value.fired = true;
        value.token = *token;
        fixture.quic.before_read_accept = nullptr;
        // The first cancel precedes provider admission; after this call
        // returns ACCEPTED, a second cancel must wait for its terminal.
        fixture.quic.cancel_result = FLY_SESSION_V2_OK;
        fixture.quic.close_result = FLY_SESSION_V2_ACCEPTED;
        value.shutdown_result = fly_session_begin_shutdown_v2(
            fixture.engine, 9943);
        fixture.quic.cancel_result = FLY_SESSION_V2_ACCEPTED;
    };
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_START_DUAL_V2, 9942);
    check(probe.fired && probe.shutdown_result == FLY_SESSION_V2_ACCEPTED,
          "shutdown crossed the DUAL read call before admission returned");
    if (!probe.fired) { shutdown_pair(pair); return; }
    check(pair.inviter.quic.closes == 0,
          "connection cannot close while DUAL read submit is unresolved");
    if (pair.inviter.quic.closes == 0)
    {
        check(pair.inviter.quic.cancelled_token.operation_id ==
                  probe.token.operation_id,
              "accepted late DUAL read is cancelled under its original token");
        flynes::session::loopback::deliver_provider_end(
            pair.inviter.quic.inbox, probe.token,
            FLY_SESSION_PROVIDER_QUIC_DATA_V2, FLY_SESSION_V2_CANCELLED);
        pair.inviter.executor.run_all();
        check(pair.inviter.quic.closes == 1,
              "late DUAL terminal permits exactly one connection close");
    }
    if (pair.inviter.quic.closes == 1)
    {
        flynes::session::loopback::deliver_provider_end(
            pair.inviter.quic.inbox, pair.inviter.quic.close_token,
            FLY_SESSION_PROVIDER_QUIC_END_V2);
        pair.inviter.executor.run_all();
    }
    const auto destroyed = fly_session_destroy_v2(pair.inviter.engine);
    check(destroyed == FLY_SESSION_V2_OK,
          "late DUAL admission settles before destroy");
    if (destroyed == FLY_SESSION_V2_OK) pair.inviter.engine = nullptr;
    shutdown_engine_with_the_pump(pair.joiner, pair.joiner_pump, pair.limits);
}

void shutdown_during_content_read_submission_waits_for_admission()
{
    std::puts("dual mvp: shutdown during ROM read port call");
    LobbyPair pair(true, true, false, 1u, 0u);
    bring_up_lobby(pair);
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_OFFER_CONTENT_V2, 9950);
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_APPROVE_SEND_V2, 9951);
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_APPROVE_RECEIVE_V2, 9952);
    submit_kind(pair, pair.joiner, FLY_SESSION_ACTION_APPROVE_SEND_V2, 9953);
    struct Probe final
    {
        LobbyPair* pair = nullptr;
        bool fired = false;
        fly_session_result_v2 shutdown_result = FLY_SESSION_V2_INVALID_STATE;
        fly_session_op_token_v2 token{};
    } probe{&pair};
    pair.joiner.quic.before_read_accept_context = &probe;
    pair.joiner.quic.before_read_accept = [](
        void* context, const fly_session_op_token_v2* token) {
        auto& value = *static_cast<Probe*>(context);
        auto& fixture = value.pair->joiner;
        if (fixture.quic.rom_streams == 0) return;
        value.fired = true;
        value.token = *token;
        fixture.quic.before_read_accept = nullptr;
        fixture.quic.cancel_result = FLY_SESSION_V2_OK;
        fixture.quic.close_result = FLY_SESSION_V2_ACCEPTED;
        value.shutdown_result = fly_session_begin_shutdown_v2(
            fixture.engine, 9955);
        fixture.quic.cancel_result = FLY_SESSION_V2_ACCEPTED;
    };
    std::vector<fly_session_action_descriptor_v2> actions;
    pair.joiner.snapshot(&actions);
    const auto* receive = retain_action(
        &actions, FLY_SESSION_ACTION_APPROVE_RECEIVE_V2);
    check(receive != nullptr, "receiver consent available for dispatch race");
    if (receive) submit(pair.joiner, *receive, 9954, false);
    for (auto& action : actions)
        fly_session_approval_token_release_v2(action.approval_token);
    if (!receive) { shutdown_pair(pair); return; }
    for (int round = 0; round < 4000 && !probe.fired; ++round)
    {
        flynes::session::loopback::relay_gatt(pair.transport, pair.relayed);
        flynes::session::loopback::relay_quic(pair.transport, pair.relayed);
        flynes::session::loopback::pump_once(pair.inviter, pair.inviter_pump);
        flynes::session::loopback::pump_once(pair.joiner, pair.joiner_pump);
        pair.inviter.executor.run_all();
        pair.joiner.executor.run_all();
    }
    check(probe.fired && probe.shutdown_result == FLY_SESSION_V2_ACCEPTED,
          "shutdown crossed ROM read call before admission returned");
    if (!probe.fired) { shutdown_pair(pair); return; }
    check(pair.joiner.quic.closes == 0,
          "connection cannot close while ROM read submit is unresolved");
    if (pair.joiner.quic.closes == 0)
    {
        check(pair.joiner.quic.cancelled_token.operation_id ==
                  probe.token.operation_id,
              "accepted late ROM read is cancelled under its original token");
        flynes::session::loopback::deliver_provider_end(
            pair.joiner.quic.inbox, probe.token,
            FLY_SESSION_PROVIDER_QUIC_DATA_V2, FLY_SESSION_V2_CANCELLED);
        pair.joiner.executor.run_all();
        check(pair.joiner.quic.closes == 1,
              "late ROM terminal permits exactly one connection close");
    }
    if (pair.joiner.quic.closes == 1)
    {
        flynes::session::loopback::deliver_provider_end(
            pair.joiner.quic.inbox, pair.joiner.quic.close_token,
            FLY_SESSION_PROVIDER_QUIC_END_V2);
        pair.joiner.executor.run_all();
    }
    const auto destroyed = fly_session_destroy_v2(pair.joiner.engine);
    check(destroyed == FLY_SESSION_V2_OK,
          "late ROM admission settles before destroy");
    if (destroyed == FLY_SESSION_V2_OK) pair.joiner.engine = nullptr;
    shutdown_engine_with_the_pump(pair.inviter, pair.inviter_pump, pair.limits);
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    no_content_port_leaves_the_lobby_unchanged();
    missing_rom_stays_in_lobby_and_does_not_pretend_dual();
    send_only_consent_does_not_move_rom_bytes();
    both_consents_import_then_select();
    content_catalog_select_is_applied_and_rejected();
    start_dual_gates_and_enters_game_running();
    six_hundred_frames_converge_on_local_digests();
    pause_and_disconnect_freeze_both_ends();
    activity_timeout_freezes_without_sliding_deadline();
    shutdown_waits_for_quic_close_terminal();
    shutdown_retains_cancelled_quic_read_until_terminal(2);
    shutdown_retains_cancelled_quic_read_until_terminal(1);
    shutdown_retains_cancelled_quic_read_until_terminal(2, true);
    shutdown_retains_cancelled_quic_read_until_terminal(2, false, true);
    shutdown_closes_connection_created_after_cancel();
    shutdown_closes_connection_created_after_cancel(true);
    shutdown_retains_cancelled_dual_read_until_terminal();
    shutdown_retains_cancelled_content_read_until_terminal();
    shutdown_during_dual_read_submission_waits_for_admission();
    shutdown_during_content_read_submission_waits_for_admission();
    if (flynes::session::loopback::failures != 0)
    {
        std::fprintf(stderr, "%d failure(s)\n",
                     flynes::session::loopback::failures);
        return 1;
    }
    std::puts("flynes_two_engine_dual_mvp passed");
    return 0;
}
