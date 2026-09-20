/*
 * J1/J2: two public SessionEngines + NestopiaUE + product Quinn on localhost
 * sockets. Discovery/GATT/security remain fixtures, not production pairing. Control
 * and StateCommit bytes must travel through flynes_quic_provider_*; this test
 * never copies application bytes between engines.
 */

#include "../harness/nes_dual_runtime_port.hpp"
#include "../harness/two_engine_loopback_fixture.hpp"
#ifdef FLYNES_TEST_ANDROID_QUIC_ADAPTER
#include "../harness/android_product_quic_adapter.hpp"
#else
#include "../harness/product_quic_port.hpp"
#endif
#include "nes/nes.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <set>
#include <thread>
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
using flynes::session::loopback::relay_gatt;
using flynes::session::loopback::shutdown_engine_with_the_pump;
using flynes::session::loopback::submit;
using flynes::session::loopback::submit_choice;
using flynes::session::nes_port::NesDualRuntimeCAbiV1;
using flynes::session::nes_port::bind_nes_dual_runtime;
using flynes::session::nes_port::read_rom;
using flynes::session::quic_port::ProductQuicPort;

#ifndef FLYNES_ENABLE_RUST_QUIC_PROVIDER
#error "flynes_two_engine_real_quic requires FLYNES_ENABLE_RUST_QUIC_PROVIDER=ON"
#endif

struct LobbyPair final
{
    LoopbackWorld world;
    ProductQuicPort inviter_quic;
    ProductQuicPort joiner_quic;
    fly_session_quic_port_v2 inviter_port{};
    fly_session_quic_port_v2 joiner_port{};
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
        : inviter_port(inviter_quic.port()),
          joiner_port(joiner_quic.port()),
          inviter(world, LoopbackSide::Initiator, true, true, true, 1,
                  &inviter_port),
          joiner(world, LoopbackSide::Responder, true, true, true, 1,
                 &joiner_port)
    {
        flynes::session::loopback::reset_loopback_clock_ns();
        bind_nes_dual_runtime(inviter.dual_runtime.override, inviter_nes);
        bind_nes_dual_runtime(joiner.dual_runtime.override, joiner_nes);
        check(inviter_quic.ready() && joiner_quic.ready() &&
                  inviter_quic.material_handle() != 0 &&
                  joiner_quic.material_handle() != 0,
              "both engines own a live FlynesQuicProvider with TLS material");
        check(!inviter_quic.listen_address().empty() &&
                  !joiner_quic.listen_address().empty(),
              "both engines bound a real Quinn listener on 127.0.0.1");
        install_tls(inviter, inviter_quic);
        install_tls(joiner, joiner_quic);
        std::fprintf(stderr,
                     "real-quic provider=%s inviter_listen=%s joiner_listen=%s\n",
                     inviter_quic.provider_type(),
                     inviter_quic.listen_address().c_str(),
                     joiner_quic.listen_address().c_str());
    }

    static void install_tls(EngineFixture& engine, ProductQuicPort& quic)
    {
        engine.use_tls_der_spki = true;
        engine.use_bearer_endpoint_override = true;
        engine.tls_material_handle_override = quic.material_handle();
        const auto& spki = quic.der_spki();
        check(spki.size() == engine.tls_der_spki.size(),
              "ReadTlsSpki uses the 91-byte DER-SPKI Quinn minted");
        if (spki.size() == engine.tls_der_spki.size())
            std::copy(spki.begin(), spki.end(), engine.tls_der_spki.begin());
        engine.bearer_endpoint_override = quic.listen_endpoint();
    }
};

int pump_round(LobbyPair& pair, std::uint64_t& sas_id)
{
    const int answers_before =
        pair.inviter_pump.counts.answers + pair.joiner_pump.counts.answers;
    const std::size_t gatt_before =
        pair.relayed.peripheral_to_central.delivered.size() +
        pair.relayed.central_to_peripheral.delivered.size();
    relay_gatt(pair.transport, pair.relayed);
    const int drained = pair.inviter_quic.drain() + pair.joiner_quic.drain();
    pump_engine(pair.inviter, pair.inviter_pump, pair.limits);
    pump_engine(pair.joiner, pair.joiner_pump, pair.limits);
    const bool inviter_sas =
        flynes::session::loopback::pairing_sas_is_offered(pair.inviter);
    const bool joiner_sas =
        flynes::session::loopback::pairing_sas_is_offered(pair.joiner);
    bool sas_acted = false;
    if (inviter_sas && joiner_sas)
    {
        const bool inviter_acted =
            flynes::session::loopback::confirm_pairing_sas_when_the_abi_asks(
                pair.inviter, sas_id++);
        const bool joiner_acted =
            flynes::session::loopback::confirm_pairing_sas_when_the_abi_asks(
                pair.joiner, sas_id++);
        sas_acted = inviter_acted || joiner_acted;
    }
    const int answers_after =
        pair.inviter_pump.counts.answers + pair.joiner_pump.counts.answers;
    const std::size_t gatt_after =
        pair.relayed.peripheral_to_central.delivered.size() +
        pair.relayed.central_to_peripheral.delivered.size();
    int work = drained;
    if (answers_after != answers_before)
        ++work;
    if (gatt_after != gatt_before)
        ++work;
    if (sas_acted)
        ++work;
    return work;
}

void pump_real(LobbyPair& pair, std::chrono::milliseconds budget,
               bool wait_pending = true, int max_idle = 80)
{
    const auto deadline = std::chrono::steady_clock::now() + budget;
    int idle = 0;
    std::uint64_t sas_id = 9000;
    while (std::chrono::steady_clock::now() < deadline)
    {
        const int work = pump_round(pair, sas_id);
        const int pending =
            pair.inviter_quic.pending_ops() + pair.joiner_quic.pending_ops();
        if (work != 0 || (wait_pending && pending != 0))
            idle = 0;
        else
            ++idle;
        if (idle >= max_idle)
            return;
        if (work == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void pump_until_committed(LobbyPair& pair, std::uint64_t frame,
                          std::chrono::milliseconds budget)
{
    const auto deadline = std::chrono::steady_clock::now() + budget;
    std::uint64_t sas_id = 9000;
    while (std::chrono::steady_clock::now() < deadline)
    {
        const int work = pump_round(pair, sas_id);
        const auto left = pair.inviter.snapshot();
        const auto right = pair.joiner.snapshot();
        if (pair.inviter_nes.port.steps >= frame + 1u &&
            pair.joiner_nes.port.steps >= frame + 1u &&
            left.dual_frame_index >= frame && right.dual_frame_index >= frame)
            return;
        if (work == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void log_link(const char* tag, EngineFixture& engine)
{
    const auto snap = engine.snapshot();
    std::fprintf(stderr, "%s link=%u game=%u reason=%s\n", tag, snap.link_state,
                 snap.game_state, snap.primary_reason_key);
}

void shutdown_pair(LobbyPair& pair)
{
    shutdown_engine_with_the_pump(pair.inviter, pair.inviter_pump, pair.limits);
    shutdown_engine_with_the_pump(pair.joiner, pair.joiner_pump, pair.limits);
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
    pump_real(pair, std::chrono::seconds(8));
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
          "GATT/discovery is the labeled stub; both ends share one loopback link");

    pump_engine(pair.inviter, pair.inviter_pump, pair.limits);
    pump_engine(pair.joiner, pair.joiner_pump, pair.limits);
    flynes::session::loopback::relay_and_pump_until_idle(
        pair.transport, pair.inviter, pair.inviter_pump, pair.joiner,
        pair.joiner_pump, pair.limits, 4000, 8, &pair.relayed, false, false);
    flynes::session::loopback::relay_and_pump_until_idle(
        pair.transport, pair.inviter, pair.inviter_pump, pair.joiner,
        pair.joiner_pump, pair.limits, 4000, 8, &pair.relayed, true, false);
    pump_real(pair, std::chrono::seconds(30));
    std::fprintf(stderr,
                 "quic stats inviter listen=%d connect=%d inspect=%d export=%d "
                 "streams=%d pending=%d w=%llu r=%llu\n",
                 pair.inviter_quic.listens(), pair.inviter_quic.connects(),
                 pair.inviter_quic.handshake_inspections(),
                 pair.inviter_quic.exporters(), pair.inviter_quic.streams_opened(),
                 pair.inviter_quic.pending_ops(),
                 static_cast<unsigned long long>(pair.inviter_quic.bytes_written()),
                 static_cast<unsigned long long>(pair.inviter_quic.bytes_read()));
    std::fprintf(stderr,
                 "quic stats joiner listen=%d connect=%d inspect=%d export=%d "
                 "streams=%d pending=%d w=%llu r=%llu\n",
                 pair.joiner_quic.listens(), pair.joiner_quic.connects(),
                 pair.joiner_quic.handshake_inspections(),
                 pair.joiner_quic.exporters(), pair.joiner_quic.streams_opened(),
                 pair.joiner_quic.pending_ops(),
                 static_cast<unsigned long long>(pair.joiner_quic.bytes_written()),
                 static_cast<unsigned long long>(pair.joiner_quic.bytes_read()));
    log_link("inviter", pair.inviter);
    log_link("joiner", pair.joiner);

    for (auto& action : inviter_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : joiner_actions)
        fly_session_approval_token_release_v2(action.approval_token);
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
    pump_real(pair, std::chrono::seconds(8));
}

void confirm_both(LobbyPair& pair)
{
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_CONFIRM_GAME_CONFIG_V2,
                2402);
    submit_kind(pair, pair.joiner, FLY_SESSION_ACTION_CONFIRM_GAME_CONFIG_V2,
                2403);
    pump_real(pair, std::chrono::seconds(8));
}

void start_both(LobbyPair& pair)
{
    submit_kind(pair, pair.inviter, FLY_SESSION_ACTION_START_DUAL_V2, 2404);
    submit_kind(pair, pair.joiner, FLY_SESSION_ACTION_START_DUAL_V2, 2405);
    pump_real(pair, std::chrono::seconds(8));
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
          "same-frame digests are not the zero digest of an unproduced state");
    check(inviter_nes.port.pcm_sample_count > 0 &&
              joiner_nes.port.pcm_sample_count > 0 &&
              inviter_nes.port.pcm_sample_count ==
                  joiner_nes.port.pcm_sample_count &&
              inviter_nes.port.pcm_first_sequence ==
                  joiner_nes.port.pcm_first_sequence &&
              inviter_nes.port.pcm_last_media_time_ns ==
                  joiner_nes.port.pcm_last_media_time_ns,
          "PCM sample counts and timeline match on the committed frame");
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
    pump_real(pair, std::chrono::seconds(4), false, 8);
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

void two_engines_real_quic_and_nes()
{
    std::puts("j1/j2: two SessionEngines + NestopiaUE + product Quinn; discovery/GATT/security are fixtures, not app pairing");
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
    const auto after_link_inviter = pair.inviter.snapshot();
    const auto after_link_joiner = pair.joiner.snapshot();
    check(after_link_inviter.link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
              after_link_joiner.link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
          "J1: both engines reached CONNECTED_LOBBY over product Quinn");
    check(pair.inviter_quic.listen_address().find("127.0.0.1:") == 0 &&
              pair.joiner_quic.listen_address().find("127.0.0.1:") == 0,
          "J1: Quinn listeners report real localhost socket addresses");
    check(pair.inviter_quic.handshake_inspections() > 0 &&
              pair.joiner_quic.handshake_inspections() > 0 &&
              pair.inviter_quic.exporters() > 0 &&
              pair.joiner_quic.exporters() > 0,
          "J1: both engines inspected handshake facts and pulled exporters");
    check(pair.inviter_quic.bytes_written() > 0 &&
              pair.joiner_quic.bytes_written() > 0 &&
              pair.inviter_quic.bytes_read() > 0 &&
              pair.joiner_quic.bytes_read() > 0,
          "J1: Control bytes crossed Quinn sockets; fixture did not forward them");
    check(pair.relayed.quic_peripheral_to_central.units == 0 &&
              pair.relayed.quic_central_to_peripheral.units == 0,
          "J1: LoopbackTransport did not relay any QUIC application bytes");

    select_shared_content(pair, 2401);
    confirm_both(pair);
    const auto confirmed_inviter = pair.inviter.snapshot();
    const auto confirmed_joiner = pair.joiner.snapshot();
    check(confirmed_inviter.pending_config_local_confirmed != 0 &&
              confirmed_inviter.pending_config_peer_confirmed != 0 &&
              confirmed_joiner.pending_config_local_confirmed != 0 &&
              confirmed_joiner.pending_config_peer_confirmed != 0,
          "J1: both engines independently confirmed the pending config");
    check(std::memcmp(confirmed_inviter.pending_config_id,
                      confirmed_joiner.pending_config_id, 32u) == 0,
          "J2: both engines bound the same pending_config_id");
    check(std::memcmp(confirmed_inviter.dual_content_hash, rom_hash.data(),
                      32u) == 0 &&
              std::memcmp(confirmed_joiner.dual_content_hash, rom_hash.data(),
                          32u) == 0,
          "J2: bound content identity is the ROM SHA-256");

    start_both(pair);
    check(pair.inviter.dual_runtime.steps == 0 &&
              pair.joiner.dual_runtime.steps == 0,
          "the fake DualRuntime is not the simulation worker");
    check(pair.inviter.snapshot().game_state == FLY_SESSION_GAME_RUNNING_V2 &&
              pair.joiner.snapshot().game_state == FLY_SESSION_GAME_RUNNING_V2,
          "START_DUAL loads NestopiaUE on both SessionEngines");
    check(pair.inviter_quic.streams_opened() >= 2 &&
              pair.joiner_quic.streams_opened() >= 2,
          "J2: Control and StateCommit streams opened on Quinn");
    check(pair.inviter_nes.port.created_sample_rate ==
              FLY_RUNTIME_DEFAULT_SAMPLE_RATE &&
              pair.joiner_nes.port.created_sample_rate ==
                  FLY_RUNTIME_DEFAULT_SAMPLE_RATE,
          "J2: NestopiaUE runtimes use the canonical 48 kHz sample rate");

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
        pump_until_committed(pair, frame, std::chrono::milliseconds(500));
        if ((frame + 1u) % 60u == 0u)
        {
            const auto left = pair.inviter.snapshot();
            const auto right = pair.joiner.snapshot();
            assert_same_committed_frame(left, right, frame, pair.inviter_nes,
                                        pair.joiner_nes);
        }
        if (flynes::session::loopback::failures != 0)
        {
            shutdown_pair(pair);
            return;
        }
    }
    check(p1_edges.size() >= 100u && p2_edges.size() >= 100u,
          "P1 and P2 each produce at least 100 distinguishable input edges");
    check(pair.inviter_nes.port.loads == 1 && pair.joiner_nes.port.loads == 1,
          "both NestopiaUE ports loaded once");
    check(pair.inviter_nes.port.steps >= 600u &&
              pair.joiner_nes.port.steps >= 600u,
          "both NestopiaUE ports stepped at least 600 frames");
    std::fprintf(stderr,
                 "j2 frame=599 core=%s rom_sha256_prefix=%02x%02x provider=%s "
                 "inviter_bytes_w=%llu joiner_bytes_w=%llu pcm_samples=%llu\n",
                 nes_core_version(), rom_hash[0], rom_hash[1],
                 pair.inviter_quic.provider_type(),
                 static_cast<unsigned long long>(pair.inviter_quic.bytes_written()),
                 static_cast<unsigned long long>(pair.joiner_quic.bytes_written()),
                 static_cast<unsigned long long>(
                     pair.inviter_nes.port.pcm_sample_count));

    std::vector<fly_session_action_descriptor_v2> pause_actions;
    pair.inviter.snapshot(&pause_actions);
    const auto* pause =
        find_action(pause_actions, FLY_SESSION_ACTION_PAUSE_GAME_V2);
    check(pause != nullptr, "PAUSE is published while GAME_RUNNING");
    if (pause)
        submit(pair.inviter, *pause, 2506, false);
    pump_real(pair, std::chrono::seconds(8));
    assert_both_frozen_and_frames_stop(
        pair, "pause freezes both NestopiaUE sessions over Quinn");
    for (auto& action : pause_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    shutdown_pair(pair);

    LobbyPair other;
    bring_up_lobby(other);
    select_shared_content(other, 2601);
    confirm_both(other);
    start_both(other);
    std::vector<fly_session_action_descriptor_v2> disconnect_actions;
    other.inviter.snapshot(&disconnect_actions);
    const auto* disconnect =
        find_action(disconnect_actions, FLY_SESSION_ACTION_DISCONNECT_LINK_V2);
    check(disconnect != nullptr, "DISCONNECT is published while GAME_RUNNING");
    if (disconnect)
        submit(other.inviter, *disconnect, 2606, false);
    pump_real(other, std::chrono::seconds(8));
    assert_both_frozen_and_frames_stop(
        other, "disconnect freezes both NestopiaUE sessions over Quinn");
    for (auto& action : disconnect_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    shutdown_pair(other);
}

#ifndef FLYNES_TEST_ANDROID_QUIC_ADAPTER
void actual_pending_read_failure(bool running)
{
    LobbyPair pair;
    bring_up_lobby(pair);
    check(pair.inviter.snapshot().link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
          pair.joiner.snapshot().link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
          "read-failure regression starts from actual connected public engines");
    select_shared_content(pair, 3701);
    confirm_both(pair);
    if (running) {
        start_both(pair);
        check(submit_pad(pair.inviter, 0, 0x08u, 3801) == FLY_SESSION_V2_OK,
              "inviter accepts actual input before read fault");
        check(submit_pad(pair.joiner, 1, 0x10u, 3802) == FLY_SESSION_V2_OK,
              "joiner accepts actual input before read fault");
        pump_until_committed(pair, 0, std::chrono::seconds(8));
        check(pair.inviter_nes.port.steps > 0 && pair.joiner_nes.port.steps > 0,
              "read-failure running case actually stepped both NES cores");
        check(submit_pad(pair.inviter, 0, 0x10u, 3803) == FLY_SESSION_V2_OK,
              "one-sided next input creates a real outstanding StateCommit read");
        pump_real(pair, std::chrono::milliseconds(300), false);
        check(pair.inviter_quic.pending_reads() > 1,
              "fault exercises both control and subordinate DUAL pending reads");
    }
    check(pair.inviter_quic.pending_reads() > 0 && pair.joiner_quic.pending_reads() > 0,
          "both real providers have pending read operations before socket close");
    const int left_secrets = pair.inviter.crypto.releases;
    const int right_secrets = pair.joiner.crypto.releases;
    const int left_cancels = pair.inviter_quic.cancelled_operations;
    if (running)
        check(pair.inviter_quic.prioritize_oldest_read_failure_for_test(),
              "deliver actual control failure before sibling failure to exercise cancellation deterministically");
    check(pair.inviter_quic.close_transport_for_test(),
          "real provider accepts explicit test transport close");
    pump_real(pair, std::chrono::seconds(4), false);
    check(!pair.inviter_quic.read_failures.empty() && !pair.joiner_quic.read_failures.empty(),
          "actual Quinn close produces read errors on both sides");
    for (auto* port : {&pair.inviter_quic, &pair.joiner_quic}) {
        bool admitted = false;
        for (const auto& failure : port->read_failures) {
            std::fprintf(stderr, "actual read terminal result=%d admission=%d op=%llu\n",
                failure.event.result, failure.admission,
                static_cast<unsigned long long>(failure.event.token.operation_id));
            admitted = admitted || failure.admission == FLY_SESSION_V2_ACCEPTED;
            check(failure.admission != FLY_SESSION_V2_CONTRACT_VIOLATION,
                  "actual Quinn DATA failure is never rejected as wrong payload shape");
        }
        check(admitted, "actual Quinn pending read failure reaches the public engine");
        if (!port->read_failures.empty()) {
            const auto replay = port->replay_failure(port->read_failures.front());
            check(replay == FLY_SESSION_V2_DUPLICATE || replay == FLY_SESSION_V2_STALE,
                  "read failure replay is duplicate or fenced after teardown");
            auto late = port->read_failures.front();
            late.event.event_sequence += 1;
            check(port->replay_failure(late) == FLY_SESSION_V2_STALE,
                  "new late event cannot reuse the failed operation token");
        }
    }
    check(pair.inviter.snapshot().link_state == FLY_SESSION_LINK_FAILED_V2 &&
          pair.joiner.snapshot().link_state == FLY_SESSION_LINK_FAILED_V2,
          "actual read errors publish LINK_FAILED before any public shutdown");
    check(pair.inviter.crypto.releases > left_secrets && pair.joiner.crypto.releases > right_secrets,
          "read fault releases owned directional secrets through existing teardown");
    for (auto* engine : {&pair.inviter, &pair.joiner}) {
        std::vector<fly_session_action_descriptor_v2> actions;
        engine->snapshot(&actions);
        check(find_action(actions, FLY_SESSION_ACTION_START_DUAL_V2) == nullptr &&
              find_action(actions, FLY_SESSION_ACTION_CONFIRM_GAME_CONFIG_V2) == nullptr,
              "read fault revokes ready-lobby start and confirmation actions");
        for (auto& action : actions) fly_session_approval_token_release_v2(action.approval_token);
    }
    const auto left_steps = pair.inviter_nes.port.steps;
    const auto right_steps = pair.joiner_nes.port.steps;
    pump_real(pair, std::chrono::milliseconds(300), false);
    check(pair.inviter_nes.port.steps == left_steps && pair.joiner_nes.port.steps == right_steps,
          "read fault prevents further core steps");
    check(pair.inviter_quic.pending_reads() == 0 && pair.joiner_quic.pending_reads() == 0,
          "read fault drains or cancels remaining read operations");
    if (running)
        check(pair.inviter_quic.cancelled_operations > left_cancels,
              "control read fault requests cancellation of the outstanding DUAL operation");
    std::fprintf(stderr, "read-failure running=%d errors=%zu/%zu cancels=%d/%d steps=%llu/%llu\n",
        running ? 1 : 0, pair.inviter_quic.read_failures.size(), pair.joiner_quic.read_failures.size(),
        pair.inviter_quic.cancelled_operations, pair.joiner_quic.cancelled_operations,
        static_cast<unsigned long long>(left_steps), static_cast<unsigned long long>(right_steps));
    shutdown_pair(pair);
    pair.inviter_quic.drain();
    pair.joiner_quic.drain();
}
#endif

} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    const bool read_failure_only = argc == 2 && std::strcmp(argv[1], "--read-failure-only") == 0;
    if (!read_failure_only) two_engines_real_quic_and_nes();
#ifndef FLYNES_TEST_ANDROID_QUIC_ADAPTER
    actual_pending_read_failure(false);
    actual_pending_read_failure(true);
#endif
    if (flynes::session::loopback::failures != 0)
    {
        std::fprintf(stderr, "%d real-Quinn joint checks failed\n",
                     flynes::session::loopback::failures);
        return 1;
    }
    std::puts("two SessionEngines + NestopiaUE + product Quinn passed");
    return 0;
}
