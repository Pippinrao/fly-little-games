/*
 * W2 / Task 5: bounded DUAL input window.
 *
 * The window holds the rollback ring. Missing input is predicted from the last
 * known complete per-seat state; a predicted frame is never a final canonical
 * success. A prediction depth of ten frames freezes the session, and nothing may
 * ever exceed the hard twelve frame ring.
 */

#include "dual/canonical_input.hpp"
#include "dual/input_window.hpp"
#include "flynes/flynes_runtime.h"
#include "flynes/product/nes_input_bits.hpp"

#include <array>
#include <cstdio>

namespace {

namespace dual = flynes::session::dual;
namespace product = flynes::product;

using dual::DualInputWindowV1;
using dual::DualOwnerKeyV1;
using dual::DualPortInputV1;
using dual::DualWindowStatusV1;

int failures = 0;

void check(bool value, const char* message)
{
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::uint64_t next_sequence = 1;

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

dual::DualInputKeyV1 key_for(std::uint64_t frame, std::uint64_t revision)
{
    dual::DualInputKeyV1 key{};
    key.session_id = id(0x11);
    key.branch_id = id(0x22);
    key.timeline_epoch = 3;
    key.frame_index = frame;
    key.seat_revision = revision;
    return key;
}

std::array<DualPortInputV1, dual::kDualPortCountV1> samples(
    std::uint32_t port_zero, std::uint32_t port_one, std::uint32_t port_two,
    std::uint32_t port_three)
{
    std::array<DualPortInputV1, dual::kDualPortCountV1> out{};
    out[0] = {port_zero, next_sequence++};
    out[1] = {port_one, next_sequence++};
    out[2] = {port_two, next_sequence++};
    out[3] = {port_three, next_sequence++};
    return out;
}

/* The DUAL fixture in this file runs two seats: seat 0 owns port 0 with key
 * 0x55 and seat 1 owns port 1 with key 0x77. Ports 2 and 3 carry an all-zero
 * key, which is how the window is told that those seats are not in play. */
std::array<DualOwnerKeyV1, dual::kDualPortCountV1> owners()
{
    std::array<DualOwnerKeyV1, dual::kDualPortCountV1> out{};
    out[0] = {0, key_id(0x55)};
    out[1] = {1, key_id(0x77)};
    out[2] = {2, std::array<std::uint8_t, 32>{}};
    out[3] = {3, std::array<std::uint8_t, 32>{}};
    return out;
}

dual::DualInputBundleV1 bundle_for(
    const dual::DualInputKeyV1& key, std::uint8_t seat,
    const std::array<std::uint8_t, 32>& owner,
    const std::array<DualPortInputV1, dual::kDualPortCountV1>& raw)
{
    dual::DualInputBundleV1 bundle{};
    const auto status =
        dual::canonical_input_build_v1(key, seat, owner, raw, &bundle);
    if (status != dual::DualInputStatusV1::Ok)
    check(status == dual::DualInputStatusV1::Ok,
          "the test fixture bundle builds");
    return bundle;
}

DualWindowStatusV1 bind(DualInputWindowV1& window, std::uint64_t revision = 1)
{
    return window.begin(key_for(0, revision), owners());
}

/* ---------------------------------------------------- predict and converge */

void prediction_is_filled_from_last_known_state()
{
    DualInputWindowV1 window;
    check(bind(window) == DualWindowStatusV1::Ok,
          "the window binds a session context");

    const auto first = samples(product::NES_A, 0, 0, 0);
    check(window.deliver_input(bundle_for(key_for(0, 1), 0, key_id(0x55),
                                          first)) ==
              DualWindowStatusV1::Accepted,
          "a confirmed local bundle is accepted");

    auto plan = window.plan_next();
    check(plan.status == DualWindowStatusV1::Ok,
          "planning the first frame succeeds");
    check(plan.frame_index == 0, "the first planned frame is frame zero");
    check(plan.bundle.ports[0].mask == product::NES_A,
          "the confirmed port is honoured without prediction");
    /* Ports that are in play but have not reported yet are predictions; ports
     * with no seat owner are not in play at all and are never predicted. */
    check((plan.bundle.predicted_port_mask & 0x1u) == 0,
          "a confirmed port is not marked predicted");
    check((plan.bundle.predicted_port_mask & 0x2u) != 0,
          "a missing in-play port is marked predicted");
    check((plan.bundle.predicted_port_mask & 0x4u) == 0,
          "a port with no owner is not predicted");
    check((plan.bundle.predicted_port_mask & 0x8u) == 0,
          "every unowned port is left out of the prediction mask");
    check(plan.bundle.ports[1].mask == 0 && plan.bundle.ports[2].mask == 0 &&
              plan.bundle.ports[3].mask == 0,
          "a never seen seat predicts neutral");
    check(plan.ports[0].owner_signing_key_id == key_id(0x55),
          "a real port is attributed to its owner signing key");
    check(plan.ports[1].owner_signing_key_id == key_id(0x77),
          "a predicted port is attributed to the seat it predicts for");
    check(plan.key == key_for(0, 1), "the plan carries the exact input key");
    check(plan.prediction_depth == 0, "the first frame has no depth yet");
    check(plan.freeze == dual::DualFreezeReasonV1::None,
          "no freeze at depth zero");

    /* Frame zero is stepped, then the peer's real bundle for frame zero makes
     * the frame fully real, and its frame-one bundle drives the next plan. */
    check(window.mark_planned(0, plan.bundle.predicted_port_mask) ==
              DualWindowStatusV1::Ok,
          "frame zero is recorded as planned");
    const auto peer_zero = samples(0, 0, 0, 0);
    check(window.deliver_input(bundle_for(key_for(0, 1), 1, key_id(0x77),
                                          peer_zero)) ==
              DualWindowStatusV1::Accepted,
          "the peer's real frame-zero bundle is accepted");
    check(window.frame_is_real(0) && window.frame_all_real(0),
          "frame zero is fully real once both seats reported");
    check(window.commit_frontier() == 1,
          "the committed watermark follows the fully real frame");
    const auto peer_one = samples(0, product::NES_UP, 0, 0);
    check(window.deliver_input(bundle_for(key_for(1, 1), 1, key_id(0x77),
                                          peer_one)) ==
              DualWindowStatusV1::Accepted,
          "a confirmed peer bundle for frame one is accepted");
    plan = window.plan_next();
    check(plan.frame_index == 1, "the window advances to frame one");
    check(plan.bundle.ports[1].mask == product::NES_UP,
          "the peer's own port one sample is served for frame one");
    check(plan.prediction_depth == 0,
          "frame one is at the committed watermark");
}

void conflicting_directions_are_cleared_before_the_window()
{
    DualInputWindowV1 window;
    check(bind(window) == DualWindowStatusV1::Ok,
          "the window binds for the normalization case");
    /* Port 0 carries both opposing d-pad pairs and must be cleared; port 1
     * carries a legal button mask that must survive byte for byte. */
    const auto conflicting =
        samples(product::NES_UP | product::NES_DOWN, product::NES_A | product::NES_B,
                0, 0);
    check(window.deliver_input(bundle_for(key_for(0, 1), 0, key_id(0x55),
                                          conflicting)) ==
              DualWindowStatusV1::Accepted,
          "the normalized conflicting bundle is accepted");
    const auto raw_plan = window.plan_next();
    const auto clone = window.clone_frame_bundle(0);
    check(clone.has_value(), "the stored bundle is readable");
    check(clone->ports[0].mask == 0,
          "UP+DOWN was neutralized before the window stored it");
    /* Seat 1 has not reported for frame zero, so port 1 is still a prediction
     * and must not carry seat 0's view of that pad. */
    check((raw_plan.predicted_port_mask & 0x2u) != 0,
          "an unreported in-play port stays predicted");
    const auto peer = samples(0, product::NES_A | product::NES_B, 0, 0);
    check(window.deliver_input(bundle_for(key_for(0, 1), 1, key_id(0x77),
                                          peer)) ==
              DualWindowStatusV1::Accepted,
          "the second seat's own frame-zero bundle is accepted");
    const auto confirmed = window.clone_frame_bundle(0);
    check(confirmed.has_value() &&
              confirmed->ports[1].mask == (product::NES_A | product::NES_B),
          "a legal button mask is preserved exactly for its own owner");
}

/* --------------------------------------------------------- window rejection */

void future_and_committed_packets_are_rejected()
{
    DualInputWindowV1 window;
    check(bind(window) == DualWindowStatusV1::Ok,
          "the window binds for the rejection cases");

    const auto local = samples(product::NES_A, 0, 0, 0);
    const auto base = bundle_for(key_for(0, 1), 0, key_id(0x55), local);
    check(window.deliver_input(base) == DualWindowStatusV1::Accepted,
          "the base frame is accepted");

    /* A future packet further than the hard ring is refused outright. */
    DualInputWindowV1 far_window;
    check(bind(far_window) == DualWindowStatusV1::Ok, "the far window binds");
    const auto far_input = samples(product::NES_A, 0, 0, 0);
    const auto far_bundle = bundle_for(
        key_for(dual::kDualMaxRollbackFramesV1 + 4, 1), 0, key_id(0x55),
        far_input);
    check(far_window.deliver_input(far_bundle) ==
              DualWindowStatusV1::RejectedWindowExceeded,
          "a future packet beyond the hard ring is rejected");

    /* Plan frame zero (leaving a prediction) and then make it fully real; a
     * different byte stream afterwards is committed history. */
    const auto plan = window.plan_next();
    check(plan.status == DualWindowStatusV1::Ok, "frame zero plans");
    check(window.mark_planned(0, plan.bundle.predicted_port_mask) ==
              DualWindowStatusV1::Ok,
          "frame zero is recorded as planned");
    const auto peer = samples(0, product::NES_UP, 0, 0);
    const auto real_peer = bundle_for(key_for(0, 1), 1, key_id(0x77), peer);
    check(window.deliver_input(real_peer) == DualWindowStatusV1::Accepted,
          "the peer's real sample for the planned frame is accepted");
    check(window.frame_all_real(0), "the frame becomes fully real");
    check(window.deliver_input(real_peer) == DualWindowStatusV1::Duplicate,
          "the very same bytes are only a duplicate");
    const auto conflicting = samples(0, product::NES_B, 0, 0);
    const auto rewrite = bundle_for(key_for(0, 1), 1, key_id(0x77),
                                    conflicting);
    check(window.deliver_input(rewrite) ==
              DualWindowStatusV1::RejectedCommittedHistory,
          "different bytes for a fully real frame are committed history");

    /* A frame that would need a thirteenth ring slot is refused: one frame is
     * already retained and the hard ring holds exactly twelve. */
    DualInputWindowV1 old_window;
    check(bind(old_window) == DualWindowStatusV1::Ok, "the old window binds");
    const auto old_frame_input = samples(product::NES_A, 0, 0, 0);
    check(old_window.deliver_input(
              bundle_for(key_for(0, 1), 0, key_id(0x55), old_frame_input)) ==
              DualWindowStatusV1::Accepted,
          "the old window accepts its base frame");
    const auto beyond_ring = samples(product::NES_B, 0, 0, 0);
    const auto too_far = bundle_for(
        key_for(dual::kDualMaxRollbackFramesV1, 1), 0, key_id(0x55),
        beyond_ring);
    check(old_window.deliver_input(too_far) ==
              DualWindowStatusV1::RejectedWindowExceeded,
          "a frame needing a thirteenth ring slot is refused");
    check(old_window.plan_next().status == DualWindowStatusV1::Ok,
          "the retained frame still plans after the refusal");
}

void identity_branch_and_revision_are_enforced()
{
    DualInputWindowV1 window;
    check(bind(window) == DualWindowStatusV1::Ok,
          "the window binds for the identity cases");

    const auto local = samples(product::NES_A, 0, 0, 0);
    const auto good = bundle_for(key_for(0, 1), 0, key_id(0x55), local);
    check(window.deliver_input(good) == DualWindowStatusV1::Accepted,
          "a correctly owned bundle is accepted");
    check(window.deliver_input(good) == DualWindowStatusV1::Duplicate,
          "an exact retransmission is only a duplicate");

    const auto wrong_seat = samples(product::NES_B, 0, 0, 0);
    const auto wrong_seat_bundle = bundle_for(key_for(0, 1), 2, key_id(0x55),
                                              wrong_seat);
    check(window.deliver_input(wrong_seat_bundle) ==
              DualWindowStatusV1::RejectedWrongOwner,
          "a bundle claiming an unowned seat is rejected");

    const auto unknown_key = samples(product::NES_B, 0, 0, 0);
    const auto unknown_key_bundle = bundle_for(key_for(0, 1), 0, key_id(0x66),
                                               unknown_key);
    check(window.deliver_input(unknown_key_bundle) ==
              DualWindowStatusV1::RejectedWrongOwner,
          "a bundle signed by an unknown session key is rejected");

    const auto cross_branch_input = samples(product::NES_B, 0, 0, 0);
    auto cross_branch_key = key_for(0, 1);
    cross_branch_key.branch_id = id(0x99);
    const auto cross_branch_bundle = bundle_for(cross_branch_key, 0,
                                                key_id(0x55),
                                                cross_branch_input);
    check(window.deliver_input(cross_branch_bundle) ==
              DualWindowStatusV1::RejectedOtherBranch,
          "a cross branch packet is rejected");

    const auto cross_epoch_input = samples(product::NES_B, 0, 0, 0);
    auto cross_epoch_key = key_for(0, 1);
    cross_epoch_key.timeline_epoch = 4;
    const auto cross_epoch_bundle = bundle_for(cross_epoch_key, 0, key_id(0x55),
                                               cross_epoch_input);
    check(window.deliver_input(cross_epoch_bundle) ==
              DualWindowStatusV1::RejectedOtherTimeline,
          "a packet from another timeline epoch is rejected");

    DualInputWindowV1 current;
    check(current.begin(key_for(0, 2), owners()) == DualWindowStatusV1::Ok,
          "a window binds with seat revision two");
    const auto stale_input = samples(product::NES_A, 0, 0, 0);
    const auto stale_bundle = bundle_for(key_for(0, 1), 0, key_id(0x55),
                                         stale_input);
    check(current.deliver_input(stale_bundle) ==
              DualWindowStatusV1::RejectedStaleRevision,
          "a stale global seat revision is rejected");

    /* A revision bump inside the window must not let the old revision rewrite a
     * frame the new revision already owns. */
    DualInputWindowV1 bumping;
    check(bind(bumping) == DualWindowStatusV1::Ok, "the bumping window binds");
    const auto fresh_input = samples(product::NES_A, 0, 0, 0);
    check(bumping.deliver_input(bundle_for(key_for(0, 2), 0, key_id(0x55),
                                           fresh_input)) ==
              DualWindowStatusV1::Accepted,
          "a newer seat revision is accepted");
    const auto revived_input = samples(product::NES_B, 0, 0, 0);
    const auto revived = bundle_for(key_for(1, 1), 0, key_id(0x55),
                                    revived_input);
    check(bumping.deliver_input(revived) ==
              DualWindowStatusV1::RejectedStaleRevision,
          "the superseded seat revision cannot enter the window afterwards");
}

/* ------------------------------------------------------- prediction freeze */

void prediction_depth_freezes_at_ten_frames()
{
    DualInputWindowV1 window;
    check(bind(window) == DualWindowStatusV1::Ok, "the freeze window binds");

    const auto only_port_zero = samples(product::NES_A, 0, 0, 0);
    check(window.deliver_input(bundle_for(key_for(0, 1), 0, key_id(0x55),
                                          only_port_zero)) ==
              DualWindowStatusV1::Accepted,
          "only port zero is confirmed");

    for (std::uint64_t frame = 0; frame < dual::kDualPredictionFreezeDepthV1;
         ++frame) {
        const auto plan = window.plan_next();
        if (frame < 3)
        check(plan.status == DualWindowStatusV1::Ok,
              "a frame inside the prediction budget plans");
        check(plan.frame_index == frame, "frames plan in order");
        check(plan.prediction_depth == frame,
              "the reported prediction depth is exact");
        check(plan.freeze == dual::DualFreezeReasonV1::None,
              "no freeze before the budget is exhausted");
        check(window.mark_planned(frame, plan.bundle.predicted_port_mask) ==
                  DualWindowStatusV1::Ok,
              "the planned frame is recorded");
    }

    const auto frozen = window.plan_next();
    check(frozen.status == DualWindowStatusV1::Frozen,
          "planning at depth ten freezes");
    check(frozen.freeze == dual::DualFreezeReasonV1::PredictionDepthExceeded,
          "the freeze reason is the prediction depth");
    check(frozen.prediction_depth == dual::kDualPredictionFreezeDepthV1,
          "the frozen plan reports depth ten");

    /* The hard ring is never exceeded. */
    DualInputWindowV1 ring;
    check(bind(ring) == DualWindowStatusV1::Ok, "the ring window binds");
    const std::uint64_t beyond =
        dual::kDualMaxRollbackFramesV1 + dual::kDualPredictionFreezeDepthV1 + 1;
    const auto future = samples(product::NES_B, 0, 0, 0);
    const auto future_bundle = bundle_for(key_for(beyond, 1), 0, key_id(0x55),
                                          future);
    check(ring.deliver_input(future_bundle) ==
              DualWindowStatusV1::RejectedWindowExceeded,
          "the hard twelve frame ring is never exceeded");
    check(dual::kDualPredictionFreezeDepthV1 < dual::kDualMaxRollbackFramesV1,
          "prediction freezes before the hard ring bound");
}

void late_real_input_replaces_a_prediction()
{
    DualInputWindowV1 window;
    check(bind(window) == DualWindowStatusV1::Ok, "the replay window binds");

    const auto local = samples(product::NES_A, 0, 0, 0);
    check(window.deliver_input(bundle_for(key_for(0, 1), 0, key_id(0x55),
                                          local)) ==
              DualWindowStatusV1::Accepted,
          "the base frame is accepted");
    const auto plan = window.plan_next();
    check(plan.status == DualWindowStatusV1::Ok, "frame zero plans");
    check(window.mark_planned(0, plan.bundle.predicted_port_mask) ==
              DualWindowStatusV1::Ok,
          "frame zero is planned with predictions");
    const auto predicted_before = window.frame_predicted_mask(0);
    check(predicted_before.has_value(), "the frame record is readable");
    check((*predicted_before & 0x2u) != 0, "port one starts predicted");

    auto peer = samples(0, product::NES_B, 0, 0);
    peer[1].sequence = peer[0].sequence + 1;
    const auto late = bundle_for(key_for(0, 1), 1, key_id(0x77), peer);
    check(window.deliver_input(late) == DualWindowStatusV1::Accepted,
          "a late trusted input inside the ring is accepted");
    check(window.frame_all_real(0),
          "both in-play ports are real once the peer reported");
    const auto predicted_after = window.frame_predicted_mask(0);
    check(predicted_after.has_value() && (*predicted_after & 0x2u) == 0,
          "the served port is no longer predicted");
    check(window.commit_frontier() == 1,
          "the committed watermark follows the now fully real frame");
    check(window.deliver_input(late) == DualWindowStatusV1::Duplicate,
          "the identical late bundle is only a duplicate");
}

void canonical_bundle_is_identical_for_any_arrival_order()
{
    /* Acceptance criterion DUAL-INPUT: for any message arrival order the two
     * ends materialize the same canonical bundle for the same frame. */
    const auto raw = samples(product::NES_A | product::NES_UP, product::NES_B,
                             product::NES_DOWN, product::NES_START);

    DualInputWindowV1 first_order;
    DualInputWindowV1 second_order;
    check(bind(first_order) == DualWindowStatusV1::Ok &&
              bind(second_order) == DualWindowStatusV1::Ok,
          "both ordering windows bind");

    check(first_order.deliver_input(
              bundle_for(key_for(0, 1), 0, key_id(0x55), raw)) ==
              DualWindowStatusV1::Accepted,
          "the ascending order accepts the first owner");
    check(first_order.deliver_input(
              bundle_for(key_for(0, 1), 1, key_id(0x77), raw)) ==
              DualWindowStatusV1::Accepted,
          "the ascending order accepts the second owner");

    check(second_order.deliver_input(
              bundle_for(key_for(0, 1), 1, key_id(0x77), raw)) ==
              DualWindowStatusV1::Accepted,
          "the descending order accepts the second owner");
    check(second_order.deliver_input(
              bundle_for(key_for(0, 1), 0, key_id(0x55), raw)) ==
              DualWindowStatusV1::Accepted,
          "the descending order accepts the first owner");

    const auto first_plan = first_order.plan_next();
    const auto second_plan = second_order.plan_next();
    check(first_plan.status == DualWindowStatusV1::Ok &&
              second_plan.status == DualWindowStatusV1::Ok,
          "both orders plan the frame");
    check(first_plan.bundle == second_plan.bundle,
          "any arrival order yields the same canonical bundle");
    check(dual::canonical_input_digest_v1(first_plan.bundle) ==
              dual::canonical_input_digest_v1(second_plan.bundle),
          "any arrival order yields the same canonical digest");
    check(first_plan.bundle.predicted_port_mask == 0,
          "a fully delivered frame carries no prediction");
    check(first_plan.bundle.ports[0].mask ==
              (product::NES_A | product::NES_UP),
          "a legal diagonal plus a button survives normalization byte for byte");
    check(first_plan.bundle.ports[1].mask == product::NES_B,
          "the second in-play port carries its own owner's sample");
}

} // namespace

int main()
{
    prediction_is_filled_from_last_known_state();
    conflicting_directions_are_cleared_before_the_window();
    future_and_committed_packets_are_rejected();
    identity_branch_and_revision_are_enforced();
    prediction_depth_freezes_at_ten_frames();
    late_real_input_replaces_a_prediction();
    canonical_bundle_is_identical_for_any_arrival_order();

    if (failures != 0) {
        std::fprintf(stderr, "%d dual input window checks failed\n", failures);
        return 1;
    }
    std::puts("dual input window tests passed");
    return 0;
}
