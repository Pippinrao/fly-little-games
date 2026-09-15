#include "input_window.hpp"

namespace flynes::session::dual {
namespace {

bool key_is_all_zero(const std::array<std::uint8_t, 32>& value) noexcept
{
    for (const auto byte : value)
        if (byte != 0)
            return false;
    return true;
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
    owned_port_mask_ = 0;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
        if (!key_is_all_zero(owners_[port].signing_key_id))
            owned_port_mask_ |= 1u << port;
    }
    last_complete_mask_ = {};
    last_complete_sequence_ = {};
    step_frontier_ = 0;
    commit_frontier_ = 0;
    materialized_through_ = 0;
    min_accept_revision_ = context.seat_revision;
    bound_ = true;
    return DualWindowStatusV1::Ok;
}

DualInputWindowV1::FrameRecordV1* DualInputWindowV1::find_record(
    std::uint64_t frame_index) noexcept
{
    auto& slot = ring_[slot_of(frame_index)];
    if (!slot.used || slot.frame_index != frame_index)
        return nullptr;
    return &slot;
}

const DualInputWindowV1::FrameRecordV1* DualInputWindowV1::find_record(
    std::uint64_t frame_index) const noexcept
{
    const auto& slot = ring_[slot_of(frame_index)];
    if (!slot.used || slot.frame_index != frame_index)
        return nullptr;
    return &slot;
}

DualInputWindowV1::FrameRecordV1& DualInputWindowV1::ensure_record(
    std::uint64_t frame_index) noexcept
{
    if (auto* existing = find_record(frame_index); existing != nullptr)
        return *existing;
    /* The ring is sized for every frame that can be in flight, so the slot's
     * old occupant is never still needed when it is reused. */
    auto& slot = ring_[slot_of(frame_index)];
    slot = {};
    slot.used = true;
    slot.frame_index = frame_index;
    return slot;
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

    DualInputBundleV1 bundle{};
    bundle.key = context_;
    bundle.key.frame_index = frame_index;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
        if (record->provided[port]) {
            bundle.ports[port] = {record->masks[port], record->sequences[port]};
        } else {
            const auto sequence = last_complete_sequence_[port] == 0
                                      ? 1u
                                      : last_complete_sequence_[port];
            bundle.ports[port] = {last_complete_mask_[port], sequence};
        }
    }
    bundle.predicted_port_mask = record->planned_predicted_mask;
    bundle.owner_signing_key_id = owners_[0].signing_key_id;
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
    if (!seat.has_value() || key_is_all_zero(owners_[*seat].signing_key_id))
        return DualWindowStatusV1::RejectedWrongOwner;

    DualInputKeyV1 key = context_;
    key.frame_index = step_frontier_;

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
    if (!seat.has_value() || key_is_all_zero(owners_[*seat].signing_key_id))
        return DualWindowStatusV1::RejectedWrongOwner;
    if (owners_[*seat].signing_key_id != bundle.owner_signing_key_id)
        return DualWindowStatusV1::RejectedWrongOwner;

    const std::uint64_t frame_index = bundle.key.frame_index;
    const std::uint32_t port = *seat;

    /* Idempotency and equivocation come first: a retransmission of a frame that
     * has already left the in-flight set must still read as a duplicate rather
     * than as an out-of-window packet. */
    if (const auto* existing = find_record(frame_index); existing != nullptr) {
        if (existing->provided[port]) {
            if (existing->owner_keys[port] == bundle.owner_signing_key_id &&
                existing->masks[port] == bundle.ports[port].mask &&
                existing->sequences[port] == bundle.ports[port].input_sequence)
                return DualWindowStatusV1::Duplicate;
            /* The port already carries its owner's sample, so different bytes
             * for the same frame and port can only be an equivocation. */
            return DualWindowStatusV1::RejectedCommittedHistory;
        }
    }

    /* The in-flight window. A frame below the committed watermark is already
     * immutable history, so offering different bytes for it can only be an
     * attempted rewrite; further ahead than the guest lead the frame is beyond
     * any legal input and would only crowd out the frame about to be run. */
    if (frame_index < commit_frontier_)
        return DualWindowStatusV1::RejectedCommittedHistory;
    if (frame_index > step_frontier_ + kDualGuestLeadFramesV1)
        return DualWindowStatusV1::RejectedWindowExceeded;

    auto& record = ensure_record(frame_index);
    record.keys[port] = bundle.key;
    record.owner_keys[port] = bundle.owner_signing_key_id;
    record.masks[port] = bundle.ports[port].mask;
    record.sequences[port] = bundle.ports[port].input_sequence;
    record.provided[port] = true;
    record.real_port_mask |= 1u << port;
    /* The port now has a real sample, so it is no longer a prediction. */
    record.planned_predicted_mask &= ~(1u << port);

    if (bundle.key.seat_revision > min_accept_revision_)
        min_accept_revision_ = bundle.key.seat_revision;

    advance_commit_frontier();
    return DualWindowStatusV1::Accepted;
}

void DualInputWindowV1::advance_commit_frontier() noexcept
{
    /* The watermark only covers frames the runtime has already executed: a frame
     * that is fully real but not yet stepped is still in flight. */
    while (true) {
        const auto* record = find_record(commit_frontier_);
        if (record == nullptr || !record->planned ||
            (record->real_port_mask & owned_port_mask_) != owned_port_mask_)
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

DualWindowPlanV1 DualInputWindowV1::plan_next() noexcept
{
    DualWindowPlanV1 plan{};
    if (!bound_) {
        plan.status = DualWindowStatusV1::RejectedInvalid;
        return plan;
    }

    /* The runtime may only run ahead of the committed watermark by the frozen
     * prediction depth. That one gate bounds the whole in-flight span, because
     * it caps how far the step frontier can lead the watermark. */
    std::uint64_t frame = step_frontier_;
    if (frame < commit_frontier_)
        frame = commit_frontier_;
    const std::uint64_t distance =
        frame > commit_frontier_ ? frame - commit_frontier_ : 0;
    if (distance >= kDualMaxRollbackFramesV1) {
        plan.status = DualWindowStatusV1::Frozen;
        plan.freeze = DualFreezeReasonV1::InputWindowExceeded;
        plan.frame_index = frame;
        plan.prediction_depth = static_cast<std::uint32_t>(distance);
        plan.planned_frontier = step_frontier_;
        return plan;
    }
    if (distance >= kDualPredictionFreezeDepthV1) {
        plan.status = DualWindowStatusV1::Frozen;
        plan.freeze = DualFreezeReasonV1::PredictionDepthExceeded;
        plan.frame_index = frame;
        plan.prediction_depth = static_cast<std::uint32_t>(distance);
        plan.planned_frontier = step_frontier_;
        return plan;
    }

    const auto* record = find_record(frame);
    if (record == nullptr)
        record = &ensure_record(frame);
    if (frame + 1 > materialized_through_)
        materialized_through_ = frame + 1;

    plan.status = DualWindowStatusV1::Ok;
    plan.frame_index = frame;
    plan.key = context_;
    plan.key.frame_index = frame;
    plan.key.seat_revision = context_.seat_revision;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
        if (record->provided[port])
            plan.key.seat_revision = record->keys[port].seat_revision;
    }

    /* A canonical bundle always carries the complete four-port frame, but a
     * port's authoritative sample is the one its own owner signs. A port whose
     * seat has not reported is a prediction taken from that seat's last known
     * complete state and is always flagged. */
    DualInputBundleV1 bundle{};
    bundle.key = plan.key;
    bundle.logical_seat = 0;
    std::uint32_t predicted = 0;
    std::uint64_t previous_sequence = 0;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
        DualWindowPortPlanV1 port_plan{};
        port_plan.port = port;
        if (record->provided[port]) {
            port_plan.sample = {record->masks[port], record->sequences[port]};
            port_plan.owner_signing_key_id = record->owner_keys[port];
            port_plan.real = true;
        } else {
            const auto sequence = last_complete_sequence_[port] == 0
                                      ? 1u
                                      : last_complete_sequence_[port];
            port_plan.sample = {last_complete_mask_[port], sequence};
            port_plan.owner_signing_key_id = owners_[port].signing_key_id;
            port_plan.real = false;
            if ((owned_port_mask_ & (1u << port)) != 0)
                predicted |= 1u << port;
        }
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
    plan.planned_frontier = step_frontier_;
    return plan;
}

DualWindowStatusV1 DualInputWindowV1::mark_planned(
    std::uint64_t frame_index, std::uint32_t predicted_port_mask) noexcept
{
    if (!bound_)
        return DualWindowStatusV1::RejectedInvalid;
    if (frame_index >= materialized_through_)
        return DualWindowStatusV1::RejectedWindowExceeded;

    auto* record = find_record(frame_index);
    if (record == nullptr)
        return DualWindowStatusV1::RejectedWindowExceeded;

    /* A port that was fed a real sample is not a prediction, whatever the plan
     * believed when it was built. */
    predicted_port_mask &= ~record->real_port_mask;
    record->planned = true;
    record->planned_predicted_mask = predicted_port_mask;

    /* This step retires the frame for good, which is what frees its ring slot. */
    step_frontier_ = frame_index + 1;
    advance_commit_frontier();
    return DualWindowStatusV1::Ok;
}

} // namespace flynes::session::dual
