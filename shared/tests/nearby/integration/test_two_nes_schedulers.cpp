/*
 * CP3 L2: two DualRuntimePort instances drive real NestopiaUE. This is not the
 * Fake DualRuntime, and it is not a two-SessionEngine Quinn loop.
 */

#include "dual/canonical_input.hpp"
#include "dual/dual_run_scheduler.hpp"
#include "harness/nes_dual_runtime_port.hpp"
#include "nes/nes.h"
#include "wire/sha256.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

namespace dual = flynes::session::dual;
using flynes::session::nes_port::NesDualRuntimePortV1;
using flynes::session::nes_port::read_rom;

int failures = 0;

void check(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::array<std::uint8_t, 32> owner_id(std::uint8_t first)
{
    std::array<std::uint8_t, 32> value{};
    value[0] = first;
    return value;
}

std::array<dual::DualOwnerKeyV1, dual::kDualPortCountV1> make_owners()
{
    std::array<dual::DualOwnerKeyV1, dual::kDualPortCountV1> owners{};
    owners[0] = {0, owner_id(0x55)};
    owners[1] = {1, owner_id(0x77)};
    owners[2] = {2, {}};
    owners[3] = {3, {}};
    return owners;
}

dual::DualInputKeyV1 make_context(std::uint64_t frame)
{
    dual::DualInputKeyV1 key{};
    key.session_id[0] = 0x11;
    key.branch_id[0] = 0x22;
    key.timeline_epoch = 1;
    key.frame_index = frame;
    key.seat_revision = 1;
    return key;
}

dual::DualInputBundleV1 make_owned_bundle(const dual::DualInputKeyV1& key,
                                          std::uint8_t seat,
                                          const std::array<std::uint8_t, 32>& owner,
                                          std::uint32_t p1, std::uint32_t p2)
{
    dual::DualPortInputArrayV1 samples{};
    samples[0].mask = p1;
    samples[0].sequence = key.frame_index * 4u + 1u;
    samples[1].mask = p2;
    samples[1].sequence = key.frame_index * 4u + 2u;
    samples[2].sequence = key.frame_index * 4u + 3u;
    samples[3].sequence = key.frame_index * 4u + 4u;
    dual::DualInputBundleV1 bundle{};
    (void)dual::canonical_input_build_v1(key, seat, owner, samples, &bundle);
    return bundle;
}

void feed_frame(dual::DualRunSchedulerV1& scheduler,
                const std::array<dual::DualOwnerKeyV1, dual::kDualPortCountV1>& owners,
                std::uint64_t frame, std::uint32_t p1, std::uint32_t p2)
{
    const auto key = make_context(frame);
    const auto local = make_owned_bundle(key, 0, owners[0].signing_key_id, p1, 0);
    const auto remote = make_owned_bundle(key, 1, owners[1].signing_key_id, 0, p2);
    (void)scheduler.accept_remote_input(local, nullptr);
    (void)scheduler.accept_remote_input(remote, nullptr);
}

dual::DualInputBundleV1 make_bundle(std::uint64_t frame, std::uint32_t p1,
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

void two_real_nes_ports_converge_and_show_p1_p2()
{
    std::puts("cp3: two NestopiaUE DualRuntimePorts");
    check(std::strcmp(nes_core_version(), "1.53.2") == 0,
          "the DualRuntimePort is NestopiaUE 1.53.2, not a name-hash stub");
    const auto rom = read_rom();
    check(!rom.empty(), "the runtime ROM fixture is present");
    if (failures != 0)
        return;
    const auto rom_hash = flynes::session::wire::sha256(rom.data(), rom.size());
    NesDualRuntimePortV1 left{};
    NesDualRuntimePortV1 right{};
    dual::DualContentRefV1 content{};
    content.content_hash = rom_hash;
    content.timeline_epoch = 1;
    dual::DualContentRefV1 name_hash{};
    name_hash.content_hash = flynes::session::wire::domain_hash(
        "flynes-dual-core-id-v1",
        reinterpret_cast<const std::uint8_t*>("nestopiaue"),
        sizeof("nestopiaue") - 1u);
    name_hash.timeline_epoch = 1;
    check(left.load(name_hash) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "a nestopiaue name hash is not a ROM content identity");
    check(left.load(content) == FLY_SESSION_V2_OK &&
              right.load(content) == FLY_SESSION_V2_OK,
          "both NestopiaUE ports load the fixture ROM by SHA-256");
    if (failures != 0)
        return;

    dual::DualStateDigestV1 sample{};
    int step_failures = 0;
    for (std::uint32_t frame = 0; frame < 600u; ++frame)
    {
        const std::uint32_t p1 =
            static_cast<std::uint32_t>((frame % 180u) + 1u) & 0xFFu;
        const std::uint32_t p2 =
            static_cast<std::uint32_t>(((frame * 7u) % 180u) + 1u) & 0xFFu;
        const auto bundle = make_bundle(frame, p1, p2);
        dual::DualFrameOutcomeV1 left_out{};
        dual::DualFrameOutcomeV1 right_out{};
        if (left.step(bundle, &left_out) != FLY_SESSION_V2_OK ||
            right.step(bundle, &right_out) != FLY_SESSION_V2_OK)
            ++step_failures;
        if ((frame + 1u) % 60u != 0u)
            continue;
        dual::DualStateDigestV1 a{};
        dual::DualStateDigestV1 b{};
        check(left.state_digest(frame, &a) == FLY_SESSION_V2_OK &&
                  right.state_digest(frame, &b) == FLY_SESSION_V2_OK && a == b,
              "NestopiaUE checkpoint digests match");
        sample = a;
    }
    check(step_failures == 0, "both NestopiaUE cores step the same 600 bundles");
    check(sample.state[0] != 0 || sample.state[1] != 0 || sample.state[31] != 0,
          "the NestopiaUE digest is not all-zero");
    check(sample.pcm != left.checkpoint_count_hash(64) &&
              sample.pcm != right.checkpoint_count_hash(64),
          "PCM digest is not the checkpoint frame_sequence count");
    check(sample.pcm != left.checkpoint_count_hash(72) &&
              sample.pcm != right.checkpoint_count_hash(72),
          "PCM digest is not the checkpoint pcm_producer_sequence count");
    check(sample.pcm[0] != 0 || sample.pcm[1] != 0 || sample.pcm[31] != 0,
          "PCM digest covers produced samples, not silence");

    dual::DualFrameOutcomeV1 left_out{};
    dual::DualFrameOutcomeV1 right_out{};
    check(left.step(make_bundle(600, 0, 0x08u), &left_out) == FLY_SESSION_V2_OK &&
              right.step(make_bundle(600, 0, 0x10u), &right_out) ==
                  FLY_SESSION_V2_OK,
          "divergent P2 still steps");
    dual::DualStateDigestV1 diverge_left{};
    dual::DualStateDigestV1 diverge_right{};
    check(left.state_digest(600, &diverge_left) == FLY_SESSION_V2_OK &&
              right.state_digest(600, &diverge_right) == FLY_SESSION_V2_OK &&
              diverge_left != diverge_right,
          "P2 input changes NestopiaUE state; this is not a name-hash stub");
}

void dual_run_scheduler_steps_two_nes_cores()
{
    std::puts("cp3: DualRunScheduler drives two NestopiaUE ports");
    const auto rom = read_rom();
    check(!rom.empty(), "the runtime ROM fixture is present");
    if (failures != 0)
        return;
    const auto rom_hash = flynes::session::wire::sha256(rom.data(), rom.size());
    NesDualRuntimePortV1 left{};
    NesDualRuntimePortV1 right{};
    dual::DualContentRefV1 content{};
    content.session_id[0] = 0x11;
    content.branch_id[0] = 0x22;
    content.content_hash = rom_hash;
    content.timeline_epoch = 1;
    const auto owners = make_owners();
    const auto context = make_context(0);
    dual::DualRunSchedulerV1 left_sched(left);
    dual::DualRunSchedulerV1 right_sched(right);
    check(left_sched.begin(dual::DualModeV1::Dual, content, context, owners) ==
              FLY_SESSION_V2_OK &&
              right_sched.begin(dual::DualModeV1::Dual, content, context,
                                owners) == FLY_SESSION_V2_OK,
          "DualRunScheduler loads NestopiaUE from the ROM SHA-256");
    check(left_sched.announce_running() == FLY_SESSION_V2_OK &&
              right_sched.announce_running() == FLY_SESSION_V2_OK,
          "both schedulers announce Running");
    if (failures != 0)
        return;

    dual::DualStateDigestV1 sample{};
    for (std::uint32_t frame = 0; frame < 600u; ++frame)
    {
        const std::uint32_t p1 =
            static_cast<std::uint32_t>((frame % 180u) + 1u) & 0xFFu;
        const std::uint32_t p2 =
            static_cast<std::uint32_t>(((frame * 7u) % 180u) + 1u) & 0xFFu;
        feed_frame(left_sched, owners, frame, p1, p2);
        feed_frame(right_sched, owners, frame, p1, p2);
        check(left_sched.step_next_frame() == FLY_SESSION_V2_OK &&
                  right_sched.step_next_frame() == FLY_SESSION_V2_OK,
              "DualRunScheduler steps NestopiaUE using state_digest, not a "
              "1024-byte checkpoint export");
        if (failures != 0)
            return;
        if ((frame + 1u) % 60u != 0u)
            continue;
        check(left_sched.last_digest() == right_sched.last_digest(),
              "scheduler NestopiaUE digests match every 60 frames");
        dual::DualStateDigestV1 port{};
        check(left.state_digest(frame, &port) == FLY_SESSION_V2_OK &&
                  left_sched.last_digest() == port,
              "scheduler last_digest is the NestopiaUE port digest");
        sample = port;
    }
    check(sample.pcm != left.checkpoint_count_hash(64) &&
              sample.pcm != left.checkpoint_count_hash(72),
          "scheduler PCM digest is pulled samples, not checkpoint counts");
}

} // namespace

int main()
{
    two_real_nes_ports_converge_and_show_p1_p2();
    dual_run_scheduler_steps_two_nes_cores();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d two-nes port checks failed\n", failures);
        return 1;
    }
    std::puts("flynes_two_nes_schedulers passed");
    return 0;
}
