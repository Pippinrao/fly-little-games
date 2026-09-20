/*
 * CP3: two public SessionEngines drive NestopiaUE DualRuntimePorts. Catalog
 * identity is the ROM SHA-256; the fake DualRuntime is not the worker.
 */

#include "../harness/nes_dual_runtime_port.hpp"
#include "../harness/two_engine_loopback_fixture.hpp"
#include "nes/nes.h"

#include <cstdio>
#include <cstring>
#include <set>
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
using flynes::session::loopback::loopback_content_id_v1;
using flynes::session::loopback::loopback_source_choice_ref_v1;
using flynes::session::loopback::pump_engine;
using flynes::session::loopback::relay_and_pump_until_idle;
using flynes::session::loopback::shutdown_engine_with_the_pump;
using flynes::session::loopback::submit;
using flynes::session::loopback::submit_choice;
using flynes::session::nes_port::NesDualRuntimeCAbiV1;
using flynes::session::nes_port::NesDualRuntimePortV1;
using flynes::session::nes_port::bind_nes_dual_runtime;
using flynes::session::nes_port::read_rom;

namespace dual = flynes::session::dual;

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
    NesDualRuntimeCAbiV1 inviter_nes;
    NesDualRuntimeCAbiV1 joiner_nes;

    LobbyPair()
        : inviter(world, LoopbackSide::Initiator, true, true, true, 1),
          joiner(world, LoopbackSide::Responder, true, true, true, 1)
    {
        flynes::session::loopback::reset_loopback_clock_ns();
        bind_nes_dual_runtime(inviter.dual_runtime.override, inviter_nes);
        bind_nes_dual_runtime(joiner.dual_runtime.override, joiner_nes);
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

void submit_kind(LobbyPair& pair, EngineFixture& fixture, std::uint32_t kind,
                 std::uint64_t request_id)
{
    std::vector<fly_session_action_descriptor_v2> actions;
    fixture.snapshot(&actions);
    const auto* action = find_action(actions, kind);
    check(action != nullptr, "required content action is published");
    if (action)
        submit(fixture, *action, request_id, false);
    for (auto& item : actions)
        fly_session_approval_token_release_v2(item.approval_token);
    pump_pair(pair);
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

void select_shared_content(LobbyPair& pair, std::uint64_t request_id)
{
    const auto ref = loopback_source_choice_ref_v1();
    for (EngineFixture* engine : {&pair.inviter, &pair.joiner})
    {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        const auto* select =
            find_action(actions, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
        check(select != nullptr, "both engines publish SELECT_CONTENT");
        if (select)
            submit_choice(*engine, *select, request_id, ref.data());
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
    }
    pump_pair(pair);
}

void confirm_both_via_control(LobbyPair& pair, std::uint64_t inviter_id,
                              std::uint64_t joiner_id)
{
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_CONFIRM_GAME_CONFIG_V2,
                inviter_id);
    submit_kind(pair, pair.joiner, FLY_SESSION_ACTION_CONFIRM_GAME_CONFIG_V2,
                joiner_id);
    pump_pair_long(pair);
}

void start_both(LobbyPair& pair, std::uint64_t inviter_id,
                std::uint64_t joiner_id)
{
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_START_DUAL_V2, inviter_id);
    submit_kind(pair, pair.joiner, FLY_SESSION_ACTION_START_DUAL_V2, joiner_id);
    pump_pair_long(pair);
}

bool digest_nonzero(const std::uint8_t* bytes)
{
    for (std::size_t i = 0; i < 32u; ++i)
    {
        if (bytes[i] != 0)
            return true;
    }
    return false;
}

dual::DualInputBundleV1 make_local_bundle(std::uint64_t frame, std::uint32_t p1,
                                          std::uint32_t p2)
{
    dual::DualInputBundleV1 bundle{};
    bundle.key.session_id[0] = 0x11;
    bundle.key.branch_id[0] = 0x22;
    bundle.key.timeline_epoch = 1;
    bundle.key.frame_index = frame;
    bundle.key.seat_revision = 1;
    bundle.ports[0].mask = p1;
    bundle.ports[1].mask = p2;
    for (std::uint32_t port = 0; port < dual::kDualPortCountV1; ++port)
        bundle.ports[port].input_sequence = frame * 4u + port + 1u;
    return bundle;
}

void p1_and_p2_change_nestopiaue_state()
{
    const auto rom = read_rom();
    check(!rom.empty(), "the runtime ROM fixture is present for P1/P2");
    if (rom.empty())
        return;
    const auto rom_hash = flynes::session::wire::sha256(rom.data(), rom.size());
    NesDualRuntimePortV1 left{};
    NesDualRuntimePortV1 right{};
    dual::DualContentRefV1 content{};
    content.content_hash = rom_hash;
    content.timeline_epoch = 1;
    check(left.load(content) == FLY_SESSION_V2_OK &&
              right.load(content) == FLY_SESSION_V2_OK,
          "P1/P2 observable ports load the fixture ROM");
    for (std::uint32_t frame = 0; frame < 600u; ++frame)
    {
        const std::uint32_t p1 =
            static_cast<std::uint32_t>((frame % 180u) + 1u) & 0xFFu;
        const std::uint32_t p2 =
            static_cast<std::uint32_t>(((frame * 7u) % 180u) + 1u) & 0xFFu;
        dual::DualFrameOutcomeV1 left_out{};
        dual::DualFrameOutcomeV1 right_out{};
        check(left.step(make_local_bundle(frame, p1, p2), &left_out) ==
                  FLY_SESSION_V2_OK &&
                  right.step(make_local_bundle(frame, p1, p2), &right_out) ==
                      FLY_SESSION_V2_OK,
              "P1/P2 observable ports share 600 matching frames");
        if (flynes::session::loopback::failures != 0)
            return;
    }
    dual::DualFrameOutcomeV1 left_out{};
    dual::DualFrameOutcomeV1 right_out{};
    check(left.step(make_local_bundle(600, 0x08u, 0), &left_out) ==
              FLY_SESSION_V2_OK &&
              right.step(make_local_bundle(600, 0, 0x10u), &right_out) ==
                  FLY_SESSION_V2_OK,
          "P1-only and P2-only still step after the shared warmup");
    dual::DualStateDigestV1 diverge_left{};
    dual::DualStateDigestV1 diverge_right{};
    check(left.state_digest(600, &diverge_left) == FLY_SESSION_V2_OK &&
              right.state_digest(600, &diverge_right) == FLY_SESSION_V2_OK &&
              diverge_left != diverge_right,
          "P1 and P2 each change NestopiaUE state; input kinds are not enough");
}

void assert_same_committed_frame(const fly_session_snapshot_v2& left,
                                 const fly_session_snapshot_v2& right,
                                 std::uint64_t committed_frame,
                                 NesDualRuntimeCAbiV1& inviter_nes,
                                 NesDualRuntimeCAbiV1& joiner_nes)
{
    check(left.dual_frame_index == committed_frame &&
              right.dual_frame_index == committed_frame,
          "both engines publish the same committed simulation frame");
    check(std::memcmp(left.dual_state_digest, right.dual_state_digest, 32u) == 0 &&
              std::memcmp(left.dual_frame_digest, right.dual_frame_digest, 32u) ==
                  0 &&
              std::memcmp(left.dual_pcm_digest, right.dual_pcm_digest, 32u) == 0,
          "same-frame state/frame/PCM digests match");
    check(digest_nonzero(left.dual_state_digest) &&
              digest_nonzero(left.dual_frame_digest) &&
              digest_nonzero(left.dual_pcm_digest),
          "committed digests are not unproduced zero values");
    check(left.dual_pcm_digest[0] != 0 || left.dual_pcm_digest[1] != 0 ||
              left.dual_pcm_digest[31] != 0,
          "PCM digest covers produced samples");
    const auto count_hash = inviter_nes.port.checkpoint_count_hash(64);
    const auto producer_hash = inviter_nes.port.checkpoint_count_hash(72);
    check(std::memcmp(left.dual_pcm_digest, count_hash.data(), 32u) != 0 &&
              std::memcmp(left.dual_pcm_digest, producer_hash.data(), 32u) != 0,
          "PCM digest is not a checkpoint count hash");
    check(inviter_nes.port.pcm_sample_count > 0 &&
              inviter_nes.port.pcm_sample_count ==
                  joiner_nes.port.pcm_sample_count &&
              inviter_nes.port.pcm_first_sequence ==
                  joiner_nes.port.pcm_first_sequence &&
              inviter_nes.port.pcm_last_media_time_ns ==
                  joiner_nes.port.pcm_last_media_time_ns,
          "PCM sample count and timeline match on both NES ports");
    std::fprintf(stdout,
                 "joint frame=%llu core=%s pcm_samples=%llu first_seq=%llu "
                 "media_ns=%llu\n",
                 static_cast<unsigned long long>(committed_frame),
                 nes_core_version(),
                 static_cast<unsigned long long>(inviter_nes.port.pcm_sample_count),
                 static_cast<unsigned long long>(
                     inviter_nes.port.pcm_first_sequence),
                 static_cast<unsigned long long>(
                     inviter_nes.port.pcm_last_media_time_ns));
}

void assert_both_frozen_and_frames_stop(LobbyPair& pair, const char* why)
{
    const auto left = pair.inviter.snapshot();
    const auto right = pair.joiner.snapshot();
    check(left.game_state == FLY_SESSION_GAME_FROZEN_V2 &&
              right.game_state == FLY_SESSION_GAME_FROZEN_V2,
          why);
    const auto frozen_left = left.dual_frame_index;
    const auto frozen_right = right.dual_frame_index;
    const auto steps_left = pair.inviter_nes.port.steps;
    const auto steps_right = pair.joiner_nes.port.steps;
    check(frozen_left == frozen_right, "frozen engines share a committed frame");
    (void)submit_pad(pair.inviter, 0, 0x08u, 9001);
    (void)submit_pad(pair.joiner, 1, 0x10u, 9002);
    pump_pair(pair);
    const auto after_left = pair.inviter.snapshot();
    const auto after_right = pair.joiner.snapshot();
    check(after_left.game_state == FLY_SESSION_GAME_FROZEN_V2 &&
              after_right.game_state == FLY_SESSION_GAME_FROZEN_V2 &&
              after_left.dual_frame_index == frozen_left &&
              after_right.dual_frame_index == frozen_right,
          "frozen engines do not advance the committed frame");
    check(pair.inviter_nes.port.steps == steps_left &&
              pair.joiner_nes.port.steps == steps_right,
          "frozen NestopiaUE ports do not step after freeze");
}

void two_session_engines_run_nestopiaue()
{
    std::puts("cp3: two SessionEngines + NestopiaUE");
    check(std::strcmp(nes_core_version(), "1.53.2") == 0,
          "the DualRuntimePort is NestopiaUE 1.53.2");
    const auto rom = read_rom();
    check(!rom.empty(), "the runtime ROM fixture is present");
    const auto rom_hash = flynes::session::wire::sha256(rom.data(), rom.size());
    const auto catalog = loopback_content_id_v1();
    check(catalog == rom_hash,
          "the catalog content identity is the ROM SHA-256, not 0xD1 fill");

    LobbyPair pair;
    bring_up_lobby(pair);
    select_shared_content(pair, 2401);
    confirm_both_via_control(pair, 2402, 2403);
    start_both(pair, 2404, 2405);

    check(pair.inviter.dual_runtime.steps == 0 &&
              pair.joiner.dual_runtime.steps == 0,
          "the fake DualRuntime is not the simulation worker");
    check(pair.inviter.snapshot().game_state == FLY_SESSION_GAME_RUNNING_V2 &&
              pair.joiner.snapshot().game_state == FLY_SESSION_GAME_RUNNING_V2,
          "START_DUAL loads NestopiaUE on both SessionEngines");

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
            assert_same_committed_frame(left, right, frame, pair.inviter_nes,
                                        pair.joiner_nes);
            check(left.game_state == FLY_SESSION_GAME_RUNNING_V2 &&
                      right.game_state == FLY_SESSION_GAME_RUNNING_V2,
                  "the NES run stays GAME_RUNNING");
        }
        if (flynes::session::loopback::failures != 0)
        {
            shutdown_pair(pair);
            return;
        }
    }
    check(p1_edges.size() >= 100u && p2_edges.size() >= 100u,
          "P1 and P2 each produce at least 100 distinguishable input edges");
    p1_and_p2_change_nestopiaue_state();
    check(pair.inviter_nes.port.loads == 1 && pair.joiner_nes.port.loads == 1,
          "both NestopiaUE ports loaded once");
    check(pair.inviter_nes.port.steps >= 600u &&
              pair.joiner_nes.port.steps >= 600u,
          "both NestopiaUE ports stepped at least 600 frames");
    /* Healthy pause/resume and end-second-game are J3. FROZEN here only proves
     * both engines stop stepping; it is not a complete pause/resume loop. */

    std::vector<fly_session_action_descriptor_v2> pause_actions;
    pair.inviter.snapshot(&pause_actions);
    const auto* pause =
        find_action(pause_actions, FLY_SESSION_ACTION_PAUSE_GAME_V2);
    check(pause != nullptr, "PAUSE is published while GAME_RUNNING");
    if (pause)
        submit(pair.inviter, *pause, 2506, false);
    pump_pair(pair);
    assert_both_frozen_and_frames_stop(
        pair, "pause freezes both NestopiaUE sessions");
    for (auto& action : pause_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    shutdown_pair(pair);

    LobbyPair other;
    bring_up_lobby(other);
    select_shared_content(other, 2601);
    confirm_both_via_control(other, 2602, 2603);
    start_both(other, 2604, 2605);
    std::vector<fly_session_action_descriptor_v2> disconnect_actions;
    other.inviter.snapshot(&disconnect_actions);
    const auto* disconnect =
        find_action(disconnect_actions, FLY_SESSION_ACTION_DISCONNECT_LINK_V2);
    check(disconnect != nullptr, "DISCONNECT is published while GAME_RUNNING");
    if (disconnect)
        submit(other.inviter, *disconnect, 2606, false);
    pump_pair(other);
    assert_both_frozen_and_frames_stop(
        other, "disconnect freezes both NestopiaUE sessions");
    for (auto& action : disconnect_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    shutdown_pair(other);
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    two_session_engines_run_nestopiaue();
    if (flynes::session::loopback::failures != 0)
    {
        std::fprintf(stderr, "%d failure(s)\n",
                     flynes::session::loopback::failures);
        return 1;
    }
    std::puts("flynes_two_engine_nes_joint passed");
    return 0;
}
