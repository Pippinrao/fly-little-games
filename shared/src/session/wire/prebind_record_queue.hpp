#ifndef FLYNES_SESSION_WIRE_PREBIND_RECORD_QUEUE_HPP
#define FLYNES_SESSION_WIRE_PREBIND_RECORD_QUEUE_HPP

#include "app_stream_assembler.hpp"

#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace flynes::session::wire {

enum class PreBindQueueResult : std::int32_t
{
    Queued = 1,
    Blocked = 2,
    Released = 3,
    Closed = -1,
    Backpressure = -2,
    ProtocolViolation = -3,
    InvalidArgument = -4
};

struct QueuedAppRecord
{
    std::uint64_t stream_id = 0;
    std::uint64_t stream_sequence = 0;
    OwnedAppFrame frame;
};

// Holds already-decoded, legal application frames that arrived before all
// authentication and link gates completed. Nothing leaves this object until
// TLS/pin/exporter, ChannelBind and both LINK_READY directions are established.
class PreBindRecordQueue
{
public:
    PreBindRecordQueue(std::size_t maximum_bytes,
                       std::size_t maximum_records) noexcept;

    PreBindQueueResult enqueue(std::uint64_t stream_id,
                               std::uint64_t stream_sequence,
                               OwnedAppFrame frame);
    PreBindQueueResult mark_transport_authenticated(bool full_tls13,
                                                     bool pin_verified,
                                                     bool exporter_verified) noexcept;
    PreBindQueueResult mark_channel_bound() noexcept;
    PreBindQueueResult mark_link_ready(bool local_ready,
                                       bool remote_ready) noexcept;
    PreBindQueueResult release(std::vector<QueuedAppRecord>* output);
    void discard() noexcept;

    [[nodiscard]] std::size_t buffered_bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t buffered_records() const noexcept
    {
        return records_.size();
    }
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    PreBindQueueResult fail(PreBindQueueResult result) noexcept;
    [[nodiscard]] bool open() const noexcept;
    [[nodiscard]] PreBindQueueResult gate_result() const noexcept;

    std::size_t maximum_bytes_ = 0;
    std::size_t maximum_records_ = 0;
    std::size_t bytes_ = 0;
    std::vector<QueuedAppRecord> records_;
    std::set<std::pair<std::uint64_t, std::uint64_t>> identities_;
    bool transport_authenticated_ = false;
    bool channel_bound_ = false;
    bool local_ready_ = false;
    bool remote_ready_ = false;
    bool released_ = false;
    bool failed_ = false;
};

} // namespace flynes::session::wire

#endif
