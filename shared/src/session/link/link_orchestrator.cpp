#include "link_orchestrator.hpp"

namespace flynes::session {

LinkOrchestrator::LinkOrchestrator() noexcept : gate_(64 * 1024, 64) {}

void LinkOrchestrator::own_for_cleanup(
    fly_session_resource_handle_v2 resource) noexcept
{
    if (resource != 0)
        cleanup_.push_back(resource);
}

LinkOrchestratorResult LinkOrchestrator::fail(
    LinkOrchestratorResult result) noexcept
{
    plan_.invalidate();
    quic_effect_.reset();
    own_for_cleanup(connection_);
    own_for_cleanup(bearer_);
    connection_ = 0;
    bearer_ = 0;
    state_ = LinkOrchestratorState::Failed;
    return result;
}

LinkOrchestratorResult LinkOrchestrator::begin(
    const VerifiedPairEvidence& pair) noexcept
{
    if (state_ != LinkOrchestratorState::Idle)
        return fail(LinkOrchestratorResult::ProtocolViolation);
    if (!pair.authenticated() || !plan_.begin(pair))
        return fail(LinkOrchestratorResult::AuthFailed);
    generation_ = pair.generation;
    state_ = LinkOrchestratorState::PlanNegotiating;
    return LinkOrchestratorResult::Accepted;
}

LinkOrchestratorResult LinkOrchestrator::accept_plan(
    const VerifiedPlanEvidence& evidence) noexcept
{
    return state_ == LinkOrchestratorState::PlanNegotiating &&
                   plan_.accept_plan(evidence)
               ? LinkOrchestratorResult::Accepted
               : fail(LinkOrchestratorResult::ProtocolViolation);
}

LinkOrchestratorResult LinkOrchestrator::accept_ack(
    const VerifiedPlanEvidence& evidence) noexcept
{
    return state_ == LinkOrchestratorState::PlanNegotiating &&
                   plan_.accept_ack(evidence)
               ? LinkOrchestratorResult::Accepted
               : fail(LinkOrchestratorResult::ProtocolViolation);
}

LinkOrchestratorResult LinkOrchestrator::accept_final(
    const VerifiedPlanEvidence& evidence) noexcept
{
    return state_ == LinkOrchestratorState::PlanNegotiating &&
                   plan_.accept_final(evidence)
               ? LinkOrchestratorResult::Accepted
               : fail(LinkOrchestratorResult::ProtocolViolation);
}

LinkOrchestratorResult LinkOrchestrator::accept_credentials(
    const VerifiedCredentialEvidence& evidence) noexcept
{
    return state_ == LinkOrchestratorState::PlanNegotiating &&
                   plan_.accept_credentials(evidence)
               ? LinkOrchestratorResult::Accepted
               : fail(LinkOrchestratorResult::ProtocolViolation);
}

std::optional<InitialPlanCommand> LinkOrchestrator::poll_plan_effect() const noexcept
{
    return state_ == LinkOrchestratorState::PlanNegotiating
               ? plan_.poll() : std::nullopt;
}

LinkOrchestratorResult LinkOrchestrator::complete_plan_effect(
    std::uint64_t id, std::uint64_t generation, fly_session_result_v2 result,
    fly_session_resource_handle_v2 resource) noexcept
{
    const auto pending = plan_.poll();
    if (state_ != LinkOrchestratorState::PlanNegotiating || !pending ||
        generation != generation_ || id != pending->id ||
        generation != pending->generation)
    {
        own_for_cleanup(resource);
        return LinkOrchestratorResult::Stale;
    }
    const bool bearer_effect =
        pending->kind == InitialPlanCommandKind::CreateBearer ||
        pending->kind == InitialPlanCommandKind::JoinBearer;
    if ((bearer_effect && resource == 0) || (!bearer_effect && resource != 0) ||
        result != FLY_SESSION_V2_OK)
    {
        own_for_cleanup(resource);
        return fail(result == FLY_SESSION_V2_AUTH_FAILED
                        ? LinkOrchestratorResult::AuthFailed
                        : LinkOrchestratorResult::ProtocolViolation);
    }
    const auto kind = pending->kind;
    if (!plan_.complete(id, generation, true))
        return fail(LinkOrchestratorResult::ProtocolViolation);
    if (bearer_effect)
        bearer_ = resource;
    if (kind == InitialPlanCommandKind::JoinBearer ||
        kind == InitialPlanCommandKind::PublishCredentials)
        state_ = LinkOrchestratorState::BearerReady;
    return LinkOrchestratorResult::Accepted;
}

LinkOrchestratorResult LinkOrchestrator::request_quic(
    const LinkActivationStartV1& activation) noexcept
{
    if (state_ != LinkOrchestratorState::BearerReady || bearer_ == 0 ||
        activation.pair.generation != generation_)
        return LinkOrchestratorResult::InvalidState;
    if (gate_.begin(activation) != LinkActivationResult::Accepted)
        return fail(LinkOrchestratorResult::AuthFailed);
    quic_effect_ = QuicStartEffect{
        next_effect_id_++, generation_, bearer_, activation};
    state_ = LinkOrchestratorState::QuicStarting;
    return LinkOrchestratorResult::Accepted;
}

std::optional<QuicStartEffect> LinkOrchestrator::poll_quic_effect() const noexcept
{
    return quic_effect_;
}

LinkOrchestratorResult LinkOrchestrator::complete_quic(
    std::uint64_t id, std::uint64_t generation, fly_session_result_v2 result,
    fly_session_resource_handle_v2 connection,
    const wire::QuicHandshakeFactsV2& facts, const PlanHash& exporter) noexcept
{
    if (state_ != LinkOrchestratorState::QuicStarting || !quic_effect_ ||
        quic_effect_->id != id || quic_effect_->generation != generation ||
        generation != generation_)
    {
        own_for_cleanup(connection);
        return LinkOrchestratorResult::Stale;
    }
    quic_effect_.reset();
    if (result != FLY_SESSION_V2_OK || connection == 0 ||
        gate_.accept_transport(facts, exporter) != LinkActivationResult::Accepted)
    {
        own_for_cleanup(connection);
        return fail(result == FLY_SESSION_V2_AUTH_FAILED
                        ? LinkOrchestratorResult::AuthFailed
                        : LinkOrchestratorResult::ProtocolViolation);
    }
    connection_ = connection;
    state_ = LinkOrchestratorState::Binding;
    return LinkOrchestratorResult::Accepted;
}

LinkOrchestratorResult LinkOrchestrator::accept_channel_bind(
    const LinkChannelBindV1& bind) noexcept
{
    return state_ == LinkOrchestratorState::Binding &&
                   gate_.accept_channel_bind(bind) == LinkActivationResult::Accepted
               ? LinkOrchestratorResult::Accepted
               : fail(LinkOrchestratorResult::ProtocolViolation);
}

LinkOrchestratorResult LinkOrchestrator::accept_link_ready(PairRole sender) noexcept
{
    if (state_ != LinkOrchestratorState::Binding)
        return fail(LinkOrchestratorResult::ProtocolViolation);
    const auto result = gate_.accept_link_ready(sender);
    if (result == LinkActivationResult::Connected)
    {
        state_ = LinkOrchestratorState::ConnectedLobby;
        return LinkOrchestratorResult::Connected;
    }
    return result == LinkActivationResult::Accepted
               ? LinkOrchestratorResult::Accepted
               : fail(LinkOrchestratorResult::ProtocolViolation);
}

LinkOrchestratorResult LinkOrchestrator::cancel(std::uint64_t generation) noexcept
{
    if (generation != generation_ || state_ == LinkOrchestratorState::Idle ||
        state_ == LinkOrchestratorState::Cancelled)
        return LinkOrchestratorResult::Stale;
    plan_.invalidate();
    quic_effect_.reset();
    own_for_cleanup(connection_);
    own_for_cleanup(bearer_);
    connection_ = 0;
    bearer_ = 0;
    state_ = LinkOrchestratorState::Cancelled;
    return LinkOrchestratorResult::Accepted;
}

fly_session_resource_handle_v2 LinkOrchestrator::take_cleanup_resource() noexcept
{
    if (cleanup_.empty())
        return 0;
    const auto value = cleanup_.front();
    cleanup_.pop_front();
    return value;
}

} // namespace flynes::session
