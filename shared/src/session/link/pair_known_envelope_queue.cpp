#include "pair_known_envelope_queue.hpp"

#include <utility>

namespace flynes::session::link {

namespace {

constexpr std::uint8_t kPairKnownStatusType = 21;
constexpr std::uint8_t kPairKnownBranchType = 17;

// The two status messages are the only envelopes this queue may ever hold, and
// each one is bound to an exact counter inside its own branch of the exchange.
// Rejecting anything else here keeps the negative cases negative: a wrong role,
// a reflection, a truncated body or a replayed counter still fails the link
// immediately instead of parking in the queue until a decoder exists.
bool legal_status_envelope(std::uint8_t logical_type,
                           const std::uint8_t* body, std::size_t size,
                           const std::array<std::uint8_t, 32>& logical_hash)
{
    if (logical_type != kPairKnownStatusType &&
        logical_type != kPairKnownBranchType)
        return false;
    bool nonzero_hash = false;
    for (const auto byte : logical_hash)
        nonzero_hash = nonzero_hash || byte != 0;
    if (!nonzero_hash) return false;
    wire::PairSecureEnvelopeV1 envelope{};
    if (wire::decode_pair_secure_envelope_v1(
            logical_type, body, size, &envelope) != wire::Status::Ok)
        return false;
    return envelope.message_counter ==
        (logical_type == kPairKnownStatusType ? 2U : 3U);
}

} // namespace

PairKnownEnvelopeQueue::PairKnownEnvelopeQueue(
    std::size_t maximum_bytes, std::size_t maximum_records) noexcept
    : maximum_bytes_(maximum_bytes), maximum_records_(maximum_records)
{
}

PairKnownQueueResult PairKnownEnvelopeQueue::enqueue(
    std::uint8_t logical_type, const std::uint8_t* body, std::size_t size,
    const std::array<std::uint8_t, 32>& logical_hash)
{
    if (maximum_bytes_ == 0 || maximum_records_ == 0 || !body || size == 0)
        return PairKnownQueueResult::InvalidArgument;
    if (failed_)
        return PairKnownQueueResult::ProtocolViolation;
    if (!legal_status_envelope(logical_type, body, size, logical_hash))
        return fail(PairKnownQueueResult::ProtocolViolation);

    const std::size_t record_bytes = size + logical_hash.size() + 1;
    if (records_.size() >= maximum_records_ || record_bytes > maximum_bytes_ ||
        bytes_ > maximum_bytes_ - record_bytes)
    {
        return fail(PairKnownQueueResult::Backpressure);
    }

    QueuedPairKnownEnvelope record{};
    record.logical_type = logical_type;
    record.logical_hash = logical_hash;
    record.body.assign(body, body + size);
    records_.push_back(std::move(record));
    bytes_ += record_bytes;
    // A new arrival makes the queue drainable again. Without this a hold that
    // somehow outlives one release would accept the bytes and then never hand them
    // over, which loses a legal message silently - the failure mode this whole
    // mechanism exists to remove.
    released_ = false;
    return PairKnownQueueResult::Queued;
}

PairKnownQueueResult PairKnownEnvelopeQueue::release(
    std::vector<QueuedPairKnownEnvelope>* output)
{
    if (!output)
        return PairKnownQueueResult::InvalidArgument;
    if (failed_)
        return PairKnownQueueResult::ProtocolViolation;
    if (released_)
        return PairKnownQueueResult::Released;

    output->reserve(output->size() + records_.size());
    for (auto& record : records_)
        output->push_back(std::move(record));
    records_.clear();
    bytes_ = 0;
    released_ = true;
    return PairKnownQueueResult::Released;
}

void PairKnownEnvelopeQueue::discard() noexcept
{
    records_.clear();
    bytes_ = 0;
    // Teardown clears the released latch too, so the queue can serve the next link.
    // `failed_` stays sticky on purpose: a queue that overflowed must not be reused.
    released_ = false;
}

PairKnownQueueResult PairKnownEnvelopeQueue::fail(
    PairKnownQueueResult result) noexcept
{
    discard();
    failed_ = true;
    return result;
}

} // namespace flynes::session::link
