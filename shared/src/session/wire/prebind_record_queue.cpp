#include "prebind_record_queue.hpp"

#include <limits>
#include <utility>

namespace flynes::session::wire {

namespace {

constexpr std::size_t kAppFramePrefixBytes = 6;

} // namespace

PreBindRecordQueue::PreBindRecordQueue(std::size_t maximum_bytes,
                                       std::size_t maximum_records) noexcept
    : maximum_bytes_(maximum_bytes), maximum_records_(maximum_records)
{
}

PreBindQueueResult PreBindRecordQueue::enqueue(std::uint64_t stream_id,
                                               std::uint64_t stream_sequence,
                                               OwnedAppFrame frame)
{
    if (maximum_bytes_ == 0 || maximum_records_ == 0 || stream_id == 0 ||
        stream_sequence == 0 || frame.frame_type_tag == 0 ||
        frame.type_name.empty())
    {
        return PreBindQueueResult::InvalidArgument;
    }
    if (failed_ || released_ || open())
        return PreBindQueueResult::Closed;

    const auto identity = std::make_pair(stream_id, stream_sequence);
    if (identities_.find(identity) != identities_.end())
        return fail(PreBindQueueResult::ProtocolViolation);

    if (frame.object_bytes.size() >
        std::numeric_limits<std::size_t>::max() - kAppFramePrefixBytes)
    {
        return fail(PreBindQueueResult::Backpressure);
    }
    const std::size_t record_bytes = frame.object_bytes.size() + kAppFramePrefixBytes;
    if (records_.size() >= maximum_records_ || record_bytes > maximum_bytes_ ||
        bytes_ > maximum_bytes_ - record_bytes)
    {
        return fail(PreBindQueueResult::Backpressure);
    }

    identities_.insert(identity);
    records_.push_back(QueuedAppRecord{stream_id, stream_sequence, std::move(frame)});
    bytes_ += record_bytes;
    return PreBindQueueResult::Queued;
}

PreBindQueueResult PreBindRecordQueue::mark_transport_authenticated(
    bool full_tls13, bool pin_verified, bool exporter_verified) noexcept
{
    if (failed_ || released_)
        return PreBindQueueResult::Closed;
    if (!full_tls13 || !pin_verified || !exporter_verified)
        return fail(PreBindQueueResult::ProtocolViolation);
    transport_authenticated_ = true;
    return gate_result();
}

PreBindQueueResult PreBindRecordQueue::mark_channel_bound() noexcept
{
    if (failed_ || released_)
        return PreBindQueueResult::Closed;
    if (!transport_authenticated_)
        return fail(PreBindQueueResult::ProtocolViolation);
    channel_bound_ = true;
    return gate_result();
}

PreBindQueueResult PreBindRecordQueue::mark_link_ready(bool local_ready,
                                                       bool remote_ready) noexcept
{
    if (failed_ || released_)
        return PreBindQueueResult::Closed;
    if (!channel_bound_)
        return fail(PreBindQueueResult::ProtocolViolation);
    local_ready_ = local_ready_ || local_ready;
    remote_ready_ = remote_ready_ || remote_ready;
    return gate_result();
}

PreBindQueueResult PreBindRecordQueue::release(
    std::vector<QueuedAppRecord>* output)
{
    if (!output)
        return PreBindQueueResult::InvalidArgument;
    if (failed_)
        return PreBindQueueResult::Closed;
    if (released_)
        return PreBindQueueResult::Released;
    if (!open())
        return PreBindQueueResult::Blocked;

    output->reserve(output->size() + records_.size());
    for (auto& record : records_)
        output->push_back(std::move(record));
    records_.clear();
    identities_.clear();
    bytes_ = 0;
    released_ = true;
    return PreBindQueueResult::Released;
}

void PreBindRecordQueue::discard() noexcept
{
    records_.clear();
    identities_.clear();
    bytes_ = 0;
}

PreBindQueueResult PreBindRecordQueue::fail(PreBindQueueResult result) noexcept
{
    discard();
    failed_ = true;
    return result;
}

bool PreBindRecordQueue::open() const noexcept
{
    return transport_authenticated_ && channel_bound_ && local_ready_ && remote_ready_;
}

PreBindQueueResult PreBindRecordQueue::gate_result() const noexcept
{
    return open() ? PreBindQueueResult::Released : PreBindQueueResult::Blocked;
}

} // namespace flynes::session::wire
