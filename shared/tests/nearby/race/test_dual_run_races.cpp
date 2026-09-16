/*
 * W2 / Task 6: DUAL consistency races.
 *
 * The same trace must materialize the same canonical bundle and the same state
 * digest whatever order packets arrive in; rollback stays inside the twelve
 * frame ring and freezes past it; and every frozen lifecycle state must refuse
 * step, present and new user input instead of quietly continuing.
 */

#include "dual/dual_run_scheduler.hpp"
#include "dual/dual_state_digest.hpp"
#include "flynes/flynes_runtime.h"
#include "flynes/product/nes_input_bits.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>

namespace {

namespace dual = flynes::session::dual;

using dual::DualFrameOutcomeV1;
using dual::DualInputAdmitV1;
using dual::DualRunSchedulerV1;
using dual::DualSimStateV1;
using dual::DualStateDigestV1;

int failures = 0;

void check(bool value, const char* message)
{
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::array<std::uint8_t, 16> id(std::uint8_t first)
{
    std::array<std::uint8_t, 16> value{};
    value[0] = first;
    return value;
}

std::array<std::uint8_t, 32> key_id(std::uint8_t first)
{
    std::array<std::uint8_t, 32> value{};
    value[0] = first;
    return value;
}

class DualFakeRuntimePortV1 final : public dual::DualRuntimePort
{
public:
    [[nodiscard]] std::uint64_t state_hash() const noexcept { return state_; }

    fly_session_result_v2 load(const dual::DualContentRefV1& content) noexcept override
    {
        if (content.content_hash[0] == 0)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        state_ = kSeed;
        frame_ = 0;
        return FLY_SESSION_V2_OK;
    }

    fly_session_result_v2 step(const dual::DualInputBundleV1& input,
                               DualFrameOutcomeV1* out) noexcept override
    {
        if (out == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        std::uint32_t packed = 0;
        for (std::uint32_t port = 0; port < dual::kDualPortCountV1; ++port) {
            packed |= (input.ports[port].mask & 0xFFu) << (8u * port);
            out->applied_input_sequence[port] =
                input.ports[port].input_sequence;
        }
        state_ = mix(state_ ^ packed, frame_ + 1);
        ++frame_;
        out->frame_index = frame_;
        out->honoured_port_mask = input.predicted_port_mask;
        return FLY_SESSION_V2_OK;
    }

    fly_session_result_v2 export_state(std::uint8_t* out, std::size_t capacity,
                                       std::size_t* out_written,
                                       std::array<std::uint8_t, 32>* out_hash) noexcept override
    {
        if (out == nullptr || out_written == nullptr || out_hash == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        if (capacity < sizeof(std::uint64_t))
            return FLY_SESSION_V2_BUFFER_TOO_SMALL;
        for (std::size_t index = 0; index < sizeof(state_); ++index)
            out[index] = static_cast<std::uint8_t>(state_ >> (8u * index));
        *out_written = sizeof(state_);
        (*out_hash)[0] = static_cast<std::uint8_t>(state_ & 0xFFu);
        return FLY_SESSION_V2_OK;
    }

    fly_session_result_v2 import_state(const std::uint8_t* bytes,
                                       std::size_t size) noexcept override
    {
        if (bytes == nullptr || size != sizeof(std::uint64_t))
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < sizeof(value); ++index)
            value |= static_cast<std::uint64_t>(bytes[index]) << (8u * index);
        state_ = value;
        return FLY_SESSION_V2_OK;
    }

    fly_session_result_v2 state_digest(std::uint64_t frame_index,
                                       DualStateDigestV1* out) noexcept override
    {
        if (out == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        std::uint8_t bytes[sizeof(state_)];
        for (std::size_t index = 0; index < sizeof(state_); ++index)
            bytes[index] = static_cast<std::uint8_t>(state_ >> (8u * index));
        *out = dual::dual_state_digest_v1(bytes, sizeof(bytes), frame_index);
        return FLY_SESSION_V2_OK;
    }

private:
    static constexpr std::uint64_t kSeed = 0x243F6A8885A308D3ull;

    static std::uint64_t mix(std::uint64_t lhs, std::uint64_t rhs) noexcept
    {
        std::uint64_t value = lhs ^ (rhs + 0x9E3779B97F4A7C15ull +
                                     (lhs << 6u) + (lhs >> 2u));
        value ^= value >> 33u;
        value *= 0xFF51AFD7ED558CCDull;
        value ^= value >> 33u;
        return value;
    }

    std::uint64_t state_ = kSeed;
    std::uint64_t frame_ = 0;
};

struct PacketV1
{
    std::uint64_t frame = 0;
    dual::DualInputBundleV1 bundle{};
};

struct EngineV1
{
    DualFakeRuntimePortV1 port{};
    DualRunSchedulerV1 scheduler{port};
    dual::DualInputKeyV1 context{};
    std::array<dual::DualOwnerKeyV1, dual::kDualPortCountV1> owners{};
};

void configure(EngineV1& engine, std::uint8_t session_first,
               std::uint8_t branch_first, std::uint64_t epoch)
{
    engine.context.session_id = id(session_first);
    engine.context.branch_id = id(branch_first);
    engine.context.timeline_epoch = epoch;
    engine.context.seat_revision = 1;
    engine.owners[0] = {0, key_id(0x55)};
    engine.owners[1] = {1, key_id(0x77)};
    /* Two seats are in play; ports 2 and 3 carry an all-zero owner key, which
     * is how the window is told those seats are not participating. */
    engine.owners[2] = {2, std::array<std::uint8_t, 32>{}};
    engine.owners[3] = {3, std::array<std::uint8_t, 32>{}};
}

fly_session_result_v2 start(EngineV1& engine)
{
    dual::DualContentRefV1 content{};
    content.content_hash[0] = 0x42;
    content.session_id = engine.context.session_id;
    content.branch_id = engine.context.branch_id;
    content.timeline_epoch = engine.context.timeline_epoch;
    return engine.scheduler.begin(dual::DualModeV1::Dual, content,
                                  engine.context, engine.owners);
}

std::uint32_t generated_mask(std::uint64_t frame, std::uint64_t seed) noexcept
{
    std::uint64_t value = frame * 6364136223846793005ull + seed;
    value ^= value >> 31u;
    return static_cast<std::uint32_t>(value & 0xFFu);
}

dual::DualInputBundleV1 make_bundle(const EngineV1& engine, std::uint64_t frame,
                                    std::uint32_t seat, std::uint32_t mask) noexcept
{
    dual::DualPortInputArrayV1 samples{};
    for (std::uint32_t port = 0; port < dual::kDualPortCountV1; ++port) {
        samples[port].mask = mask;
        samples[port].sequence = 1 + port;
    }
    auto key = engine.context;
    key.frame_index = frame;
    dual::DualInputBundleV1 bundle{};
    (void)dual::canonical_input_build_v1(
        key, static_cast<std::uint8_t>(seat),
        engine.owners[seat].signing_key_id, samples, &bundle);
    return bundle;
}

/* ------------------------------------------------------------ race cases */

void arrival_order_never_changes_the_frame()
{
    EngineV1 forward;
    EngineV1 backward;
    EngineV1 interleaved;
    configure(forward, 0x11, 0x22, 7);
    configure(backward, 0x11, 0x22, 7);
    configure(interleaved, 0x11, 0x22, 7);
    check(start(forward) == FLY_SESSION_V2_OK, "the forward engine starts");
    check(start(backward) == FLY_SESSION_V2_OK, "the backward engine starts");
    check(start(interleaved) == FLY_SESSION_V2_OK,
          "the interleaved engine starts");

    constexpr std::size_t kFrames = 40;
    /* Every frame's two seat packets are offered in a different relative order
     * and with a different amount of look-ahead, which is exactly the arrival
     * freedom the protocol allows: still inside the guest lead, but never in a
     * fixed seat order. A packet that was already offered by the look-ahead
     * pass reads back as an idempotent duplicate. */
    const auto accepted_or_duplicate = [](fly_session_result_v2 result) {
        return result == FLY_SESSION_V2_ACCEPTED ||
               result == FLY_SESSION_V2_DUPLICATE;
    };
    const auto run_like = [kFrames, &accepted_or_duplicate](EngineV1& engine,
                                                            std::size_t mode) {
        for (std::size_t frame = 0; frame < kFrames; ++frame) {
            if (mode == 2 && frame + 1 < kFrames) {
                const auto next_seat0 = make_bundle(
                    engine, frame + 1, 0, generated_mask(frame + 1, 0x11));
                check(accepted_or_duplicate(
                          engine.scheduler.accept_remote_input(next_seat0,
                                                               nullptr)),
                      "a one frame look-ahead packet is accepted");
            }
            const auto seat0 =
                make_bundle(engine, frame, 0, generated_mask(frame, 0x11));
            const auto seat1 =
                make_bundle(engine, frame, 1, generated_mask(frame, 0x22));
            if (mode != 1) {
                check(accepted_or_duplicate(
                          engine.scheduler.accept_remote_input(seat0, nullptr)),
                      "the first seat packet is accepted");
                check(accepted_or_duplicate(
                          engine.scheduler.accept_remote_input(seat1, nullptr)),
                      "the second seat packet is accepted");
            } else {
                check(accepted_or_duplicate(
                          engine.scheduler.accept_remote_input(seat1, nullptr)),
                      "the second seat packet is accepted first");
                check(accepted_or_duplicate(
                          engine.scheduler.accept_remote_input(seat0, nullptr)),
                      "the first seat packet is accepted second");
            }
            check(engine.scheduler.step_next_frame() == FLY_SESSION_V2_OK,
                  "a staged trace may not fail to step");
        }
    };

    run_like(forward, 0);
    run_like(backward, 1);
    run_like(interleaved, 2);

    check(forward.scheduler.current_frame() == kFrames - 1,
          "the forward engine advanced every frame");
    check(backward.scheduler.current_frame() == kFrames - 1,
          "the seat-reversed engine advanced every frame");
    check(interleaved.scheduler.current_frame() == kFrames - 1,
          "the look-ahead engine advanced every frame");
    check(forward.scheduler.commit_frontier() == kFrames,
          "the forward engine committed every frame");
    check(backward.scheduler.commit_frontier() == kFrames,
          "the seat-reversed engine committed every frame");
    check(interleaved.scheduler.commit_frontier() == kFrames,
          "the look-ahead engine committed every frame");
    check(forward.port.state_hash() == backward.port.state_hash(),
          "seat arrival order never changes the committed state");
    check(forward.port.state_hash() == interleaved.port.state_hash(),
          "look-ahead arrival never changes the committed state");
    check(!forward.scheduler.is_frozen() &&
              !backward.scheduler.is_frozen() &&
              !interleaved.scheduler.is_frozen(),
          "no ordering race freezes the session");
}

void repeated_identical_runs_are_bit_identical()
{
    EngineV1 first;
    EngineV1 second;
    configure(first, 0x11, 0x22, 7);
    configure(second, 0x11, 0x22, 7);
    check(start(first) == FLY_SESSION_V2_OK, "the first engine starts");
    check(start(second) == FLY_SESSION_V2_OK, "the second engine starts");

    for (std::size_t frame = 0; frame < 80; ++frame) {
        for (std::uint32_t seat = 0; seat < 2; ++seat) {
            const auto mask = generated_mask(frame, 0x11u + seat);
            const auto bundle = make_bundle(first, frame, seat, mask);
            check(first.scheduler.accept_remote_input(bundle, nullptr) ==
                      FLY_SESSION_V2_ACCEPTED,
                  "the first engine accepts the packet");
            const auto same = make_bundle(second, frame, seat, mask);
            check(second.scheduler.accept_remote_input(same, nullptr) ==
                      FLY_SESSION_V2_ACCEPTED,
                  "the second engine accepts the same packet");
        }
        const auto first_step = first.scheduler.step_next_frame();
        const auto second_step = second.scheduler.step_next_frame();
        check(first_step == FLY_SESSION_V2_OK &&
                  second_step == FLY_SESSION_V2_OK,
              "both engines step");
        check(first.scheduler.last_digest() == second.scheduler.last_digest(),
              "identical runs produce identical digests");
    }
    check(first.port.state_hash() == second.port.state_hash(),
          "identical runs produce identical committed state");
}

void late_input_and_frozen_states_fail_closed()
{
    /* A trace where the peer's own seat packet lags by one frame: frame f is
     * planned with a prediction for that seat, the real sample lands while f is
     * still inside the rollback ring, and the frame only becomes fully real once
     * that sample arrives. */
    EngineV1 engine;
    configure(engine, 0x11, 0x22, 7);
    check(start(engine) == FLY_SESSION_V2_OK, "the lagged engine starts");

    constexpr std::uint64_t kTotalFrames = 30;
    constexpr std::uint64_t kPeerLagFrames = 1;
    std::deque<PacketV1> peer_queue;
    bool saw_prediction = false;
    bool saw_late_acceptance = false;
    std::uint64_t predicted_frames = 0;
    for (std::uint64_t frame = 0; frame < kTotalFrames; ++frame) {
        /* This engine's own seat is always available for the frame it is about
         * to run. */
        check(engine.scheduler.accept_remote_input(
                  make_bundle(engine, frame, 0, generated_mask(frame, 0x11)),
                  nullptr) == FLY_SESSION_V2_ACCEPTED,
              "the local seat packet is accepted");

        /* The peer's packet for frame f only arrives here, one frame after the
         * runtime first planned f. It is still inside the rollback ring, so it
         * replaces that frame's prediction instead of rewriting history. */
        while (!peer_queue.empty() &&
               peer_queue.front().frame + kPeerLagFrames <= frame) {
            const auto admit =
                engine.scheduler.accept_remote_input(peer_queue.front().bundle,
                                                     nullptr);
            check(admit == FLY_SESSION_V2_ACCEPTED ||
                      admit == FLY_SESSION_V2_DUPLICATE,
                  "a delayed in-ring packet is accepted");
            saw_late_acceptance = true;
            check(engine.scheduler.window().frame_all_real(
                      peer_queue.front().frame),
                  "the late packet makes its frame fully real");
            peer_queue.pop_front();
        }
        peer_queue.push_back(
            {frame, make_bundle(engine, frame, 1, generated_mask(frame, 0x22))});

        const auto step = engine.scheduler.step_next_frame();
        if (step != FLY_SESSION_V2_OK) {
            check(false, "a one frame lag may not freeze");
            break;
        }
        if (engine.scheduler.last_plan().predicted_port_mask != 0) {
            saw_prediction = true;
            ++predicted_frames;
        }
    }
    /* The last frame's peer packet is drained here so the trace finishes fully
     * real. */
    while (!peer_queue.empty()) {
        check(engine.scheduler.accept_remote_input(peer_queue.front().bundle,
                                                   nullptr) ==
                  FLY_SESSION_V2_ACCEPTED,
              "the final delayed packet is accepted");
        check(engine.scheduler.window().frame_all_real(
                  peer_queue.front().frame),
              "the final frame becomes fully real");
        peer_queue.pop_front();
    }
    check(saw_prediction, "the one frame lag produced real predictions");
    check(saw_late_acceptance,
          "late packets inside the ring were accepted, not rejected");
    check(!engine.scheduler.is_frozen(),
          "a one frame lag inside the ring never freezes");
    check(engine.scheduler.state_verified_through() <=
              engine.scheduler.commit_frontier(),
          "the verified watermark never runs past the committed frontier");
    check(predicted_frames > 0 &&
              engine.scheduler.commit_frontier() == kTotalFrames,
          "every frame committed even though some were stepped as predictions");

    /* Rollback is anchored on the verified watermark: the watermark frame is the
     * newest frame a replay may target, anything below it is final, and anything
     * more than the ring behind it is out of reach. */
    std::uint32_t slot = 0;
    const std::uint64_t watermark = engine.scheduler.state_verified_through();
    if (watermark > 0) {
        check(engine.scheduler.rollback_allowed(watermark - 1, &slot) &&
                  slot == 0,
              "the watermark frame itself maps to rollback slot zero");
        check(!engine.scheduler.rollback_allowed(watermark - 2, &slot),
              "history below the watermark is never rollback-able");
        check(!engine.scheduler.rollback_allowed(watermark + 40, &slot),
              "a frame beyond the ring is never rollback-able");
    }
    /* The standalone gate uses the same anchor convention. */
    check(!dual::dual_digest_rollback_allowed_v1(10, 10, 0, &slot),
          "a target at or above the exclusive watermark is refused");
    check(dual::dual_digest_rollback_allowed_v1(11, 10, 0, &slot) && slot == 0,
          "the newest replayable frame maps to slot zero");
    check(dual::dual_digest_rollback_allowed_v1(21, 10, 0, &slot) && slot == 10,
          "ten frames behind the watermark maps to slot ten");
    check(dual::dual_digest_rollback_allowed_v1(22, 10, 0, &slot) && slot == 11,
          "the last frame inside the ring maps to slot eleven");
    check(!dual::dual_digest_rollback_allowed_v1(23, 10, 0, &slot),
          "the first frame outside the ring is refused");
    check(!dual::dual_digest_rollback_allowed_v1(11, 10, 12, &slot),
          "a prediction depth at the ring bound is refused");
    check(!dual::dual_digest_rollback_allowed_v1(11, 10, 0, nullptr),
          "a null slot pointer is refused");

    /* Digest mismatch freezes both engines at the committed actual frame. */
    EngineV1 local;
    EngineV1 peer;
    configure(local, 0x11, 0x22, 7);
    configure(peer, 0x11, 0x22, 7);
    check(start(local) == FLY_SESSION_V2_OK, "the local engine starts");
    check(start(peer) == FLY_SESSION_V2_OK, "the peer engine starts");
    for (std::uint64_t frame = 0; frame < 20; ++frame) {
        for (std::uint32_t seat = 0; seat < 2; ++seat) {
            const auto mask = seat == 0 ? generated_mask(frame, 0x11)
                                        : (frame < 15 ? generated_mask(frame, 0x22)
                                                      : 0x01u);
            const auto local_bundle = make_bundle(local, frame, seat, mask);
            check(local.scheduler.accept_remote_input(local_bundle, nullptr) ==
                      FLY_SESSION_V2_ACCEPTED,
                  "the local engine accepts its packet");
            const auto peer_mask =
                seat == 0 ? generated_mask(frame, 0x11)
                          : (frame < 15 ? generated_mask(frame, 0x22) : 0x02u);
            const auto peer_bundle = make_bundle(peer, frame, seat, peer_mask);
            check(peer.scheduler.accept_remote_input(peer_bundle, nullptr) ==
                      FLY_SESSION_V2_ACCEPTED,
                  "the peer engine accepts its packet");
        }
        check(local.scheduler.step_next_frame() == FLY_SESSION_V2_OK,
              "the local engine steps");
        check(peer.scheduler.step_next_frame() == FLY_SESSION_V2_OK,
              "the peer engine steps");
    }

    /* The peer confirms a diverging digest at the first diverging frame. */
    DualStateDigestV1 diverged = peer.scheduler.last_digest();
    diverged.state[0] = static_cast<std::uint8_t>(diverged.state[0] ^ 0x5Au);
    const auto mismatch = local.scheduler.acknowledge_peer_digest(
        15, diverged, nullptr);
    check(mismatch == FLY_SESSION_V2_INVALID_STATE,
          "a digest mismatch fails closed");
    check(local.scheduler.freeze_reason() ==
              dual::DualFreezeReasonV1::DigestMismatch,
          "the desynchronized freeze reason is recorded");
    check(local.scheduler.is_frozen(), "the mismatched engine is frozen");
    check(local.scheduler.step_next_frame() == FLY_SESSION_V2_INVALID_STATE,
          "a desynchronized engine never steps again");
    check(local.scheduler.present_guard() == FLY_SESSION_V2_INVALID_STATE,
          "a desynchronized engine never presents");
    const auto extra = make_bundle(local, 20, 0, 0xFFu);
    DualInputAdmitV1 out_admit = DualInputAdmitV1::Accepted;
    check(local.scheduler.accept_remote_input(extra, &out_admit) ==
              FLY_SESSION_V2_INVALID_STATE,
          "a desynchronized engine accepts no new input");
    check(out_admit == DualInputAdmitV1::RejectedFrozen,
          "the rejected input admits its frozen reason");
    check(dual::evaluate_dual_mode_v1(dual::DualModeV1::HostStream) ==
              FLY_SESSION_V2_UNAVAILABLE,
          "STREAM is unavailable, so no automatic switch exists");
}

void lifecycle_freezes_stop_everything()
{
    EngineV1 paused;
    configure(paused, 0x11, 0x22, 7);
    check(start(paused) == FLY_SESSION_V2_OK, "the paused engine starts");
    /* Both seats report and their digest is confirmed, so frame zero is a safe
     * point the two engines agree on. */
    check(paused.scheduler.accept_remote_input(
              make_bundle(paused, 0, 0, generated_mask(0, 0x11)), nullptr) ==
              FLY_SESSION_V2_ACCEPTED,
          "the paused engine takes its own frame-zero sample");
    check(paused.scheduler.accept_remote_input(
              make_bundle(paused, 0, 1, generated_mask(0, 0x22)), nullptr) ==
              FLY_SESSION_V2_ACCEPTED,
          "the paused engine takes the peer frame-zero sample");
    check(paused.scheduler.step_next_frame() == FLY_SESSION_V2_OK,
          "the paused engine steps once");
    check(paused.scheduler.acknowledge_peer_digest(
              0, paused.scheduler.last_digest(), nullptr) == FLY_SESSION_V2_OK,
          "frame zero is confirmed by both engines");
    check(paused.scheduler.pause(1000) == FLY_SESSION_V2_OK,
          "the engine pauses");
    check(paused.scheduler.state() == DualSimStateV1::Ready,
          "a paused engine is not running");
    check(paused.scheduler.step_next_frame() == FLY_SESSION_V2_INVALID_STATE,
          "a paused engine never steps");
    check(paused.scheduler.present_guard() == FLY_SESSION_V2_INVALID_STATE,
          "a paused engine never presents");
    DualInputAdmitV1 admit = DualInputAdmitV1::Accepted;
    check(paused.scheduler.accept_remote_input(
              make_bundle(paused, 1, 0, 0x01u), &admit) ==
              FLY_SESSION_V2_INVALID_STATE,
          "a paused engine accepts no new input");
    check(admit == DualInputAdmitV1::RejectedFrozen,
          "the paused rejection is explicit");
    /* Resume demands a safe point both engines confirmed. */
    check(paused.scheduler.resume(2000, paused.scheduler.last_digest()) ==
              FLY_SESSION_V2_OK,
          "resume from a confirmed safe point succeeds");
    /* The resumed frame must also be fully real and confirmed, otherwise the
     * next resume has no safe point to return to. */
    check(paused.scheduler.accept_remote_input(
              make_bundle(paused, 1, 0, generated_mask(1, 0x11)), nullptr) ==
              FLY_SESSION_V2_ACCEPTED,
          "the resumed engine takes its own frame-one sample");
    check(paused.scheduler.accept_remote_input(
              make_bundle(paused, 1, 1, generated_mask(1, 0x22)), nullptr) ==
              FLY_SESSION_V2_ACCEPTED,
          "the resumed engine takes the peer frame-one sample");
    check(paused.scheduler.step_next_frame() == FLY_SESSION_V2_OK,
          "a resumed engine steps again");
    check(paused.scheduler.acknowledge_peer_digest(
              1, paused.scheduler.last_digest(), nullptr) == FLY_SESSION_V2_OK,
          "frame one is confirmed by both engines");
    check(paused.scheduler.pause(3000) == FLY_SESSION_V2_OK,
          "the engine pauses again");
    DualStateDigestV1 wrong{};
    wrong.state[0] = 0x5A;
    check(paused.scheduler.resume(4000, wrong) ==
              FLY_SESSION_V2_PROTOCOL_VIOLATION,
          "resume from a digest the peer did not confirm is refused");
    check(paused.scheduler.is_frozen() == false,
          "a refused resume does not silently freeze");

    EngineV1 surface;
    configure(surface, 0x11, 0x22, 7);
    check(start(surface) == FLY_SESSION_V2_OK, "the surface engine starts");
    check(surface.scheduler.set_surface_available(false) ==
              FLY_SESSION_V2_INVALID_STATE,
          "an unavailable surface freezes the engine");
    check(surface.scheduler.freeze_reason() ==
              dual::DualFreezeReasonV1::TransportTerminal,
          "the surface freeze reason is recorded");
    check(surface.scheduler.step_next_frame() == FLY_SESSION_V2_INVALID_STATE,
          "an engine without a surface never steps");

    EngineV1 audio;
    configure(audio, 0x11, 0x22, 7);
    check(start(audio) == FLY_SESSION_V2_OK, "the audio engine starts");
    check(audio.scheduler.set_audio_available(false) ==
              FLY_SESSION_V2_INVALID_STATE,
          "unavailable audio freezes the engine");
    check(audio.scheduler.present_guard() == FLY_SESSION_V2_INVALID_STATE,
          "an engine without audio never presents");

    EngineV1 network;
    configure(network, 0x11, 0x22, 7);
    check(start(network) == FLY_SESSION_V2_OK, "the network engine starts");
    check(network.scheduler.set_network_available(false) ==
              FLY_SESSION_V2_INVALID_STATE,
          "an unavailable network freezes the engine");
    check(network.scheduler.step_next_frame() == FLY_SESSION_V2_INVALID_STATE,
          "an engine without a network never steps");

    EngineV1 terminal;
    configure(terminal, 0x11, 0x22, 7);
    check(start(terminal) == FLY_SESSION_V2_OK, "the terminal engine starts");
    check(terminal.scheduler.mark_transport_terminal() ==
              FLY_SESSION_V2_INVALID_STATE,
          "a QUIC terminal freezes the engine");
    check(terminal.scheduler.freeze_reason() ==
              dual::DualFreezeReasonV1::TransportTerminal,
          "the terminal freeze reason is recorded");
    check(terminal.scheduler.step_next_frame() == FLY_SESSION_V2_INVALID_STATE,
          "a terminal engine never steps");

    EngineV1 stopped;
    configure(stopped, 0x11, 0x22, 7);
    check(start(stopped) == FLY_SESSION_V2_OK, "the shutdown engine starts");
    check(stopped.scheduler.shutdown() == FLY_SESSION_V2_OK,
          "shutdown succeeds");
    check(stopped.scheduler.state() == DualSimStateV1::Frozen,
          "a shut down engine is frozen");
    check(stopped.scheduler.step_next_frame() == FLY_SESSION_V2_CLOSED,
          "a shut down engine never steps");
    check(stopped.scheduler.present_guard() == FLY_SESSION_V2_CLOSED,
          "a shut down engine never presents");
    check(stopped.scheduler.accept_remote_input(
              make_bundle(stopped, 1, 0, 0x01u), nullptr) ==
              FLY_SESSION_V2_CLOSED,
          "a shut down engine accepts no new input");
    check(stopped.scheduler.pause(5000) == FLY_SESSION_V2_CLOSED,
          "a shut down engine cannot pause");
    check(stopped.scheduler.resume(6000, stopped.scheduler.last_digest()) ==
              FLY_SESSION_V2_CLOSED,
          "a shut down engine cannot resume");

    /* The authenticated-activity timer uses the scheduler's monotonic clock. */
    EngineV1 activity;
    configure(activity, 0x11, 0x22, 7);
    check(start(activity) == FLY_SESSION_V2_OK, "the activity engine starts");
    activity.scheduler.observe_authenticated_activity(1000);
    check(activity.scheduler.step_next_frame() == FLY_SESSION_V2_OK,
          "an active engine steps");
    activity.scheduler.observe_authenticated_activity(999);
    check(activity.scheduler.step_next_frame() == FLY_SESSION_V2_OK,
          "a stale sample never moves the activity clock backwards");
}

} // namespace

int main()
{
    arrival_order_never_changes_the_frame();
    repeated_identical_runs_are_bit_identical();
    late_input_and_frozen_states_fail_closed();
    lifecycle_freezes_stop_everything();

    if (failures != 0) {
        std::fprintf(stderr, "%d dual run race checks failed\n", failures);
        return 1;
    }
    std::puts("dual run race tests passed");
    return 0;
}
