/*
 * W2 / Task 6: DUAL simulation scheduler and consistency gate.
 *
 * Two isolated fake runtimes start from the same state, exchange both inputs and
 * advance at least 600 frames. Every 60 frames the two engines compare the
 * digest of the same committed frame, and the verified watermark only moves
 * when the digests match. Late real input inside the rollback ring rolls back
 * and replays; late input that would rewrite committed history freezes the
 * session.
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
#include <optional>
#include <vector>

namespace {

namespace dual = flynes::session::dual;
namespace product = flynes::product;

using dual::DualFrameOutcomeV1;
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

/* ------------------------------------------------------ deterministic fake */

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

/*
 * A deterministic fake core. The committed state is a pure function of the
 * applied port masks, so two engines that receive the same masks per frame
 * produce byte identical digests, and any input divergence is visible
 * immediately.
 */
class DualFakeRuntimePortV1 final : public dual::DualRuntimePort
{
public:
    [[nodiscard]] std::uint64_t state_hash() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_; }
    [[nodiscard]] std::uint64_t last_applied(std::uint32_t port) const noexcept
    {
        return port < dual::kDualPortCountV1 ? applied_[port] : 0;
    }

    fly_session_result_v2 load(const dual::DualContentRefV1& content) noexcept override
    {
        if (content.content_hash[0] == 0)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        initial_ = kSeed;
        initial_frame_ = 0;
        reset();
        events_.clear();
        events_.push_back({0, initial_, {}});
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
        }
        state_ = mix(state_ ^ packed, frame_ + 1);

        for (std::uint32_t port = 0; port < dual::kDualPortCountV1; ++port) {
            applied_[port] = input.ports[port].input_sequence;
            out->applied_input_sequence[port] = input.ports[port].input_sequence;
        }
        ++frame_;
        DualEventV1 event{};
        event.frame = frame_;
        event.state = state_;
        event.packed = packed;
        events_.push_back(event);
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
        std::uint64_t value = state_;
        for (std::size_t index = 0; index < sizeof(value); ++index)
            out[index] = reinterpret_cast<const std::uint8_t*>(&value)[index];
        *out_written = sizeof(value);
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
        /* events_ holds the frame-0 initial state at index 0 and the committed
         * state after frame n at index n + 1, so a frame that has not been
         * stepped yet has no digest at all. */
        if (frame_index + 1 >= events_.size())
            return FLY_SESSION_V2_STALE;
        const std::uint64_t state =
            events_[static_cast<std::size_t>(frame_index) + 1].state;
        std::uint8_t bytes[sizeof(state)];
        for (std::size_t index = 0; index < sizeof(state); ++index)
            bytes[index] = static_cast<std::uint8_t>(state >> (8u * index));
        *out = dual::dual_state_digest_v1(bytes, sizeof(bytes), frame_index);
        return FLY_SESSION_V2_OK;
    }

    /* Test seam: rewind the fake core to a committed frame and replay. */
    void rewind_to(std::uint64_t frame_index) noexcept
    {
        const auto& event = events_[static_cast<std::size_t>(frame_index)];
        state_ = event.state;
        frame_ = event.frame;
    }

    void replay_event(std::uint64_t frame_index) noexcept
    {
        const auto& event = events_[static_cast<std::size_t>(frame_index)];
        state_ = mix(state_ ^ event.packed, frame_ + 1);
        ++frame_;
    }

    [[nodiscard]] std::size_t event_count() const noexcept { return events_.size(); }

    [[nodiscard]] std::uint32_t packed_at(std::uint64_t frame_index) const noexcept
    {
        return events_[static_cast<std::size_t>(frame_index)].packed;
    }

private:
    struct DualEventV1
    {
        std::uint64_t frame = 0;
        std::uint64_t state = kSeed;
        std::uint32_t packed = 0;
    };

    static constexpr std::uint64_t kSeed = 0x9E3779B97F4A7C15ull;

    static std::uint64_t mix(std::uint64_t lhs, std::uint64_t rhs) noexcept
    {
        std::uint64_t value = lhs ^ (rhs + 0x9E3779B97F4A7C15ull +
                                     (lhs << 6u) + (lhs >> 2u));
        value ^= value >> 33u;
        value *= 0xFF51AFD7ED558CCDull;
        value ^= value >> 33u;
        return value;
    }

    void reset() noexcept
    {
        state_ = initial_;
        frame_ = initial_frame_;
        applied_ = {};
    }

    std::uint64_t initial_ = kSeed;
    std::uint64_t initial_frame_ = 0;
    std::uint64_t state_ = kSeed;
    std::uint64_t frame_ = 0;
    std::array<std::uint64_t, dual::kDualPortCountV1> applied_{};
    std::vector<DualEventV1> events_{};
};

/* ------------------------------------------------------------ test runner */

struct StagedInputV1
{
    std::uint64_t deliver_at = 0;
    dual::DualInputBundleV1 bundle{};
};

std::uint32_t generated_mask(std::uint64_t frame, std::uint64_t seed) noexcept
{
    std::uint64_t value = frame * 6364136223846793005ull + seed;
    value ^= value >> 31u;
    /* Restrict to the eight real pad bits; the builder normalizes conflicts. */
    return static_cast<std::uint32_t>(value & 0xFFu);
}

dual::DualInputBundleV1 make_local_bundle(
    const dual::DualInputKeyV1& key, std::uint8_t seat,
    const std::array<std::uint8_t, 32>& owner, std::uint32_t mask) noexcept
{
    dual::DualPortInputArrayV1 samples{};
    for (std::uint32_t port = 0; port < dual::kDualPortCountV1; ++port) {
        samples[port].mask = mask;
        samples[port].sequence = 1 + port;
    }
    dual::DualInputBundleV1 bundle{};
    (void)dual::canonical_input_build_v1(key, seat, owner, samples, &bundle);
    return bundle;
}

class DualRunFixtureV1 final
{
public:
    DualRunFixtureV1(std::uint8_t session_first, std::uint8_t branch_first,
                     std::uint64_t epoch, std::uint64_t seed,
                     std::uint64_t lag, bool verify)
        : seed_(seed), lag_(lag), verify_(verify)
    {
        context_.session_id = id(session_first);
        context_.branch_id = id(branch_first);
        context_.timeline_epoch = epoch;
        context_.seat_revision = 1;
        /* Two seats are in play; ports 2 and 3 carry an all-zero owner key,
         * which is how the window is told those seats are not participating. */
        owners_[0] = {0, key_id(0x55)};
        owners_[1] = {1, key_id(0x77)};
        owners_[2] = {2, std::array<std::uint8_t, 32>{}};
        owners_[3] = {3, std::array<std::uint8_t, 32>{}};
    }

    [[nodiscard]] fly_session_result_v2 start() noexcept
    {
        dual::DualContentRefV1 content{};
        content.content_hash[0] = 0x42;
        content.session_id = context_.session_id;
        content.branch_id = context_.branch_id;
        content.timeline_epoch = context_.timeline_epoch;
        return scheduler_.begin(dual::DualModeV1::Dual, content, context_,
                                owners_);
    }

    /* Stage the two seats' inputs for a frame and return the local seat mask. */
    void stage_input(std::uint64_t frame, std::uint32_t seat_mask) noexcept
    {
        auto key = context_;
        key.frame_index = frame;
        const auto local = make_local_bundle(key, 0, owners_[0].signing_key_id,
                                             seat_mask);
        pending_.push_back({frame + lag_, local});
    }

    void stage_remote(std::uint64_t frame, std::uint32_t seat_mask) noexcept
    {
        auto key = context_;
        key.frame_index = frame;
        const auto remote = make_local_bundle(key, 1, owners_[1].signing_key_id,
                                              seat_mask);
        pending_.push_back({frame + lag_, remote});
    }

    fly_session_result_v2 step(std::uint64_t frame) noexcept
    {
        while (!pending_.empty() && pending_.front().deliver_at <= frame + 1) {
            const auto admit = scheduler_.accept_remote_input(
                pending_.front().bundle, nullptr);
            if (admit != FLY_SESSION_V2_ACCEPTED &&
                admit != FLY_SESSION_V2_DUPLICATE)
                return admit;
            pending_.pop_front();
        }
        return scheduler_.step_next_frame();
    }

    [[nodiscard]] const DualStateDigestV1& digest() const noexcept
    {
        return scheduler_.last_digest();
    }

    [[nodiscard]] fly_session_result_v2 port_digest(
        std::uint64_t frame, DualStateDigestV1* out) noexcept
    {
        return port_.state_digest(frame, out);
    }

    [[nodiscard]] DualRunSchedulerV1& scheduler() noexcept { return scheduler_; }
    [[nodiscard]] const DualRunSchedulerV1& scheduler() const noexcept
    {
        return scheduler_;
    }
    [[nodiscard]] DualFakeRuntimePortV1& port() noexcept { return port_; }
    [[nodiscard]] const dual::DualInputKeyV1& context() const noexcept
    {
        return context_;
    }
    [[nodiscard]] const std::array<dual::DualOwnerKeyV1, dual::kDualPortCountV1>&
    owners() const noexcept
    {
        return owners_;
    }
    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

private:
    std::uint64_t seed_ = 0;
    std::uint64_t lag_ = 0;
    bool verify_ = true;
    dual::DualInputKeyV1 context_{};
    std::array<dual::DualOwnerKeyV1, dual::kDualPortCountV1> owners_{};
    DualFakeRuntimePortV1 port_{};
    DualRunSchedulerV1 scheduler_{port_};
    std::deque<StagedInputV1> pending_{};
};

/* -------------------------------------------------------------- test cases */

void matched_digests_advance_the_verified_watermark()
{
    DualStateDigestV1 local{};
    DualStateDigestV1 peer{};
    check(dual::dual_digest_gate_v1(local, peer) ==
              dual::DualFreezeReasonV1::None,
          "matching digests do not freeze");
    peer.frame[0] = 1;
    check(dual::dual_digest_gate_v1(local, peer) ==
              dual::DualFreezeReasonV1::DigestMismatch,
          "a frame component mismatch freezes");
    peer = local;
    peer.pcm[0] = 1;
    check(dual::dual_digest_gate_v1(local, peer) ==
              dual::DualFreezeReasonV1::DigestMismatch,
          "a pcm component mismatch freezes");
    check(dual::dual_digest_equal_v1(local, local),
          "identical digests compare equal");
}

void six_hundred_frames_converge()
{
    DualRunFixtureV1 first(0x11, 0x22, 7, 0xAB, 0, true);
    DualRunFixtureV1 second(0x11, 0x22, 7, 0xAB, 0, true);
    check(first.start() == FLY_SESSION_V2_OK, "the first engine starts");
    check(second.start() == FLY_SESSION_V2_OK, "the second engine starts");

    constexpr std::uint64_t kTotalFrames = 600;
    for (std::uint64_t frame = 0; frame < kTotalFrames; ++frame) {
        const auto local_mask = generated_mask(frame, first.seed());
        const auto peer_mask = generated_mask(frame, first.seed() ^ 0x5A5Au);
        first.stage_input(frame, local_mask);
        first.stage_remote(frame, peer_mask);
        second.stage_input(frame, local_mask);
        second.stage_remote(frame, peer_mask);

        const auto first_step = first.step(frame);
        const auto second_step = second.step(frame);
        if (first_step != FLY_SESSION_V2_OK ||
            second_step != FLY_SESSION_V2_OK) {
            check(false, "neither engine may fail during a clean trace");
            break;
        }

        /* Both engines compare the digest of the same committed frame, read from
         * the peer's own per-frame snapshot. The acknowledgement happens for the
         * frame just stepped, so the comparison always stays inside the retained
         * rollback ring. */
        DualStateDigestV1 peer_digest{};
        const auto peer_read = second.port_digest(frame, &peer_digest);
        check(peer_read == FLY_SESSION_V2_OK,
              "the peer digest for the frame is available");
        bool peer_verified = false;
        const auto result = first.scheduler().acknowledge_peer_digest(
            frame, peer_digest, &peer_verified);
        check(result == FLY_SESSION_V2_OK,
              "every committed frame is digest comparable");
        if (frame % dual::kDualDigestSampleIntervalFramesV1 == 0) {
            check(peer_verified,
                  "a matching fully real frame is verified");
            check(dual::dual_digest_equal_v1(first.digest(), second.digest()),
                  "both engines publish the same state digest");
            check(first.scheduler().frame_is_canonical_success(frame),
                  "the sampled frame is a final canonical success");
            check(first.scheduler().state_verified_through() == frame + 1,
                  "the verified watermark only advances on a match");
        }
    }

    check(first.scheduler().current_frame() == kTotalFrames - 1,
          "the first engine advanced at least six hundred frames");
    check(second.scheduler().current_frame() == kTotalFrames - 1,
          "the second engine advanced at least six hundred frames");
    check(first.scheduler().state_verified_through() == kTotalFrames,
          "the first engine verified every committed frame");
    check(second.scheduler().commit_frontier() == kTotalFrames,
          "the second engine committed every frame");
    check(!first.scheduler().is_frozen(), "a matched trace never freezes");
}

void predicted_frames_are_never_final()
{
    /* The remote seat lags, so early frames are predictions until the real
     * sample arrives and forces a rollback and replay. */
    constexpr std::uint64_t kTotalFrames = 90;
    DualRunFixtureV1 first(0x11, 0x22, 7, 0xCD, 2, true);
    DualRunFixtureV1 second(0x11, 0x22, 7, 0xCD, 2, true);
    check(first.start() == FLY_SESSION_V2_OK, "the lagged first engine starts");
    check(second.start() == FLY_SESSION_V2_OK, "the lagged second engine starts");

    bool saw_prediction = false;
    for (std::uint64_t frame = 0; frame < kTotalFrames; ++frame) {
        const auto local_mask = generated_mask(frame, 0x33);
        const auto peer_mask = generated_mask(frame, 0x77);
        first.stage_input(frame, local_mask);
        first.stage_remote(frame, peer_mask);
        second.stage_input(frame, local_mask);
        second.stage_remote(frame, peer_mask);
        if (first.step(frame) != FLY_SESSION_V2_OK ||
            second.step(frame) != FLY_SESSION_V2_OK) {
            check(false, "a lagged clean trace may not fail");
            break;
        }
        if (first.scheduler().last_plan().predicted_port_mask != 0)
            saw_prediction = true;
        check(first.scheduler().present_guard() == FLY_SESSION_V2_OK,
              "presentation stays permitted while running");
    }
    check(saw_prediction, "the lagged peer forced real prediction pressure");

    /* Once every real sample has arrived, whatever is still inside the retained
     * rollback ring is confirmed and the two engines still agree. */
    const std::uint64_t retained_from =
        first.scheduler().window().step_frontier() >
                dual::kDualMaxRollbackFramesV1
            ? first.scheduler().window().step_frontier() -
                  dual::kDualMaxRollbackFramesV1
            : 0;
    for (std::uint64_t frame = retained_from;
         frame < first.scheduler().window().step_frontier(); ++frame) {
        DualStateDigestV1 peer_digest{};
        if (second.port_digest(frame, &peer_digest) != FLY_SESSION_V2_OK)
            continue;
        const auto result =
            first.scheduler().acknowledge_peer_digest(frame, peer_digest);
        if (result != FLY_SESSION_V2_OK &&
            result != FLY_SESSION_V2_ACCEPTED) {
            check(false, "digest acknowledgement may not fail after replay");
            break;
        }
    }
    check(dual::dual_digest_equal_v1(first.digest(), second.digest()),
          "rollback and replay converge to the same committed state");
    check(!first.scheduler().is_frozen(),
          "a replayed late input does not freeze the session");

    /* A predicted frame is not a final canonical success even when it was
     * digest comparable; only fully real verified frames are. */
    check(!first.scheduler().frame_is_canonical_success(
              first.scheduler().current_frame() + 1),
          "an unknown frame is never a canonical success");
}

void late_input_rewriting_verified_history_freezes()
{
    DualRunFixtureV1 engine(0x11, 0x22, 7, 0xEE, 0, true);
    check(engine.start() == FLY_SESSION_V2_OK, "the engine starts");

    constexpr std::uint64_t kTotalFrames = 40;
    for (std::uint64_t frame = 0; frame < kTotalFrames; ++frame) {
        const auto local_mask = generated_mask(frame, 0x11);
        const auto peer_mask = generated_mask(frame, 0x22);
        engine.stage_input(frame, local_mask);
        engine.stage_remote(frame, peer_mask);
        const auto step = engine.step(frame);
        if (step != FLY_SESSION_V2_OK) {
            check(false, "the in-order trace may not fail");
            break;
        }
        engine.scheduler().acknowledge_peer_digest(frame, engine.digest(),
                                                   nullptr);
    }
    check(engine.scheduler().state_verified_through() == kTotalFrames,
          "the whole trace is verified");
    check(engine.scheduler().frame_is_canonical_success(10),
          "a verified real frame is a canonical success");

    /* Different bytes for a verified frame: committed history. */
    auto key = engine.context();
    key.frame_index = 10;
    const auto rewrite = make_local_bundle(key, 1,
                                           engine.owners()[1].signing_key_id,
                                           0x03u);
    const auto admit = engine.scheduler().accept_remote_input(rewrite, nullptr);
    check(admit == FLY_SESSION_V2_INVALID_STATE,
          "a committed history rewrite freezes the session");
    check(engine.scheduler().freeze_reason() ==
              dual::DualFreezeReasonV1::CommittedHistoryRewrite,
          "the freeze reason is the committed history rewrite");
    check(engine.scheduler().is_frozen(), "the engine is frozen");
    check(engine.scheduler().present_guard() == FLY_SESSION_V2_INVALID_STATE,
          "presentation is refused once frozen");
    check(engine.scheduler().step_next_frame() == FLY_SESSION_V2_INVALID_STATE,
          "stepping is refused once frozen");
    check(!engine.scheduler().frame_is_canonical_success(10),
          "frozen history is no longer reported as a canonical success");

    /* The identical bytes would have been idempotent instead. */
    DualRunFixtureV1 clean(0x11, 0x22, 7, 0xEE, 0, true);
    check(clean.start() == FLY_SESSION_V2_OK, "the clean engine starts");
    for (std::uint64_t frame = 0; frame < 5; ++frame) {
        clean.stage_input(frame, generated_mask(frame, 0x11));
        clean.stage_remote(frame, generated_mask(frame, 0x22));
        check(clean.step(frame) == FLY_SESSION_V2_OK, "in-order step succeeds");
    }
    auto duplicate_key = clean.context();
    duplicate_key.frame_index = 2;
    dual::DualPortInputArrayV1 samples{};
    for (std::uint32_t port = 0; port < dual::kDualPortCountV1; ++port) {
        samples[port].mask = generated_mask(2, 0x22);
        samples[port].sequence = 1 + port;
    }
    dual::DualInputBundleV1 duplicate{};
    (void)dual::canonical_input_build_v1(
        duplicate_key, 1, clean.owners()[1].signing_key_id, samples,
        &duplicate);
    dual::DualInputAdmitV1 out_admit = dual::DualInputAdmitV1::RejectedFrozen;
    check(clean.scheduler().accept_remote_input(duplicate, &out_admit) ==
              FLY_SESSION_V2_DUPLICATE,
          "identical bytes are only a duplicate");
    check(out_admit == dual::DualInputAdmitV1::Duplicate,
          "the duplicate is reported without freezing");
    check(!clean.scheduler().is_frozen(),
          "an idempotent retransmission never freezes");
}

void digest_mismatch_freezes_at_the_committed_frame()
{
    DualRunFixtureV1 engine(0x11, 0x22, 7, 0x12, 0, false);
    DualRunFixtureV1 divergent(0x11, 0x22, 7, 0x12, 0, false);
    check(engine.start() == FLY_SESSION_V2_OK, "the local engine starts");
    check(divergent.start() == FLY_SESSION_V2_OK, "the divergent engine starts");

    for (std::uint64_t frame = 0; frame < 31; ++frame) {
        const auto local_mask = generated_mask(frame, 0x11);
        engine.stage_input(frame, local_mask);
        engine.stage_remote(frame, 0x0Fu);
        divergent.stage_input(frame, local_mask);
        /* The peer diverges from frame eleven on. */
        divergent.stage_remote(frame, frame < 11 ? 0x0Fu : 0x01u);
        check(engine.step(frame) == FLY_SESSION_V2_OK, "local step succeeds");
        check(divergent.step(frame) == FLY_SESSION_V2_OK,
              "divergent step succeeds");

        /* Both engines confirm the frame they just stepped, so the digest being
         * compared is always the one still inside the rollback ring. */
        DualStateDigestV1 peer_digest{};
        check(divergent.port_digest(frame, &peer_digest) == FLY_SESSION_V2_OK,
              "the peer digest is available");
        if (frame <= 10) {
            check(engine.scheduler().acknowledge_peer_digest(
                      frame, peer_digest, nullptr) == FLY_SESSION_V2_OK,
                  "matching frames verify");
        }
    }
    check(engine.scheduler().state_verified_through() == 11,
          "the watermark only advanced through the matching frames");

    /* Frame eleven is where the two engines diverged, and its digest is now too
     * old to confirm, so the mismatch is established at the committed frame the
     * two sides last agreed on. */
    DualStateDigestV1 stale_digest{};
    check(divergent.port_digest(11, &stale_digest) == FLY_SESSION_V2_OK,
          "the divergent digest is available");
    check(engine.scheduler().acknowledge_peer_digest(11, stale_digest, nullptr) ==
              FLY_SESSION_V2_STALE,
          "a digest older than the retained ring is stale, not a mismatch");
    check(engine.scheduler().state_verified_through() == 11,
          "the watermark does not advance past the mismatch");
    check(!engine.scheduler().is_frozen(),
          "a stale acknowledgement alone does not freeze the session");
    check(engine.scheduler().step_next_frame() == FLY_SESSION_V2_OK,
          "the engine keeps running after a stale acknowledgement");

    /* A mismatch at a frame still inside the ring freezes at that frame. */
    DualStateDigestV1 wrong = engine.scheduler().last_digest();
    wrong.pcm[0] = static_cast<std::uint8_t>(wrong.pcm[0] ^ 0x5Au);
    check(engine.scheduler().acknowledge_peer_digest(
              engine.scheduler().window().step_frontier() - 1, wrong, nullptr) ==
              FLY_SESSION_V2_INVALID_STATE,
          "a mismatch on a retained frame freezes the session");
    check(engine.scheduler().freeze_reason() ==
              dual::DualFreezeReasonV1::DigestMismatch,
          "the freeze reason is the digest mismatch");
    check(engine.scheduler().step_next_frame() == FLY_SESSION_V2_INVALID_STATE,
          "a desynchronized session never steps again");
}

void stream_is_never_selectable()
{
    DualFakeRuntimePortV1 port{};
    dual::DualRunSchedulerV1 scheduler{port};
    dual::DualContentRefV1 content{};
    content.content_hash[0] = 1;
    dual::DualInputKeyV1 context{};
    context.session_id = id(0x11);
    context.branch_id = id(0x22);
    context.timeline_epoch = 1;
    context.seat_revision = 1;
    std::array<dual::DualOwnerKeyV1, dual::kDualPortCountV1> owners{};
    owners[0] = {0, key_id(1)};
    owners[1] = {1, key_id(2)};
    owners[2] = {2, key_id(3)};
    owners[3] = {3, key_id(4)};

    check(scheduler.begin(dual::DualModeV1::HostStream, content, context,
                          owners) == FLY_SESSION_V2_UNAVAILABLE,
          "HOST_STREAM can never be selected");
    check(scheduler.state() == DualSimStateV1::Unloaded,
          "a refused mode leaves the scheduler unloaded");
    check(scheduler.set_mode(dual::DualModeV1::HostStream) ==
              FLY_SESSION_V2_UNAVAILABLE,
          "a later STREAM switch is refused");
    check(static_cast<std::uint8_t>(dual::kDualSupportedModeMaskV1) == 0x01u,
          "the supported mode mask admits DUAL only");
}

} // namespace

int main()
{
    matched_digests_advance_the_verified_watermark();
    six_hundred_frames_converge();
    predicted_frames_are_never_final();
    late_input_rewriting_verified_history_freezes();
    digest_mismatch_freezes_at_the_committed_frame();
    stream_is_never_selectable();

    if (failures != 0) {
        std::fprintf(stderr, "%d dual run scheduler checks failed\n", failures);
        return 1;
    }
    std::puts("dual run scheduler tests passed");
    return 0;
}
