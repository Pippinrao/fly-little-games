#include "link_activation.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace flynes::session {
namespace {

template <std::size_t N>
bool any_nonzero(const std::array<std::uint8_t, N>& value) noexcept
{
    return std::any_of(value.begin(), value.end(), [](std::uint8_t byte) {
        return byte != 0;
    });
}

bool valid_role(PairRole role) noexcept
{
    return role == PairRole::Initiator || role == PairRole::Responder;
}

} // namespace

AuthenticatedLinkGate::AuthenticatedLinkGate(
    std::size_t maximum_prebind_bytes,
    std::size_t maximum_prebind_records) noexcept
    : prebind_(maximum_prebind_bytes, maximum_prebind_records)
{
}

LinkActivationResult AuthenticatedLinkGate::fail(
    LinkActivationResult result) noexcept
{
    prebind_.discard();
    phase_ = Phase::Failed;
    return result;
}

LinkActivationResult AuthenticatedLinkGate::begin(
    const LinkActivationStartV1& start) noexcept
{
    if (phase_ != Phase::Idle)
        return fail(LinkActivationResult::ProtocolViolation);
    if (!start.pair.authenticated() || !valid_role(start.pair.local_role) ||
        !valid_role(start.quic_listener_role) ||
        start.pair.generation == 0 || !any_nonzero(start.pair.transcript) ||
        !any_nonzero(start.session_id) ||
        !any_nonzero(start.listener_der_spki_hash))
        return fail(LinkActivationResult::AuthFailed);
    start_ = start;
    phase_ = Phase::PairAccepted;
    return LinkActivationResult::Accepted;
}

LinkActivationResult AuthenticatedLinkGate::map_queue(
    wire::PreBindQueueResult result) noexcept
{
    switch (result)
    {
    case wire::PreBindQueueResult::Queued:
    case wire::PreBindQueueResult::Blocked:
        return LinkActivationResult::Accepted;
    case wire::PreBindQueueResult::Released:
        return LinkActivationResult::Released;
    case wire::PreBindQueueResult::Backpressure:
        return LinkActivationResult::Backpressure;
    case wire::PreBindQueueResult::InvalidArgument:
        return LinkActivationResult::InvalidArgument;
    case wire::PreBindQueueResult::ProtocolViolation:
        return LinkActivationResult::ProtocolViolation;
    case wire::PreBindQueueResult::Closed:
        return LinkActivationResult::Closed;
    }
    return LinkActivationResult::ProtocolViolation;
}

LinkActivationResult AuthenticatedLinkGate::enqueue_prebind(
    std::uint64_t stream_id, std::uint64_t stream_sequence,
    wire::OwnedAppFrame frame)
{
    if (phase_ == Phase::Idle)
        return LinkActivationResult::InvalidArgument;
    if (phase_ == Phase::Failed || phase_ == Phase::Connected)
        return LinkActivationResult::Closed;
    const auto queued = prebind_.enqueue(stream_id, stream_sequence,
                                         std::move(frame));
    const auto mapped = map_queue(queued);
    if (queued == wire::PreBindQueueResult::Backpressure ||
        queued == wire::PreBindQueueResult::ProtocolViolation)
        phase_ = Phase::Failed;
    return mapped;
}

LinkActivationResult AuthenticatedLinkGate::accept_transport(
    const wire::QuicHandshakeFactsV2& facts,
    const std::array<std::uint8_t, 32>& exporter) noexcept
{
    if (phase_ != Phase::PairAccepted)
        return fail(LinkActivationResult::ProtocolViolation);
    if (!any_nonzero(exporter))
        return fail(LinkActivationResult::AuthFailed);

    fly_session_quic_connect_policy_v2 policy{};
    policy.struct_size = FLY_SESSION_QUIC_CONNECT_POLICY_V2_SIZE;
    policy.abi_version = FLY_SESSION_ABI_VERSION_2;
    policy.require_full_tls13 = 1;
    policy.forbid_resumption = 1;
    policy.forbid_zero_rtt = 1;
    constexpr char alpn[] = "flynes-nearby/2";
    std::copy(alpn, alpn + sizeof(alpn), policy.alpn);
    std::copy(start_.listener_der_spki_hash.begin(),
              start_.listener_der_spki_hash.end(),
              policy.expected_der_spki_hash);
    const auto handshake_status =
        start_.pair.local_role == start_.quic_listener_role
            ? wire::verify_quic_listener_handshake_v2(facts)
            : wire::verify_quic_handshake_v2(policy, facts);
    if (handshake_status != wire::Status::Ok)
        return fail(LinkActivationResult::AuthFailed);

    exporter_ = exporter;
    const auto queue_result = prebind_.mark_transport_authenticated(true, true, true);
    if (queue_result == wire::PreBindQueueResult::ProtocolViolation)
        return fail(LinkActivationResult::ProtocolViolation);
    phase_ = Phase::TransportAuthenticated;
    return LinkActivationResult::Accepted;
}

LinkActivationResult AuthenticatedLinkGate::accept_channel_bind(
    const LinkChannelBindV1& bind) noexcept
{
    if (phase_ != Phase::TransportAuthenticated)
        return fail(LinkActivationResult::ProtocolViolation);
    const auto expected_channel_id = wire::derive_channel_id_v1(
        start_.pair.transcript, start_.session_id,
        start_.reconnect_transcript_hash, exporter_);
    if (bind.pair_transcript_hash != start_.pair.transcript ||
        bind.session_id != start_.session_id ||
        bind.reconnect_transcript_hash != start_.reconnect_transcript_hash ||
        bind.channel_id != expected_channel_id)
        return fail(LinkActivationResult::ProtocolViolation);

    channel_id_ = expected_channel_id;
    const auto queue_result = prebind_.mark_channel_bound();
    if (queue_result == wire::PreBindQueueResult::ProtocolViolation)
        return fail(LinkActivationResult::ProtocolViolation);
    phase_ = Phase::Bound;
    return LinkActivationResult::Accepted;
}

LinkActivationResult AuthenticatedLinkGate::accept_link_ready(
    PairRole sender) noexcept
{
    if (phase_ != Phase::Bound || !valid_role(sender))
        return fail(LinkActivationResult::ProtocolViolation);
    bool* ready = sender == PairRole::Initiator
        ? &initiator_ready_ : &responder_ready_;
    if (*ready)
        return fail(LinkActivationResult::ProtocolViolation);
    *ready = true;

    const auto queue_result = prebind_.mark_link_ready(
        start_.pair.local_role == PairRole::Initiator
            ? initiator_ready_ : responder_ready_,
        start_.pair.local_role == PairRole::Initiator
            ? responder_ready_ : initiator_ready_);
    if (queue_result == wire::PreBindQueueResult::ProtocolViolation)
        return fail(LinkActivationResult::ProtocolViolation);
    if (initiator_ready_ && responder_ready_)
    {
        phase_ = Phase::Connected;
        return LinkActivationResult::Connected;
    }
    return LinkActivationResult::Accepted;
}

LinkActivationResult AuthenticatedLinkGate::release_prebind(
    std::vector<wire::QueuedAppRecord>* output)
{
    if (phase_ == Phase::Failed)
        return LinkActivationResult::Closed;
    return map_queue(prebind_.release(output));
}

bool AuthenticatedLinkGate::connected() const noexcept
{
    return phase_ == Phase::Connected;
}

bool AuthenticatedLinkGate::failed() const noexcept
{
    return phase_ == Phase::Failed;
}

} // namespace flynes::session
