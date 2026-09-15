#include "input_window.hpp"

namespace flynes::session::dual {
namespace {

bool owner_key_matches(const std::array<std::uint8_t, 32>& lhs,
                       const std::array<std::uint8_t, 32>& rhs) noexcept
{
    return lhs == rhs;
}

} // namespace

DualWindowStatusV1 DualInputWindowV1::begin(
    const DualInputKeyV1& context,
    const std::array<DualOwnerKeyV1, kDualPortCountV1>& owners) noexcept
{
    if (!dual_input_key_is_valid_v1(context))
        return DualWindowStatusV1::RejectedInvalid;

    /* One owner entry per port. A seat that is not in play carries an all-zero
     * signing key: it owns nothing, so every bundle claiming it is refused. */
    std::array<bool, kDualPortCountV1> seat_seen{};
    for (const auto& owner : owners) {
        if (owner.seat >= static_cast<std::uint8_t>(kDualPortCountV1))
            return DualWindowStatusV1::RejectedInvalid;
        if (seat_seen[owner.seat])
            return DualWindowStatusV1::RejectedInvalid;
        seat_seen[owner.seat] = true;
    }
    for (const auto seen : seat_seen)
        if (!seen)
            return DualWindowStatusV1::RejectedInvalid;

    ring_ = {};
    context_ = context;
    owners_ = owners;
    std::array<std::uint8_t, 32> zero_key{};
    owned_port_mask_ = 0;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
        if (!owner_key_matches(owners_[port].signing_key_id, zero_key))
            owned_port_mask_ |= 1u << port;
    }
    last_complete_mask_ = {};
    last_complete_sequence_ = {};
    planned_frontier_ = 0;
    replan_floor_ = 0;
    commit_frontier_ = 0;
    min_accept_revision_ = context.seat_revision;
    bound_ = true;
    return DualWindowStatusV1::Ok;
}

DualInputWindowV1::FrameRecordV1* DualInputWindowV1::find_record(
    std::uint64_t frame_index) noexcept
{
    for (auto& record : ring_) {
        if (record.used && record.frame_index == frame_index)
            return &record;
    }
    return nullptr;
}

const DualInputWindowV1::FrameRecordV1* DualInputWindowV1::find_record(
    std::uint64_t frame_index) const noexcept
{
    for (const auto& record : ring_) {
        if (record.used && record.frame_index == frame_index)
            return &record;
    }
    return nullptr;
}

bool DualInputWindowV1::has_frame(std::uint64_t frame_index) const noexcept
{
    return find_record(frame_index) != nullptr;
}

bool DualInputWindowV1::frame_all_real(std::uint64_t frame_index) const noexcept
{
    const auto* record = find_record(frame_index);
    if (record == nullptr || owned_port_mask_ == 0)
        return false;
    return (record->real_port_mask & owned_port_mask_) == owned_port_mask_;
}

bool DualInputWindowV1::frame_is_real(std::uint64_t frame_index) const noexcept
{
    const auto* record = find_record(frame_index);
    if (record == nullptr || owned_port_mask_ == 0)
        return false;
    return ((record->real_port_mask | record->planned_predicted_mask) &
            owned_port_mask_) == owned_port_mask_;
}

std::optional<std::uint32_t> DualInputWindowV1::frame_predicted_mask(
    std::uint64_t frame_index) const noexcept
{
    const auto* record = find_record(frame_index);
    if (record == nullptr)
        return std::nullopt;
    return record->planned_predicted_mask;
}

std::optional<DualInputBundleV1> DualInputWindowV1::clone_frame_bundle(
    std::uint64_t frame_index) const noexcept
{
    const auto* record = find_record(frame_index);
    if (record == nullptr)
        return std::nullopt;

    const auto plan = plan_next();
    if (plan.status != DualWindowStatusV1::Ok ||
        plan.frame_index != frame_index)
        return std::nullopt;

    DualInputBundleV1 bundle = plan.bundle;
    bundle.predicted_port_mask = record->planned_predicted_mask;
    return bundle;
}

std::uint32_t DualInputWindowV1::effective_mask(std::uint64_t frame_index,
                                                std::uint32_t port) const noexcept
{
    if (port >= kDualPortCountV1)
        return 0;
    const auto* record = find_record(frame_index);
    if (record != nullptr && record->provided[port])
        return record->masks[port];
    return last_complete_mask_[port];
}


std::uint64_t DualInputWindowV1::next_frame() const noexcept
{
    const auto plan = plan_next();
    return plan.frame_index;
}

DualWindowStatusV1 DualInputWindowV1::deliver_local_input(
    const DualPortInputArrayV1& samples, std::uint8_t logical_seat) noexcept
{
    if (!bound_)
        return DualWindowStatusV1::RejectedInvalid;

    std::optional<std::uint32_t> seat;
    for (std::uint32_t index = 0; index < kDualPortCountV1; ++index) {
        if (owners_[index].seat == logical_seat) {
            seat = index;
            break;
        }
    }
    if (!seat.has_value())
        return DualWindowStatusV1::RejectedWrongOwner;

    DualInputKeyV1 key = context_;
    key.frame_index = next_frame();

    DualInputBundleV1 bundle{};
    if (canonical_input_build_v1(key, logical_seat,
                                 owners_[*seat].signing_key_id, samples,
                                 &bundle) != DualInputStatusV1::Ok)
        return DualWindowStatusV1::RejectedInvalid;
    return deliver_input(bundle);
}

DualWindowStatusV1 DualInputWindowV1::deliver_input(
    const DualInputBundleV1& bundle) noexcept
{
    if (!bound_)
        return DualWindowStatusV1::RejectedInvalid;
    if (!canonical_input_is_canonical_v1(bundle))
        return DualWindowStatusV1::RejectedInvalid;
    if (bundle.key.session_id != context_.session_id)
        return DualWindowStatusV1::RejectedInvalid;
    if (bundle.key.branch_id != context_.branch_id)
        return DualWindowStatusV1::RejectedOtherBranch;
    if (bundle.key.timeline_epoch != context_.timeline_epoch)
        return DualWindowStatusV1::RejectedOtherTimeline;
    if (bundle.key.seat_revision < min_accept_revision_)
        return DualWindowStatusV1::RejectedStaleRevision;

    std::optional<std::uint32_t> seat;
    for (std::uint32_t index = 0; index < kDualPortCountV1; ++index) {
        if (owners_[index].seat == bundle.logical_seat) {
            seat = index;
            break;
        }
    }
    if (!seat.has_value())
        return DualWindowStatusV1::RejectedWrongOwner;
    /* An out-of-play seat has no owner key, so nothing can be attributed to it. */
    std::array<std::uint8_t, 32> zero_key{};
    if (owner_key_matches(owners_[*seat].signing_key_id, zero_key))
        return DualWindowStatusV1::RejectedWrongOwner;
    if (!owner_key_matches(owners_[*seat].signing_key_id,
                           bundle.owner_signing_key_id))
        return DualWindowStatusV1::RejectedWrongOwner;

    const std::uint64_t frame_index = bundle.key.frame_index;
    const std::uint32_t port = *seat;

    /* Idempotency and committed-history checks come first: a retransmission of a
     * frame that has already moved into committed history must still be
     * recognized as a duplicate, not mistaken for an out-of-window packet. */
    auto* record = find_record(frame_index);
    if (record != nullptr && record->provided[port]) {
        if (owner_key_matches(record->owner_keys[port],
                              bundle.owner_signing_key_id) &&
            record->masks[port] == bundle.ports[port].mask &&
            record->sequences[port] == bundle.ports[port].input_sequence)
            return DualWindowStatusV1::Duplicate;
        return DualWindowStatusV1::RejectedCommittedHistory;
    }

    /* The retained window is [commit_frontier_, commit_frontier_ + 12): twelve
     * slots, so a frame needing a thirteenth is refused. */
    if (frame_index < commit_frontier_)
        return DualWindowStatusV1::RejectedWindowExceeded;
    if (frame_index >= commit_frontier_ + kDualMaxRollbackFramesV1)
        return DualWindowStatusV1::RejectedWindowExceeded;

    if (record == nullptr) {
        for (auto& candidate : ring_) {
            if (!candidate.used) {
                candidate = {};
                candidate.used = true;
                candidate.frame_index = frame_index;
                record = &candidate;
                break;
            }
        }
        if (record == nullptr)
            return DualWindowStatusV1::RejectedWindowExceeded;
    }

    /* A canonical bundle carries the complete four-port frame, but a port's
     * authoritative sample is the one its own owner signs. The delivering
     * seat's own port is confirmed here; the other ports in the bundle are kept
     * only as that seat's reported view and become real when their own owner
     * delivers, so one player's view can never masquerade as another's input. */
    record->keys[port] = bundle.key;
    record->owner_keys[port] = bundle.owner_signing_key_id;
    record->masks[port] = bundle.ports[port].mask;
    record->sequences[port] = bundle.ports[port].input_sequence;
    record->provided[port] = true;
    record->real_port_mask |= 1u << port;
    /* A port that now has a real sample is no longer a prediction. */
    record->planned_predicted_mask &= ~(1u << port);

    if (bundle.key.seat_revision > min_accept_revision_)
        min_accept_revision_ = bundle.key.seat_revision;

    advance_commit_frontier();
    return DualWindowStatusV1::Accepted;
}

void DualInputWindowV1::advance_commit_frontier() noexcept
{
    while (true) {
        const auto* record = find_record(commit_frontier_);
        if (record == nullptr ||
            (record->real_port_mask & owned_port_mask_) != owned_port_mask_ ||
            owned_port_mask_ == 0)
            break;
        for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
            if ((owned_port_mask_ & (1u << port)) == 0)
                continue;
            last_complete_mask_[port] = record->masks[port];
            last_complete_sequence_[port] = record->sequences[port];
        }
        ++commit_frontier_;
    }
}

std::uint64_t DualInputWindowV1::first_unplanned_frame() const noexcept
{
    const std::uint64_t frame = planned_frontier_;
    const auto* record = find_record(frame);
    /* A frame with no record, or one created only by a delivery, has never been
     * stepped. */
    if (record == nullptr || !record->planned)
        return frame;
    /* A frame whose step accounted for every port that is in play is finished;
     * for a frame that has no ports at all in play, one step is also enough. */
    const std::uint32_t planned_mask =
        record->planned_predicted_mask & owned_port_mask_;
    if (planned_mask == owned_port_mask_ && planned_frontier_ < frame + 1)
        return frame + 1;
    return frame;
}

std::uint64_t DualInputWindowV1::lowest_replan_frame() const noexcept
{
    for (std::uint64_t frame = replan_floor_; frame < planned_frontier_;
         ++frame) {
        const auto* record = find_record(frame);
        if (record == nullptr)
            continue;
        /* Real input arrived for a frame that was stepped while some port was
         * still a prediction, so that frame must be re-materialized. */
        if (record->planned_predicted_mask != 0 &&
            record->real_port_mask != record->planned_predicted_mask &&
            record->real_port_mask != 0)
            return frame;
    }
    return planned_frontier_;
}

DualWindowPlanV1 DualInputWindowV1::plan_next() const noexcept
{
    DualWindowPlanV1 plan{};
    if (!bound_) {
        plan.status = DualWindowStatusV1::RejectedInvalid;
        return plan;
    }

    /* A recorded step retires every frame at or below it, so the earliest frame
     * that can need materializing is the first unplanned one. */
    std::uint64_t frame = lowest_replan_frame();
    if (frame < planned_frontier_)
        frame = planned_frontier_;
    const std::uint64_t candidate = first_unplanned_frame();
    if (candidate > frame)
        frame = candidate;

    const std::uint64_t distance =
        frame > commit_frontier_ ? frame - commit_frontier_ : 0;
    if (distance >= kDualMaxRollbackFramesV1) {
        plan.status = DualWindowStatusV1::Frozen;
        plan.freeze = DualFreezeReasonV1::InputWindowExceeded;
        plan.frame_index = frame;
        plan.prediction_depth = static_cast<std::uint32_t>(distance);
        plan.planned_frontier = planned_frontier_;
        return plan;
    }
    if (distance >= kDualPredictionFreezeDepthV1) {
        plan.status = DualWindowStatusV1::Frozen;
        plan.freeze = DualFreezeReasonV1::PredictionDepthExceeded;
        plan.frame_index = frame;
        plan.prediction_depth = static_cast<std::uint32_t>(distance);
        plan.planned_frontier = planned_frontier_;
        return plan;
    }

    const auto* record = find_record(frame);
    plan.status = DualWindowStatusV1::Ok;
    plan.frame_index = frame;
    plan.key = context_;
    plan.key.frame_index = frame;
    plan.key.seat_revision = context_.seat_revision;
    if (record != nullptr) {
        for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
            if (record->provided[port])
                plan.key.seat_revision = record->keys[port].seat_revision;
        }
    }

    /* Two ports sharing one frame row belong to two different seats, so the
     * runtime bundle carries sample bytes only: the per-port owner signing key
     * and the real/predicted flag stay alongside it in plan.ports[]. */
    DualInputBundleV1 bundle{};
    bundle.key = plan.key;
    bundle.logical_seat = 0;
    std::uint32_t predicted = 0;
    std::uint64_t previous_sequence = 0;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
        DualWindowPortPlanV1 port_plan{};
        port_plan.port = port;
        if (record != nullptr) {
            /* The frame carries the complete four-port state from the bundles
             * that have arrived; a port is a confirmed sample only when its own
             * seat's bundle has been accepted. */
            const bool own = record->provided[port];
            port_plan.sample = {record->masks[port], record->sequences[port]};
            port_plan.owner_signing_key_id =
                own ? record->owner_keys[port] : owners_[port].signing_key_id;
            port_plan.real = own;
        } else {
            const auto sequence = last_complete_sequence_[port] == 0
                                      ? 1u
                                      : last_complete_sequence_[port];
            port_plan.sample = {last_complete_mask_[port], sequence};
            port_plan.owner_signing_key_id = owners_[port].signing_key_id;
            port_plan.real = false;
        }
        if (!port_plan.real && (owned_port_mask_ & (1u << port)) != 0)
            predicted |= 1u << port;
        if (port_plan.sample.input_sequence <= previous_sequence)
            port_plan.sample.input_sequence = previous_sequence + 1u;
        previous_sequence = port_plan.sample.input_sequence;
        bundle.ports[port] = port_plan.sample;
        plan.ports[port] = port_plan;
    }

    bundle.predicted_port_mask = predicted;
    bundle.owner_signing_key_id = owners_[0].signing_key_id;
    plan.bundle = bundle;
    plan.predicted_port_mask = predicted;
    plan.prediction_depth = static_cast<std::uint32_t>(distance);
    plan.freeze = DualFreezeReasonV1::None;
    plan.planned_frontier = planned_frontier_;
    return plan;
}

DualWindowStatusV1 DualInputWindowV1::mark_planned(
    std::uint64_t frame_index, std::uint32_t predicted_port_mask) noexcept
{
    if (!bound_)
        return DualWindowStatusV1::RejectedInvalid;
    if (frame_index < commit_frontier_ ||
        frame_index > planned_frontier_ + kDualMaxRollbackFramesV1)
        return DualWindowStatusV1::RejectedWindowExceeded;

    auto* record = find_record(frame_index);
    if (record == nullptr) {
        for (auto& candidate : ring_) {
            if (!candidate.used) {
                candidate = {};
                candidate.used = true;
                candidate.frame_index = frame_index;
                record = &candidate;
                break;
            }
        }
        if (record == nullptr)
            return DualWindowStatusV1::RejectedWindowExceeded;
    }

    if (predicted_port_mask != 0) {
        for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
            if ((predicted_port_mask & (1u << port)) != 0 &&
                record->provided[port]) {
                predicted_port_mask &= ~(1u << port);
            }
        }
    }
    record->planned = true;
    record->planned_predicted_mask = predicted_port_mask;
    /* A frame at or above the frontier is the furthest frame the engine has
     * stepped, so the frontier follows it. Every frame below it has already
     * been materialized at least once, which is what makes the frontier an
     * exact progress mark even when a frame is planned repeatedly. */
    if (frame_index >= planned_frontier_)
        planned_frontier_ = frame_index + 1;
    return DualWindowStatusV1::Ok;
}

} // namespace flynes::session::dual
