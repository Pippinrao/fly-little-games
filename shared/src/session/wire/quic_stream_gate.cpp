#include "quic_stream_gate.hpp"

#include <algorithm>
#include <array>

namespace flynes::session::wire {
namespace {

struct ExpectedRecord
{
    std::uint32_t size;
    std::uint8_t first_byte;
};

constexpr std::array<ExpectedRecord, 5> kInitiatorPrebind{{
    {356, 1}, {356, 3}, {289, 13}, {181, 20}, {173, 14}}};
constexpr std::array<ExpectedRecord, 5> kResponderPrebind{{
    {356, 2}, {356, 4}, {181, 20}, {289, 13}, {173, 14}}};
constexpr std::array<ExpectedRecord, 2> kConnectorBind{{{248, 0}, {128, 0}}};
constexpr std::array<ExpectedRecord, 1> kListenerBind{{{248, 0}}};

std::uint32_t be32(const std::uint8_t* bytes) noexcept
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24u) |
           (static_cast<std::uint32_t>(bytes[1]) << 16u) |
           (static_cast<std::uint32_t>(bytes[2]) << 8u) |
           bytes[3];
}

bool valid_side(TlsSideV1 side) noexcept
{
    return side == TlsSideV1::Connector || side == TlsSideV1::Listener;
}

} // namespace

QuicStreamGate::QuicStreamGate(BindingKindV1 binding_kind,
                               bool pair_initiator_is_connector) noexcept
    : initiator_is_connector_(pair_initiator_is_connector)
{
    if (binding_kind == BindingKindV1::SamePathReconnect)
        phase_ = Phase::ExpectPrebind;
    else if (binding_kind == BindingKindV1::Initial ||
             binding_kind == BindingKindV1::NewBearerReconnect)
        phase_ = Phase::ExpectBind;
}

QuicGateResult QuicStreamGate::fail(QuicGateResult result) noexcept
{
    connector_.bytes.clear();
    listener_.bytes.clear();
    phase_ = Phase::Failed;
    return result;
}

QuicGateResult QuicStreamGate::open_bidi(std::uint64_t stream_id,
                                         TlsSideV1 opener) noexcept
{
    if (!valid_side(opener) || stream_id == 0)
        return fail(QuicGateResult::InvalidArgument);
    if ((phase_ != Phase::ExpectPrebind && phase_ != Phase::ExpectBind) ||
        opener != TlsSideV1::Connector || stream_id == prior_stream_id_)
        return fail(QuicGateResult::ProtocolViolation);
    stream_id_ = stream_id;
    connector_ = {};
    listener_ = {};
    preamble_complete_ = false;
    phase_ = phase_ == Phase::ExpectPrebind ? Phase::Prebind : Phase::Bind;
    return QuicGateResult::Accepted;
}

QuicStreamGate::Direction& QuicStreamGate::direction(TlsSideV1 sender) noexcept
{
    return sender == TlsSideV1::Connector ? connector_ : listener_;
}

const QuicStreamGate::Direction& QuicStreamGate::direction(
    TlsSideV1 sender) const noexcept
{
    return sender == TlsSideV1::Connector ? connector_ : listener_;
}

std::size_t QuicStreamGate::expected_count(TlsSideV1 sender) const noexcept
{
    if (phase_ == Phase::Bind)
        return sender == TlsSideV1::Connector ? kConnectorBind.size()
                                              : kListenerBind.size();
    return kInitiatorPrebind.size();
}

QuicGateResult QuicStreamGate::parse(Direction& value, TlsSideV1 sender)
{
    bool accepted = false;
    while (value.bytes.size() >= 4)
    {
        if (value.record_index >= expected_count(sender))
            return fail(QuicGateResult::ProtocolViolation);
        const bool sender_is_initiator =
            (sender == TlsSideV1::Connector) == initiator_is_connector_;
        const ExpectedRecord expected = phase_ == Phase::Bind
            ? (sender == TlsSideV1::Connector
                   ? kConnectorBind[value.record_index]
                   : kListenerBind[value.record_index])
            : (sender_is_initiator
                   ? kInitiatorPrebind[value.record_index]
                   : kResponderPrebind[value.record_index]);
        const std::uint32_t size = be32(value.bytes.data());
        if (size != expected.size)
            return fail(QuicGateResult::ProtocolViolation);
        const std::size_t total = static_cast<std::size_t>(size) + 4;
        if (value.bytes.size() < total)
            return QuicGateResult::NeedMore;
        if (expected.first_byte != 0 && value.bytes[4] != expected.first_byte)
            return fail(QuicGateResult::ProtocolViolation);
        value.bytes.erase(value.bytes.begin(),
                          value.bytes.begin() + static_cast<std::ptrdiff_t>(total));
        ++value.record_index;
        accepted = true;
    }
    return accepted ? QuicGateResult::Accepted : QuicGateResult::NeedMore;
}

QuicGateResult QuicStreamGate::data(std::uint64_t stream_id, TlsSideV1 sender,
                                    const std::uint8_t* bytes, std::size_t size)
{
    if (!valid_side(sender) || (bytes == nullptr && size != 0))
        return fail(QuicGateResult::InvalidArgument);
    if (phase_ == Phase::Failed || phase_ == Phase::Bound)
        return QuicGateResult::Closed;
    if ((phase_ != Phase::Prebind && phase_ != Phase::Bind) ||
        stream_id != stream_id_ || direction(sender).fin)
        return fail(QuicGateResult::ProtocolViolation);
    auto& target = direction(sender);
    if (target.bytes.size() + size > 2048)
        return fail(QuicGateResult::Backpressure);
    try
    {
        if (size != 0)
            target.bytes.insert(target.bytes.end(), bytes, bytes + size);
    }
    catch (...)
    {
        return fail(QuicGateResult::Backpressure);
    }

    if (!preamble_complete_)
    {
        if (sender != TlsSideV1::Connector)
            return fail(QuicGateResult::ProtocolViolation);
        if (target.bytes.size() < 8)
            return QuicGateResult::NeedMore;
        const std::array<std::uint8_t, 8> expected = phase_ == Phase::Prebind
            ? std::array<std::uint8_t, 8>{{'F','N','R','1',0,1,0,2}}
            : std::array<std::uint8_t, 8>{{'F','N','B','1',0,1,0,1}};
        if (!std::equal(expected.begin(), expected.end(), target.bytes.begin()))
            return fail(QuicGateResult::ProtocolViolation);
        target.bytes.erase(target.bytes.begin(), target.bytes.begin() + 8);
        preamble_complete_ = true;
    }
    return parse(target, sender);
}

QuicGateResult QuicStreamGate::finish(std::uint64_t stream_id,
                                      TlsSideV1 sender) noexcept
{
    if (!valid_side(sender))
        return fail(QuicGateResult::InvalidArgument);
    if (phase_ == Phase::Failed || phase_ == Phase::Bound)
        return QuicGateResult::Closed;
    if ((phase_ != Phase::Prebind && phase_ != Phase::Bind) ||
        stream_id != stream_id_ || !preamble_complete_)
        return fail(QuicGateResult::ProtocolViolation);
    auto& target = direction(sender);
    if (target.fin || !target.bytes.empty() ||
        target.record_index != expected_count(sender))
        return fail(QuicGateResult::ProtocolViolation);
    if (phase_ == Phase::Bind && sender == TlsSideV1::Listener && !connector_.fin)
        return fail(QuicGateResult::ProtocolViolation);
    target.fin = true;
    if (!connector_.fin || !listener_.fin)
        return QuicGateResult::Accepted;

    prior_stream_id_ = stream_id_;
    stream_id_ = 0;
    if (phase_ == Phase::Prebind)
    {
        phase_ = Phase::ExpectBind;
        return QuicGateResult::PrebindComplete;
    }
    phase_ = Phase::Bound;
    return QuicGateResult::ChannelBound;
}

bool QuicStreamGate::normal_app_stream_allowed() const noexcept
{
    return phase_ == Phase::Bound;
}

bool QuicStreamGate::failed() const noexcept
{
    return phase_ == Phase::Failed;
}

} // namespace flynes::session::wire
