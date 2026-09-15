#ifndef FLYNES_SESSION_WIRE_QUIC_STREAM_GATE_HPP
#define FLYNES_SESSION_WIRE_QUIC_STREAM_GATE_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace flynes::session::wire {

enum class BindingKindV1 : std::uint8_t
{
    Initial = 1,
    SamePathReconnect = 2,
    NewBearerReconnect = 3
};

enum class TlsSideV1 : std::uint8_t
{
    Connector = 1,
    Listener = 2
};

enum class QuicGateResult : std::int32_t
{
    NeedMore = 1,
    Accepted = 2,
    PrebindComplete = 3,
    ChannelBound = 4,
    Closed = -1,
    Backpressure = -2,
    ProtocolViolation = -3,
    InvalidArgument = -4
};

// Framing gate only. Cryptographic validation of every contained record is a
// separate reducer gate and must succeed before callers mark either direction
// complete. No ordinary application stream may be released before ChannelBound.
class QuicStreamGate
{
public:
    QuicStreamGate(BindingKindV1 binding_kind,
                   bool pair_initiator_is_connector) noexcept;

    QuicGateResult open_bidi(std::uint64_t stream_id, TlsSideV1 opener) noexcept;
    QuicGateResult data(std::uint64_t stream_id, TlsSideV1 sender,
                        const std::uint8_t* bytes, std::size_t size);
    QuicGateResult finish(std::uint64_t stream_id, TlsSideV1 sender) noexcept;

    [[nodiscard]] bool normal_app_stream_allowed() const noexcept;
    [[nodiscard]] bool failed() const noexcept;

private:
    struct Direction
    {
        std::vector<std::uint8_t> bytes;
        std::size_t record_index = 0;
        bool fin = false;
    };

    enum class Phase { ExpectPrebind, Prebind, ExpectBind, Bind, Bound, Failed };
    QuicGateResult fail(QuicGateResult result) noexcept;
    QuicGateResult parse(Direction& direction, TlsSideV1 sender);
    Direction& direction(TlsSideV1 sender) noexcept;
    const Direction& direction(TlsSideV1 sender) const noexcept;
    std::size_t expected_count(TlsSideV1 sender) const noexcept;

    bool initiator_is_connector_ = false;
    Phase phase_ = Phase::Failed;
    std::uint64_t stream_id_ = 0;
    std::uint64_t prior_stream_id_ = 0;
    bool preamble_complete_ = false;
    Direction connector_{};
    Direction listener_{};
};

} // namespace flynes::session::wire

#endif
