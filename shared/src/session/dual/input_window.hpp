#ifndef FLYNES_SESSION_DUAL_INPUT_WINDOW_HPP
#define FLYNES_SESSION_DUAL_INPUT_WINDOW_HPP

/*
 * W2 / Task 5: bounded DUAL input window.
 *
 * The window is the per-frame authority for input reality in one engine, and it
 * deliberately separates two frontiers that move at different speeds:
 *
 *   - `step_frontier_`  - the next frame the runtime will execute. Only the
 *     scheduler moves it, one frame per step.
 *   - `commit_frontier_` - the first frame whose inputs are not all confirmed.
 *     Every frame below it is fully real and therefore immutable history.
 *
 * Capacity is anchored on the STEP frontier, not on the committed watermark:
 * the ring only ever holds frames still in flight, which are bounded by the
 * frozen prediction depth (how far the runtime may run ahead of the watermark)
 * plus the guest lead (how far ahead of the runtime an input may arrive). That
 * is what keeps arrival speed and step speed from fighting over ring slots.
 *
 * A record is reused only once its frame is below the committed watermark, so a
 * delivered frame can never be lost while it is still in flight.
 *
 * Semantics:
 *   - at most one confirmed sample per (frame, port), and a port is confirmed
 *     only by the bundle its OWN seat signed, so one player's view can never
 *     masquerade as another player's input;
 *   - an impossible d-pad state never enters the window: the canonical builder
 *     clears opposing pairs before the bundle exists;
 *   - identical key plus identical bytes is idempotent; identical key with
 *     different bytes for a frame that is not yet committed is rejected, and for
 *     a frame at or below the watermark it is committed-history equivocation;
 *   - a missing in-play port is predicted from the last known complete state of
 *     the seat that owns it and is always flagged in predicted_port_mask. A
 *     frame carrying a prediction is never a final canonical success;
 *   - prediction depth ten freezes and nothing may exceed the hard twelve frame
 *     ring bound, both taken verbatim from the frozen W0 contract.
 */

#include "canonical_input.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace flynes::session::dual {

/* The session's seat owners. one key id per logical seat. */
struct DualOwnerKeyV1 final
{
    std::uint8_t seat = 0;
    std::array<std::uint8_t, 32> signing_key_id{};
};

inline bool operator==(const DualOwnerKeyV1& lhs,
                       const DualOwnerKeyV1& rhs) noexcept
{
    return lhs.seat == rhs.seat && lhs.signing_key_id == rhs.signing_key_id;
}

enum class DualWindowStatusV1 : std::uint8_t
{
    Ok = 0,
    Duplicate = 1,
    Accepted = 2,
    Frozen = 3,
    RejectedWindowExceeded = 4,
    RejectedCommittedHistory = 5,
    RejectedWrongOwner = 6,
    RejectedStaleRevision = 7,
    RejectedOtherBranch = 8,
    RejectedOtherTimeline = 9,
    RejectedInvalid = 10
};

/* One port inside a materialized frame. `real` is false exactly when the sample
 * is a prediction for that seat, and the owner signing key identifies which seat
 * signed the sample the runtime is being fed. */
struct DualWindowPortPlanV1 final
{
    std::uint32_t port = 0;
    DualPortSampleV1 sample{};
    std::array<std::uint8_t, 32> owner_signing_key_id{};
    bool real = false;
};

/* One frame materialized for the runtime: the complete canonical bundle plus
 * everything the scheduler needs to decide about rollback and freezing. */
struct DualWindowPlanV1 final
{
    DualWindowStatusV1 status = DualWindowStatusV1::Ok;
    DualFreezeReasonV1 freeze = DualFreezeReasonV1::None;
    std::uint64_t frame_index = 0;
    DualInputKeyV1 key{};
    DualInputBundleV1 bundle{};
    std::array<DualWindowPortPlanV1, kDualPortCountV1> ports{};
    std::uint32_t predicted_port_mask = 0;
    std::uint32_t prediction_depth = 0;
    std::uint64_t planned_frontier = 0;
};

class DualInputWindowV1
{
public:
    /*
     * Ring capacity in frames. It must cover every frame that can be in flight:
     * the runtime may run up to kDualPredictionFreezeDepthV1 frames ahead of the
     * committed watermark, an input may arrive up to kDualGuestLeadFramesV1
     * frames ahead of the runtime, and the watermark itself trails the runtime
     * by at most the guest lead (deliveries below it are refused), so the span
     * is 10 + 4 + 4 = 18 and 20 leaves margin. The hard bound the contract
     * advertises is still kDualMaxRollbackFramesV1: a target outside the last
     * twelve frames is a freeze, enforced by the prediction gate below.
     */
    static constexpr std::uint32_t kRingSlotsV1 = 20;

    DualInputWindowV1() noexcept = default;

    DualInputWindowV1(const DualInputWindowV1&) = delete;
    DualInputWindowV1& operator=(const DualInputWindowV1&) = delete;

    /* Bind the session context: the key supplies session/branch/epoch and the
     * current global seat revision; owners supply the per-seat signing key ids
     * that are trusted for this branch. A seat whose key is all zero is not in
     * play. */
    DualWindowStatusV1 begin(
        const DualInputKeyV1& context,
        const std::array<DualOwnerKeyV1, kDualPortCountV1>& owners) noexcept;

    bool is_bound() const noexcept { return bound_; }

    /* Offer a canonical bundle. Idempotent for identical key plus bytes. */
    DualWindowStatusV1 deliver_input(
        const DualInputBundleV1& bundle) noexcept;

    /* Build and offer this engine's own frame samples. The frame is always the
     * next frame to plan and the owner key comes from the binding, so a local
     * caller can never forge another seat or another signing key. */
    DualWindowStatusV1 deliver_local_input(
        const DualPortInputArrayV1& samples,
        std::uint8_t logical_seat) noexcept;

    /* Materialize the next frame. Advances the step frontier. */
    DualWindowPlanV1 plan_next() noexcept;

    /* Record that the runtime executed `frame_index` with this predicted port
     * mask. Returns false when the frame is not the one that was materialized. */
    DualWindowStatusV1 mark_planned(std::uint64_t frame_index,
                                    std::uint32_t predicted_port_mask) noexcept;

    /* The next frame the runtime will execute. */
    std::uint64_t step_frontier() const noexcept { return step_frontier_; }

    /* The first frame whose inputs are not all confirmed. */
    std::uint64_t commit_frontier() const noexcept { return commit_frontier_; }

    bool has_frame(std::uint64_t frame_index) const noexcept;
    /* True when every port that has an owner carries a real sample. */
    bool frame_all_real(std::uint64_t frame_index) const noexcept;
    /* True when every in-play port of the frame is either real or was predicted
     * in the plan the runtime executed. */
    bool frame_is_real(std::uint64_t frame_index) const noexcept;
    std::optional<std::uint32_t> frame_predicted_mask(
        std::uint64_t frame_index) const noexcept;
    std::optional<DualInputBundleV1> clone_frame_bundle(
        std::uint64_t frame_index) const noexcept;

    /* The exact mask a port would be served for a frame: the frame's confirmed
     * sample when present, otherwise the last known complete state, otherwise
     * neutral. */
    std::uint32_t effective_mask(std::uint64_t frame_index,
                                 std::uint32_t port) const noexcept;

private:
    struct FrameRecordV1 final
    {
        bool used = false;
        /* Set once the runtime has executed this frame. */
        bool planned = false;
        std::uint64_t frame_index = 0;
        /* Prediction mask the runtime was actually fed for this frame. */
        std::uint32_t planned_predicted_mask = 0;
        std::uint32_t real_port_mask = 0;
        std::array<bool, kDualPortCountV1> provided{};
        std::array<DualInputKeyV1, kDualPortCountV1> keys{};
        std::array<std::array<std::uint8_t, 32>, kDualPortCountV1>
            owner_keys{};
        std::array<std::uint32_t, kDualPortCountV1> masks{};
        std::array<std::uint64_t, kDualPortCountV1> sequences{};
    };

    static std::uint32_t slot_of(std::uint64_t frame_index) noexcept
    {
        return static_cast<std::uint32_t>(frame_index % kRingSlotsV1);
    }

    FrameRecordV1* find_record(std::uint64_t frame_index) noexcept;
    const FrameRecordV1* find_record(std::uint64_t frame_index) const noexcept;
    FrameRecordV1& ensure_record(std::uint64_t frame_index) noexcept;
    void advance_commit_frontier() noexcept;
    bool frame_real_locked(std::uint64_t frame_index) const noexcept;

    std::array<FrameRecordV1, kRingSlotsV1> ring_{};
    DualInputKeyV1 context_{};
    std::array<DualOwnerKeyV1, kDualPortCountV1> owners_{};
    std::array<std::uint32_t, kDualPortCountV1> last_complete_mask_{};
    std::array<std::uint64_t, kDualPortCountV1> last_complete_sequence_{};
    /* The ports that have a seat owner in this branch. Ports outside this mask
     * are not in play: never required, never predicted, never committed. */
    std::uint32_t owned_port_mask_ = 0;
    /* Next frame the runtime will execute. */
    std::uint64_t step_frontier_ = 0;
    /* First frame whose in-play ports are not all confirmed. */
    std::uint64_t commit_frontier_ = 0;
    /* Furthest frame this window has materialized. */
    std::uint64_t materialized_through_ = 0;
    std::uint64_t min_accept_revision_ = 0;
    bool bound_ = false;
};

} // namespace flynes::session::dual

#endif
