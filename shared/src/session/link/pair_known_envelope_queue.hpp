#ifndef FLYNES_SESSION_LINK_PAIR_KNOWN_ENVELOPE_QUEUE_HPP
#define FLYNES_SESSION_LINK_PAIR_KNOWN_ENVELOPE_QUEUE_HPP

#include "../wire/pair_secure.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace flynes::session::link {

enum class PairKnownQueueResult : std::int32_t
{
    Queued = 1,
    Released = 2,
    Backpressure = -1,
    ProtocolViolation = -2,
    InvalidArgument = -3
};

struct QueuedPairKnownEnvelope
{
    std::uint8_t logical_type = 0;
    std::vector<std::uint8_t> body;
    std::array<std::uint8_t, 32> logical_hash{};
};

// Holds PairKnownStatus/PairKnownBranch envelopes that were structurally legal
// when they arrived but whose decoder, PairKnownScheduler, is still gated behind
// the local SAS stage. The link fails closed on overflow: a peer that floods
// this queue is a protocol violation, not a reason to allocate.
class PairKnownEnvelopeQueue
{
public:
    PairKnownEnvelopeQueue(std::size_t maximum_bytes,
                           std::size_t maximum_records) noexcept;

    PairKnownQueueResult enqueue(std::uint8_t logical_type,
                                 const std::uint8_t* body, std::size_t size,
                                 const std::array<std::uint8_t, 32>&
                                     logical_hash);
    PairKnownQueueResult release(
        std::vector<QueuedPairKnownEnvelope>* output);
    void discard() noexcept;

    [[nodiscard]] std::size_t buffered_bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t buffered_records() const noexcept
    {
        return records_.size();
    }
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    PairKnownQueueResult fail(PairKnownQueueResult result) noexcept;

    std::size_t maximum_bytes_ = 0;
    std::size_t maximum_records_ = 0;
    std::size_t bytes_ = 0;
    std::vector<QueuedPairKnownEnvelope> records_;
    bool released_ = false;
    bool failed_ = false;
};

} // namespace flynes::session::link

#endif
