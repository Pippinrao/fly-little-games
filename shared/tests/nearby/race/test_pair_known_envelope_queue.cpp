#include "link/pair_known_envelope_queue.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

/*
 * Early-arrival acceptance fixes: the responder's PairKnownScheduler is created
 * only once its own SAS stage completes, while the initiator publishes
 * PairKnownStatus as soon as its SAS is ready. The status that arrives inside
 * that window is legal, and it must be held and replayed in arrival order rather
 * than failing the link. Everything that is not legal for the live link
 * generation must still be rejected on the spot.
 */

namespace {

namespace wire = flynes::session::wire;

using flynes::session::link::PairKnownEnvelopeQueue;
using flynes::session::link::PairKnownQueueResult;
using flynes::session::link::QueuedPairKnownEnvelope;

int failures = 0;

void check(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::vector<std::uint8_t> envelope(std::uint8_t type,
                                   std::uint64_t counter)
{
    std::size_t inner = 0;
    if (wire::pair_secure_inner_size_v1(type, &inner) != wire::Status::Ok)
        return {};
    std::vector<std::uint8_t> ciphertext(inner + 16, 0x5a);
    std::vector<std::uint8_t> body;
    if (wire::encode_pair_secure_envelope_v1(
            type, counter, ciphertext.data(), ciphertext.size(), &body) !=
        wire::Status::Ok)
        return {};
    return body;
}

std::array<std::uint8_t, 32> hash(std::uint8_t seed)
{
    std::array<std::uint8_t, 32> value{};
    value[0] = seed;
    value[31] = static_cast<std::uint8_t>(seed ^ 0xa5);
    return value;
}

void test_early_status_and_branch_replay_in_arrival_order()
{
    const auto status = envelope(21, 2);
    const auto branch = envelope(17, 3);
    check(status.size() == wire::kKnownStatusInnerSizeV1 + 16 + 12 &&
              branch.size() == wire::kKnownBranchInnerSizeV1 + 16 + 12,
          "fixture envelopes carry the exact pair-secure body sizes");
    if (status.empty() || branch.empty()) return;

    PairKnownEnvelopeQueue queue(512, 4);
    check(queue.enqueue(21, status.data(), status.size(), hash(1)) ==
              PairKnownQueueResult::Queued,
          "an early legal status is held instead of failing the link");
    check(queue.enqueue(17, branch.data(), branch.size(), hash(2)) ==
              PairKnownQueueResult::Queued,
          "an early legal branch is held too");
    check(queue.buffered_records() == 2 && queue.buffered_bytes() != 0,
          "held envelopes count against the bounded budget");

    std::vector<QueuedPairKnownEnvelope> released;
    check(queue.release(&released) == PairKnownQueueResult::Released &&
              released.size() == 2 && released[0].logical_type == 21 &&
              released[1].logical_type == 17,
          "envelopes replay once in arrival order");
    if (released.size() != 2) return;
    check(released[0].body == status && released[1].body == branch &&
              released[0].logical_hash == hash(1) &&
              released[1].logical_hash == hash(2),
          "replay preserves the exact body bytes and logical hash");
    check(queue.buffered_records() == 0 && queue.buffered_bytes() == 0,
          "released envelopes no longer count against the budget");
    // A repeated release must hand over nothing, and the check has to look at a
    // fresh vector: reusing `released` would leave its two old records in place and
    // pass without the queue having drained anything at all.
    std::vector<QueuedPairKnownEnvelope> repeated;
    check(queue.release(&repeated) == PairKnownQueueResult::Released &&
              repeated.empty(),
          "a repeated release cannot duplicate decoder input");

    // A legal envelope that arrives after one release must still be handed over.
    // Accepting it and then never delivering it would lose a message silently, which
    // is the failure mode this whole hold exists to remove.
    check(queue.enqueue(21, status.data(), status.size(), hash(3)) ==
              PairKnownQueueResult::Queued,
          "a later legal status is held again after a release");
    std::vector<QueuedPairKnownEnvelope> after_release;
    check(queue.release(&after_release) == PairKnownQueueResult::Released &&
              after_release.size() == 1 && after_release[0].body == status &&
              after_release[0].logical_hash == hash(3),
          "an envelope held after a release is handed over exactly once");
}

void test_illegal_envelopes_never_reach_the_queue()
{
    const auto status = envelope(21, 2);

    PairKnownEnvelopeQueue wrong_type(512, 4);
    check(wrong_type.enqueue(22, status.data(), status.size(), hash(1)) ==
              PairKnownQueueResult::ProtocolViolation &&
              wrong_type.failed() && wrong_type.buffered_records() == 0,
          "an unknown logical type still fails closed immediately");

    PairKnownEnvelopeQueue wrong_counter(512, 4);
    const auto branch_counter = envelope(21, 3);
    check(wrong_counter.enqueue(21, branch_counter.data(),
                                branch_counter.size(), hash(1)) ==
              PairKnownQueueResult::ProtocolViolation,
          "a status with the branch counter is not held");

    PairKnownEnvelopeQueue zero_hash(512, 4);
    const std::array<std::uint8_t, 32> empty_hash{};
    check(zero_hash.enqueue(21, status.data(), status.size(), empty_hash) ==
              PairKnownQueueResult::ProtocolViolation,
          "a zero logical hash is not held");

    PairKnownEnvelopeQueue truncated(512, 4);
    check(truncated.enqueue(21, status.data(), status.size() - 1, hash(1)) ==
              PairKnownQueueResult::ProtocolViolation,
          "a truncated body is not held");

    PairKnownEnvelopeQueue reflected(512, 4);
    auto reflected_body = status;
    reflected_body[0] = 2;
    check(reflected.enqueue(21, reflected_body.data(), reflected_body.size(),
                            hash(1)) ==
              PairKnownQueueResult::ProtocolViolation,
          "a wrong version byte is not held");
}

void test_capacity_fails_closed_and_discard_clears()
{
    const auto status = envelope(21, 2);

    PairKnownEnvelopeQueue over_count(4096, 1);
    check(over_count.enqueue(21, status.data(), status.size(), hash(1)) ==
              PairKnownQueueResult::Queued &&
              over_count.enqueue(21, status.data(), status.size(), hash(2)) ==
                  PairKnownQueueResult::Backpressure,
          "the record budget is enforced independently of the byte budget");
    check(over_count.failed() && over_count.buffered_records() == 0,
          "an over-count peer closes the queue and drops every held byte");

    PairKnownEnvelopeQueue over_bytes(status.size() + 32 + 1, 4);
    check(over_bytes.enqueue(21, status.data(), status.size(), hash(1)) ==
              PairKnownQueueResult::Queued &&
              over_bytes.enqueue(21, status.data(), status.size(), hash(2)) ==
                  PairKnownQueueResult::Backpressure,
          "the byte budget is enforced before retaining excess bytes");

    PairKnownEnvelopeQueue discarded(512, 4);
    check(discarded.enqueue(21, status.data(), status.size(), hash(1)) ==
              PairKnownQueueResult::Queued,
          "fixture held before a link teardown");
    discarded.discard();
    check(discarded.buffered_records() == 0 && discarded.buffered_bytes() == 0 &&
              !discarded.failed(),
          "a link teardown drops held envelopes without poisoning the queue");
}

} // namespace

int main()
{
    test_early_status_and_branch_replay_in_arrival_order();
    test_illegal_envelopes_never_reach_the_queue();
    test_capacity_fails_closed_and_discard_clears();
    if (failures != 0)
        return 1;
    std::printf("pair-known early-envelope queue passed\n");
    return 0;
}
