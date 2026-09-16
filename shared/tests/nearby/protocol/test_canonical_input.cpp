/*
 * W2 / Task 5: exact canonical DUAL input golden contract.
 *
 * The canonical bundle is the only input unit the DUAL seam ever carries. It
 * binds session/branch/epoch/frame, the global seat revision, the logical seat,
 * the owner session signing key, a monotonic per-port input sequence and the
 * normalized complete four-port mask. Opposing d-pad directions are cleared
 * before anything is transmitted, never resolved arbitrarily.
 */

#include "dual/canonical_input.hpp"
#include "flynes/flynes_runtime.h"
#include "flynes/product/nes_input_bits.hpp"

#include <array>
#include <cstdio>

namespace {

namespace dual = flynes::session::dual;
namespace product = flynes::product;

using dual::DualInputStatusV1;
using dual::DualPortInputV1;

int failures = 0;

void check(bool value, const char* message)
{
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

constexpr std::uint32_t kFullMask = dual::kDualFullPortMaskV1;
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

dual::DualInputKeyV1 key_for(std::uint64_t frame,
                             std::uint64_t seat_revision = 1)
{
    dual::DualInputKeyV1 key{};
    key.session_id = id(0x11);
    key.branch_id = id(0x22);
    key.timeline_epoch = 7;
    key.frame_index = frame;
    key.seat_revision = seat_revision;
    return key;
}

std::array<DualPortInputV1, dual::kDualPortCountV1> samples(
    std::uint32_t port_zero, std::uint32_t port_one,
    std::uint32_t port_two, std::uint32_t port_three)
{
    std::array<DualPortInputV1, dual::kDualPortCountV1> out{};
    out[0] = {port_zero, 10};
    out[1] = {port_one, 20};
    out[2] = {port_two, 30};
    out[3] = {port_three, 40};
    return out;
}

/* --------------------------------------------------------- golden contract */

void exact_canonical_bundle_contract()
{
    /* 0x10|0x20 (UP+DOWN), 0x40|0x80 (LEFT+RIGHT), and two clean pads. */
    const auto raw = samples(0x30u, 0xC0u, product::NES_A | product::NES_START,
                             product::NES_UP | product::NES_RIGHT);
    dual::DualInputBundleV1 bundle{};

    check(dual::canonical_input_build_v1(key_for(41), 1, key_id(0x33), raw,
                                         &bundle) == DualInputStatusV1::Ok,
          "a well formed bundle canonicalizes");

    check(bundle.key == key_for(41), "the bundle carries the exact input key");
    check(bundle.logical_seat == 1, "the bundle carries the logical seat");
    check(bundle.owner_signing_key_id == key_id(0x33),
          "the bundle carries the owner session signing key id");
    check(bundle.predicted_port_mask == 0,
          "a bundle built from confirmed samples claims no prediction");

    check(bundle.ports[0].mask == 0, "UP+DOWN on port 0 is neutralized");
    check(bundle.ports[1].mask == 0, "LEFT+RIGHT on port 1 is neutralized");
    check(bundle.ports[2].mask == (product::NES_A | product::NES_START),
          "port 2 keeps its legal mask");
    check(bundle.ports[3].mask == (product::NES_UP | product::NES_RIGHT),
          "port 3 keeps its legal diagonal");

    check(bundle.ports[0].input_sequence == 10 &&
              bundle.ports[1].input_sequence == 20 &&
              bundle.ports[2].input_sequence == 30 &&
              bundle.ports[3].input_sequence == 40,
          "every port keeps its own monotonic input sequence");

    check(dual::canonical_input_is_canonical_v1(bundle),
          "the produced bundle validates as canonical");

    /* The four legal buttons with no direction bit survive normalization
     * unchanged; normalization only ever clears opposing d-pad pairs. */
    const auto buttons_only = samples(0x0Fu, 0, 0, 0);
    dual::DualInputBundleV1 flattened{};
    check(dual::canonical_input_build_v1(key_for(42), 1, key_id(0x33),
                                         buttons_only, &flattened) ==
              DualInputStatusV1::Ok,
          "a buttons-only pad canonicalizes");
    check(flattened.ports[0].mask == 0x0Fu,
          "a buttons-only pad is preserved exactly");
    check(dual::canonical_input_is_canonical_v1(flattened),
          "the flattened bundle is canonical");

    /* Both opposing pairs at once clear together and leave the buttons. */
    const auto both_pairs = samples(0x30u | 0xC0u | 0x0Fu, 0, 0, 0);
    dual::DualInputBundleV1 cleared{};
    check(dual::canonical_input_build_v1(key_for(42), 1, key_id(0x33),
                                         both_pairs, &cleared) ==
              DualInputStatusV1::Ok,
          "both opposing d-pad pairs still canonicalize");
    check(cleared.ports[0].mask == 0x0Fu,
          "both opposing pairs clear together and keep the buttons");

    /* A mask with bits above the eight button pad is refused, not masked. */
    const auto out_of_range = samples(0x100u, 0, 0, 0);
    dual::DualInputBundleV1 rejected{};
    check(dual::canonical_input_build_v1(key_for(42), 1, key_id(0x33),
                                         out_of_range, &rejected) ==
              DualInputStatusV1::InvalidMask,
          "a mask above the eight button pad is rejected");
}

void retransmission_is_key_and_bytes()
{
    const auto raw = samples(product::NES_A, 0, 0, 0);
    dual::DualInputBundleV1 first{};
    dual::DualInputBundleV1 second{};
    check(dual::canonical_input_build_v1(key_for(9), 0, key_id(1), raw, &first) ==
              DualInputStatusV1::Ok,
          "first build succeeds");
    check(dual::canonical_input_build_v1(key_for(9), 0, key_id(1), raw, &second) ==
              DualInputStatusV1::Ok,
          "identical rebuild succeeds");
    check(first == second, "same key and same bytes is an exact duplicate");
    check(dual::canonical_input_is_duplicate_v1(first, second),
          "duplicate detection is key plus bytes");

    auto changed_mask = second;
    changed_mask.ports[0].mask = product::NES_B;
    check(dual::canonical_input_is_duplicate_v1(first, changed_mask) == false,
          "same key with different mask bytes is not a duplicate");

    auto changed_sequence = second;
    changed_sequence.ports[0].input_sequence = 11;
    check(dual::canonical_input_is_duplicate_v1(first, changed_sequence) == false,
          "same key with a different sequence is not a duplicate");

    auto changed_key = second;
    changed_key.key.frame_index = 10;
    check(dual::canonical_input_is_duplicate_v1(first, changed_key) == false,
          "a different key is never a duplicate");
}

void canonical_validation_rejects_impossible_states()
{
    const auto raw = samples(product::NES_A, 0, 0, 0);
    dual::DualInputBundleV1 bundle{};
    check(dual::canonical_input_build_v1(key_for(3), 0, key_id(1), raw, &bundle) ==
              DualInputStatusV1::Ok,
          "validation fixture builds");

    auto conflicting = bundle;
    conflicting.ports[2].mask = product::NES_UP | product::NES_DOWN;
    check(!dual::canonical_input_is_canonical_v1(conflicting),
          "a bundle carrying UP+DOWN is not canonical");

    auto partial = bundle;
    partial.ports[3].mask = kFullMask | 0x100u;
    check(!dual::canonical_input_is_canonical_v1(partial),
          "a mask with bits above the pad state is not canonical");

    auto zero_sequence = bundle;
    zero_sequence.ports[1].input_sequence = 0;
    check(!dual::canonical_input_is_canonical_v1(zero_sequence),
          "sequence zero is never canonical");

    auto unsorted = bundle;
    unsorted.ports[1].input_sequence = unsorted.ports[0].input_sequence;
    check(!dual::canonical_input_is_canonical_v1(unsorted),
          "equal neighbour sequences are not canonical");

    auto bad_key = bundle;
    bad_key.key.seat_revision = 0;
    check(!dual::canonical_input_is_canonical_v1(bad_key),
          "a zero seat revision is not a canonical key");

    auto no_owner = bundle;
    no_owner.owner_signing_key_id = {};
    check(!dual::canonical_input_is_canonical_v1(no_owner),
          "a missing owner signing key is not canonical");

    auto predicted = bundle;
    predicted.predicted_port_mask = 0x1u;
    check(dual::canonical_input_is_canonical_v1(predicted),
          "a prediction marker does not break canonical form");
}

void malformed_input_is_rejected_before_encoding()
{
    const auto raw = samples(product::NES_A, 0, 0, 0);
    dual::DualInputBundleV1 bundle{};

    check(dual::canonical_input_build_v1(key_for(1), 0, key_id(1), raw, nullptr) ==
              DualInputStatusV1::InvalidArgument,
          "a null output is rejected");

    auto zero_epoch = key_for(1);
    zero_epoch.timeline_epoch = 0;
    check(dual::canonical_input_build_v1(zero_epoch, 0, key_id(1), raw, &bundle) ==
              DualInputStatusV1::InvalidKey,
          "a zero timeline epoch is rejected");

    auto zero_revision = key_for(1, 0);
    check(dual::canonical_input_build_v1(zero_revision, 0, key_id(1), raw,
                                         &bundle) ==
              DualInputStatusV1::InvalidKey,
          "a zero seat revision is rejected");

    auto zero_session = key_for(1);
    zero_session.session_id = {};
    check(dual::canonical_input_build_v1(zero_session, 0, key_id(1), raw,
                                         &bundle) == DualInputStatusV1::InvalidKey,
          "a zero session id is rejected");

    std::array<std::uint8_t, 32> zero_key{};
    check(dual::canonical_input_build_v1(key_for(1), 0, zero_key, raw, &bundle) ==
              DualInputStatusV1::InvalidKey,
          "a zero owner signing key is rejected");

    auto zero_sequence = raw;
    zero_sequence[2].sequence = 0;
    check(dual::canonical_input_build_v1(key_for(1), 0, key_id(1), zero_sequence,
                                         &bundle) ==
              DualInputStatusV1::InvalidSequence,
          "sequence zero is rejected");

    auto backwards = raw;
    backwards[2].sequence = backwards[1].sequence;
    check(dual::canonical_input_build_v1(key_for(1), 0, key_id(1), backwards,
                                         &bundle) ==
              DualInputStatusV1::InvalidSequence,
          "a non-monotonic sequence tuple is rejected");

    /* Overflow is rejected, never wrapped. */
    auto overflow = raw;
    overflow[3].sequence = UINT64_MAX;
    check(!dual::checked_dual_next_sequence_v1(overflow[3].sequence,
                                               &overflow[3].sequence),
          "the shared checked increment refuses to wrap at UINT64_MAX");
    std::uint64_t advanced = 0;
    check(dual::checked_dual_next_sequence_v1(41, &advanced) && advanced == 42,
          "a legal sequence advances by exactly one");
    check(!dual::checked_dual_next_sequence_v1(0, &advanced),
          "zero is never a legal sequence base");
}

void bundle_digest_is_order_independent()
{
    /* The same four canonical ports always hash the same, whatever order the
     * four samples were observed or arrived in. */
    dual::DualInputBundleV1 forward{};
    dual::DualInputBundleV1 reversed{};
    check(dual::canonical_input_build_v1(
              key_for(5), 1, key_id(9),
              samples(0x01u, 0x02u, 0x04u, 0x08u), &forward) ==
              DualInputStatusV1::Ok,
          "forward bundle builds");
    check(dual::canonical_input_build_v1(
              key_for(5), 1, key_id(9),
              samples(0x01u, 0x02u, 0x04u, 0x08u), &reversed) ==
              DualInputStatusV1::Ok,
          "rebuild of the same samples succeeds");
    check(dual::canonical_input_digest_v1(forward) ==
              dual::canonical_input_digest_v1(reversed),
          "canonical digest is stable for equal bundles");

    dual::DualInputBundleV1 other{};
    check(dual::canonical_input_build_v1(
              key_for(5), 1, key_id(9),
              samples(0x01u, 0x02u, 0x04u, 0x10u), &other) ==
              DualInputStatusV1::Ok,
          "a differing bundle builds");
    check(dual::canonical_input_digest_v1(forward) !=
              dual::canonical_input_digest_v1(other),
          "a differing port mask changes the digest");
}

} // namespace

int main()
{
    exact_canonical_bundle_contract();
    retransmission_is_key_and_bytes();
    canonical_validation_rejects_impossible_states();
    malformed_input_is_rejected_before_encoding();
    bundle_digest_is_order_independent();

    if (failures != 0) {
        std::fprintf(stderr, "%d canonical input checks failed\n", failures);
        return 1;
    }
    std::puts("canonical dual input tests passed");
    return 0;
}
