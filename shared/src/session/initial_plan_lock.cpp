#include "initial_plan_lock.hpp"
#include "wire/sha256.hpp"

#include <algorithm>
#include <limits>

namespace flynes::session {
namespace {
bool nonzero(const PlanHash& hash)
{
    return std::any_of(hash.begin(), hash.end(), [](std::uint8_t b) { return b != 0; });
}
bool valid_role(PairRole role)
{
    return role == PairRole::Initiator || role == PairRole::Responder;
}
PairRole other(PairRole role)
{
    return role == PairRole::Initiator ? PairRole::Responder : PairRole::Initiator;
}
PlanHash selected_hash(const wire::BearerPlanBytes& plan)
{
    constexpr char domain[] = "flynes-selected-bearer-plan-v1";
    constexpr std::size_t domain_size = sizeof(domain) - 1;
    std::array<std::uint8_t, domain_size + 4 + 48> preimage{};
    std::copy(domain, domain + domain_size, preimage.begin());
    preimage[domain_size + 3] = 48;
    std::copy(plan.begin(), plan.end(), preimage.begin() + domain_size + 4);
    return wire::sha256(preimage.data(), preimage.size());
}
} // namespace

bool InitialPlanLock::reject() noexcept
{
    invalidate();
    return false;
}

void InitialPlanLock::invalidate() noexcept
{
    failed_ = true;
    pending_.reset();
    phase_ = Phase::Done;
}

bool InitialPlanLock::begin(const VerifiedPairEvidence& evidence)
{
    if (failed_ || phase_ != Phase::Idle || !valid_role(evidence.local_role) ||
        evidence.generation == 0 || !nonzero(evidence.transcript) ||
        !nonzero(evidence.initiator_reveal) || !nonzero(evidence.responder_reveal)) return reject();
    const auto selection = wire::select_pair_plan(
        evidence.initiator_summary.data(), evidence.initiator_summary.size(),
        evidence.responder_summary.data(), evidence.responder_summary.size(), selected_);
    if (selection != wire::PairSelectionStatus::Ok) {
        not_supported_ = selection == wire::PairSelectionStatus::NoCommonPlan;
        return reject();
    }
    pair_ = evidence;
    phase_ = Phase::Plan;
    return true;
}

bool InitialPlanLock::valid_binding(const VerifiedPlanEvidence& evidence, PairRole sender) const
{
    return !failed_ && evidence.generation == pair_.generation && evidence.generation != 0 &&
        evidence.sender == sender && evidence.receiver == other(sender) &&
        evidence.transcript == pair_.transcript &&
        evidence.initiator_reveal == pair_.initiator_reveal &&
        evidence.responder_reveal == pair_.responder_reveal &&
        evidence.selected_plan == selected_ &&
        evidence.selected_plan_hash == selected_hash(selected_) &&
        nonzero(evidence.selected_plan_hash) && nonzero(evidence.plan_logical_hash);
}

bool InitialPlanLock::issue(InitialPlanCommandKind kind, bool may_prompt)
{
    if (failed_ || pending_ || next_id_ == std::numeric_limits<std::uint64_t>::max()) return reject();
    pending_ = InitialPlanCommand{next_id_++, pair_.generation, kind, evidence_, credential_hash_, may_prompt};
    phase_ = Phase::Pending;
    return true;
}

bool InitialPlanLock::accept_plan(const VerifiedPlanEvidence& evidence)
{
    if (phase_ != Phase::Plan || !valid_binding(evidence, PairRole::Initiator) ||
        nonzero(evidence.ack_logical_hash) || nonzero(evidence.final_logical_hash)) return reject();
    evidence_ = evidence;
    return issue(pair_.local_role == PairRole::Initiator ? InitialPlanCommandKind::PersistPlan :
                 InitialPlanCommandKind::PersistPlanAndLock);
}

bool InitialPlanLock::accept_ack(const VerifiedPlanEvidence& evidence)
{
    if (phase_ != Phase::Ack || !valid_binding(evidence, PairRole::Responder) ||
        evidence.plan_logical_hash != evidence_.plan_logical_hash ||
        !nonzero(evidence.ack_logical_hash) || nonzero(evidence.final_logical_hash)) return reject();
    evidence_ = evidence;
    return issue(pair_.local_role == PairRole::Initiator ? InitialPlanCommandKind::PersistAckAndLock :
                 InitialPlanCommandKind::PersistAck);
}

bool InitialPlanLock::accept_final(const VerifiedPlanEvidence& evidence)
{
    if (phase_ != Phase::Final || !valid_binding(evidence, PairRole::Initiator) ||
        evidence.plan_logical_hash != evidence_.plan_logical_hash ||
        evidence.ack_logical_hash != evidence_.ack_logical_hash ||
        !nonzero(evidence.ack_logical_hash) || !nonzero(evidence.final_logical_hash)) return reject();
    evidence_ = evidence;
    return issue(InitialPlanCommandKind::PersistFinal);
}

bool InitialPlanLock::accept_credentials(const VerifiedCredentialEvidence& evidence)
{
    const auto creator = static_cast<PairRole>(selected_[1]);
    if (phase_ != Phase::Credentials || !mutually_locked_ ||
        !valid_binding(evidence.binding, creator) ||
        evidence.binding.plan_logical_hash != evidence_.plan_logical_hash ||
        evidence.binding.ack_logical_hash != evidence_.ack_logical_hash ||
        evidence.binding.final_logical_hash != evidence_.final_logical_hash ||
        !nonzero(evidence.credential_logical_hash)) return reject();
    credential_hash_ = evidence.credential_logical_hash;
    evidence_ = evidence.binding;
    return issue(InitialPlanCommandKind::PersistCredentials);
}

std::optional<InitialPlanCommand> InitialPlanLock::poll() const
{
    return pending_;
}

bool InitialPlanLock::issue_bearer()
{
    const bool designated = selected_[5] == static_cast<std::uint8_t>(pair_.local_role);
    if (!mutually_locked_ || (designated && !prompt_consumed_)) return reject();
    const bool creator = selected_[1] == static_cast<std::uint8_t>(pair_.local_role);
    if (!creator && !nonzero(credential_hash_)) return reject();
    return issue(creator ? InitialPlanCommandKind::CreateBearer : InitialPlanCommandKind::JoinBearer,
                 designated);
}

bool InitialPlanLock::authorize_bearer()
{
    if (!mutually_locked_) return reject();
    if (selected_[5] == static_cast<std::uint8_t>(pair_.local_role)) {
        if (prompt_consumed_) return reject();
        return issue(InitialPlanCommandKind::PersistPromptConsumed);
    }
    return issue_bearer();
}

bool InitialPlanLock::complete(std::uint64_t id, std::uint64_t generation, bool success)
{
    if (failed_ || !pending_ || pending_->id != id || pending_->generation != generation ||
        pair_.generation != generation || !success) return reject();
    const auto kind = pending_->kind;
    pending_.reset();
    switch (kind) {
    case InitialPlanCommandKind::PersistPlan:
        return issue(InitialPlanCommandKind::SendPlan);
    case InitialPlanCommandKind::PersistPlanAndLock:
        locked_ = true;
        phase_ = Phase::Ack;
        return true;
    case InitialPlanCommandKind::SendPlan:
        phase_ = Phase::Ack;
        return true;
    case InitialPlanCommandKind::PersistAck:
        return issue(InitialPlanCommandKind::SendAck);
    case InitialPlanCommandKind::PersistAckAndLock:
        locked_ = true;
        phase_ = Phase::Final;
        return true;
    case InitialPlanCommandKind::SendAck:
        phase_ = Phase::Final;
        return true;
    case InitialPlanCommandKind::PersistFinal:
        return issue(pair_.local_role == PairRole::Initiator ? InitialPlanCommandKind::SendFinal :
                     InitialPlanCommandKind::PersistMutualLock);
    case InitialPlanCommandKind::SendFinal:
        return issue(InitialPlanCommandKind::PersistMutualLock);
    case InitialPlanCommandKind::PersistMutualLock:
        mutually_locked_ = true;
        if (selected_[1] == static_cast<std::uint8_t>(pair_.local_role)) return authorize_bearer();
        phase_ = Phase::Credentials;
        return true;
    case InitialPlanCommandKind::PersistCredentials:
        if (selected_[1] == static_cast<std::uint8_t>(pair_.local_role))
            return issue(InitialPlanCommandKind::PublishCredentials);
        return authorize_bearer();
    case InitialPlanCommandKind::PersistPromptConsumed:
        prompt_consumed_ = true;
        return issue_bearer();
    case InitialPlanCommandKind::CreateBearer:
        phase_ = Phase::Credentials;
        return true;
    case InitialPlanCommandKind::PublishCredentials:
    case InitialPlanCommandKind::JoinBearer:
        phase_ = Phase::Done;
        return true;
    }
    return reject();
}
} // namespace flynes::session
