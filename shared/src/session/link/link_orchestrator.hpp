#ifndef FLYNES_SESSION_LINK_LINK_ORCHESTRATOR_HPP
#define FLYNES_SESSION_LINK_LINK_ORCHESTRATOR_HPP

#include "../initial_plan_lock.hpp"
#include "../link_activation.hpp"

#include <cstdint>
#include <deque>
#include <optional>

namespace flynes::session {

enum class LinkOrchestratorState : std::uint8_t
{
    Idle,
    PlanNegotiating,
    BearerReady,
    QuicStarting,
    Binding,
    ConnectedLobby,
    Cancelled,
    Failed
};

enum class LinkOrchestratorResult : std::int32_t
{
    Accepted = 1,
    Connected = 2,
    Stale = -1,
    InvalidState = -2,
    AuthFailed = -3,
    ProtocolViolation = -4
};

struct QuicStartEffect final
{
    std::uint64_t id = 0;
    std::uint64_t generation = 0;
    fly_session_resource_handle_v2 bearer = 0;
    LinkActivationStartV1 activation{};
};

// Coordinates the game-less LINK path. Protocol validation remains in
// InitialPlanLock and AuthenticatedLinkGate; this layer owns side-effect fencing
// and provider resource cleanup across cancellation and stale completion races.
class LinkOrchestrator final
{
public:
    LinkOrchestrator() noexcept;

    LinkOrchestratorResult begin(const VerifiedPairEvidence& pair) noexcept;
    LinkOrchestratorResult accept_plan(const VerifiedPlanEvidence& evidence) noexcept;
    LinkOrchestratorResult accept_ack(const VerifiedPlanEvidence& evidence) noexcept;
    LinkOrchestratorResult accept_final(const VerifiedPlanEvidence& evidence) noexcept;
    LinkOrchestratorResult accept_credentials(
        const VerifiedCredentialEvidence& evidence) noexcept;
    std::optional<InitialPlanCommand> poll_plan_effect() const noexcept;
    LinkOrchestratorResult complete_plan_effect(
        std::uint64_t id, std::uint64_t generation,
        fly_session_result_v2 result,
        fly_session_resource_handle_v2 resource = 0) noexcept;

    LinkOrchestratorResult request_quic(
        const LinkActivationStartV1& activation) noexcept;
    std::optional<QuicStartEffect> poll_quic_effect() const noexcept;
    LinkOrchestratorResult complete_quic(
        std::uint64_t id, std::uint64_t generation,
        fly_session_result_v2 result,
        fly_session_resource_handle_v2 connection,
        const wire::QuicHandshakeFactsV2& facts,
        const PlanHash& exporter) noexcept;
    LinkOrchestratorResult accept_channel_bind(
        const LinkChannelBindV1& bind) noexcept;
    LinkOrchestratorResult accept_link_ready(PairRole sender) noexcept;

    LinkOrchestratorResult cancel(std::uint64_t generation) noexcept;
    fly_session_resource_handle_v2 take_cleanup_resource() noexcept;

    [[nodiscard]] LinkOrchestratorState state() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

private:
    LinkOrchestratorResult fail(LinkOrchestratorResult result) noexcept;
    void own_for_cleanup(fly_session_resource_handle_v2 resource) noexcept;

    InitialPlanLock plan_{};
    AuthenticatedLinkGate gate_;
    LinkOrchestratorState state_ = LinkOrchestratorState::Idle;
    std::uint64_t generation_ = 0;
    std::uint64_t next_effect_id_ = 1;
    fly_session_resource_handle_v2 bearer_ = 0;
    fly_session_resource_handle_v2 connection_ = 0;
    std::optional<QuicStartEffect> quic_effect_{};
    std::deque<fly_session_resource_handle_v2> cleanup_{};
};

} // namespace flynes::session

#endif
