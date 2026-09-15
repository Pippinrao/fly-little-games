#ifndef FLYNES_SESSION_DUAL_INPUT_WINDOW_HPP
#define FLYNES_SESSION_DUAL_INPUT_WINDOW_HPP

/*
 * W2 / Task 5: bounded DUAL input window.
 *
 * The window is the per-frame authority for input reality in one engine:
 *
 *   - it stores at most one canonical bundle per (frame, logical seat), so
 *     out-of-order arrival can never change the materialized frame;
 *   - a frame inside the ring is re-materialized from a fully real per-seat
 *     bundle before it is reported again, which is the rollback/replay basis;
 *   - a frame that has become fully real is committed history and may never be
 *     rewritten by different bytes; a byte-identical retransmission is only a
 *     duplicate;
 *   - a missing port is predicted from the last known complete state of that
 *     seat and is always flagged in predicted_port_mask. A predicted frame is
 *     never a final canonical success;
 *   - the hard ring is kDualMaxRollbackFramesV1 (12) frames and the prediction
 *     gate is kDualPredictionFreezeDepthV1 (10) frames: a plan at depth ten
 *     freezes, so a prediction depth of twelve is unreachable.
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
    DualInputWindowV1() noexcept = default;

    DualInputWindowV1(const DualInputWindowV1&) = delete;
    DualInputWindowV1& operator=(const DualInputWindowV1&) = delete;

    /* Bind the session context: the key supplies session/branch/epoch and the
     * current global seat revision; owners supply the per-seat signing key ids
     * that are trusted for this branch. */
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

    /* The next frame this window would materialize. */
    std::uint64_t next_frame() const noexcept;

    /* Materialize the next frame. Never mutates the window. */
    DualWindowPlanV1 plan_next() const noexcept;

    /* Record that the runtime stepped the frame with this predicted port mask,
     * then re-materialize a frame at or above the committed watermark. */
    DualWindowStatusV1 mark_planned(std::uint64_t frame_index,
                                    std::uint32_t predicted_port_mask) noexcept;

    /* Where the engine has stepped to so far. */
    std::uint64_t planned_frontier() const noexcept { return planned_frontier_; }

    /* The last frame that is fully real, so entirely immutable. */
    std::uint64_t commit_frontier() const noexcept { return commit_frontier_; }

    bool has_frame(std::uint64_t frame_index) const noexcept;
    /* True when every port that has an owner carries a real sample. */
    bool frame_all_real(std::uint64_t frame_index) const noexcept;
    /* True when every port of the frame carries a real (never predicted)
     * sample, so the frame is immutable even above the committed watermark. */
    bool frame_is_real(std::uint64_t frame_index) const noexcept;
    std::optional<std::uint32_t> frame_predicted_mask(
        std::uint64_t frame_index) const noexcept;
    std::optional<DualInputBundleV1> clone_frame_bundle(
        std::uint64_t frame_index) const noexcept;

    /* The exact mask a port would be served for a frame: the frame's real
     * bundle when present, otherwise the last known complete state, otherwise
     * neutral. */
    std::uint32_t effective_mask(std::uint64_t frame_index,
                                 std::uint32_t port) const noexcept;

private:
    struct FrameRecordV1 final
    {
        bool used = false;
        /* Set once the runtime has stepped this frame at least once. */
        bool planned = false;
        std::uint64_t frame_index = 0;
        /* Materialized plan when the runtime stepped the frame. */
        std::uint32_t planned_predicted_mask = 0;
        std::uint32_t real_port_mask = 0;
        std::array<bool, kDualPortCountV1> provided{};
        std::array<DualInputKeyV1, kDualPortCountV1> keys{};
        std::array<std::array<std::uint8_t, 32>, kDualPortCountV1>
            owner_keys{};
        std::array<std::uint32_t, kDualPortCountV1> masks{};
        std::array<std::uint64_t, kDualPortCountV1> sequences{};
    };

    FrameRecordV1* find_record(std::uint64_t frame_index) noexcept;
    const FrameRecordV1* find_record(std::uint64_t frame_index) const noexcept;
    void advance_commit_frontier() noexcept;
    std::uint64_t first_unplanned_frame() const noexcept;
    std::uint64_t lowest_replan_frame() const noexcept;

    std::array<FrameRecordV1, kDualMaxRollbackFramesV1> ring_{};
    DualInputKeyV1 context_{};
    std::array<DualOwnerKeyV1, kDualPortCountV1> owners_{};
    std::array<std::uint32_t, kDualPortCountV1> last_complete_mask_{};
    std::array<std::uint64_t, kDualPortCountV1> last_complete_sequence_{};
    /* The ports that have a seat owner in this branch. Ports outside this mask
     * are not in play: they are never required, never predicted and never
     * committed, so a two-seat DUAL session still commits every frame. */
    std::uint32_t owned_port_mask_ = 0;
    std::uint64_t planned_frontier_ = 0;
    /* Frames below this have already been stepped and must not be re-planned
     * unless real input replaced a prediction they were stepped with. */
    std::uint64_t replan_floor_ = 0;
    std::uint64_t commit_frontier_ = 0;
    std::uint64_t min_accept_revision_ = 0;
    bool bound_ = false;
};

} // namespace flynes::session::dual

#endif
