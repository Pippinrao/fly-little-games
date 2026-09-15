#ifndef FLYNES_SESSION_LINK_ACTIVATION_HPP
#define FLYNES_SESSION_LINK_ACTIVATION_HPP

#include "initial_plan_lock.hpp"
#include "wire/prebind_record_queue.hpp"
#include "wire/quic_contract.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace flynes::session {

enum class LinkActivationResult : std::int32_t
{
    Accepted = 1,
    Connected = 2,
    Released = 3,
    Closed = -1,
    InvalidArgument = -2,
    AuthFailed = -3,
    ProtocolViolation = -4,
    Backpressure = -5
};

struct LinkActivationStartV1
{
    VerifiedPairEvidence pair{};
    std::array<std::uint8_t, 16> session_id{};
    std::array<std::uint8_t, 32> reconnect_transcript_hash{};
    PairRole quic_listener_role{};
    std::array<std::uint8_t, 32> listener_der_spki_hash{};
};

struct LinkChannelBindV1
{
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    std::array<std::uint8_t, 16> session_id{};
    std::array<std::uint8_t, 32> reconnect_transcript_hash{};
    std::array<std::uint8_t, 16> channel_id{};
};

// The only gate that turns authenticated pairing and transport facts into an
// active game-less LINK. It deliberately has no ROM/game input: game setup is
// a later scope. Any failed proof closes the gate and destroys pre-bind bytes.
class AuthenticatedLinkGate
{
public:
    AuthenticatedLinkGate(std::size_t maximum_prebind_bytes,
                          std::size_t maximum_prebind_records) noexcept;

    LinkActivationResult begin(const LinkActivationStartV1& start) noexcept;
    LinkActivationResult enqueue_prebind(std::uint64_t stream_id,
                                         std::uint64_t stream_sequence,
                                         wire::OwnedAppFrame frame);
    LinkActivationResult accept_transport(
        const wire::QuicHandshakeFactsV2& facts,
        const std::array<std::uint8_t, 32>& exporter) noexcept;
    LinkActivationResult accept_channel_bind(
        const LinkChannelBindV1& bind) noexcept;
    LinkActivationResult accept_link_ready(PairRole sender) noexcept;
    LinkActivationResult release_prebind(
        std::vector<wire::QueuedAppRecord>* output);

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 16>& channel_id() const noexcept
    {
        return channel_id_;
    }

private:
    enum class Phase { Idle, PairAccepted, TransportAuthenticated, Bound, Connected, Failed };
    LinkActivationResult fail(LinkActivationResult result) noexcept;
    static LinkActivationResult map_queue(wire::PreBindQueueResult result) noexcept;

    wire::PreBindRecordQueue prebind_;
    Phase phase_ = Phase::Idle;
    LinkActivationStartV1 start_{};
    std::array<std::uint8_t, 32> exporter_{};
    std::array<std::uint8_t, 16> channel_id_{};
    bool initiator_ready_ = false;
    bool responder_ready_ = false;
};

} // namespace flynes::session

#endif
