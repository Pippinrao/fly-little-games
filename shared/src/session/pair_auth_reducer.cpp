#include "pair_auth_reducer.hpp"
#include "wire/sha256.hpp"

#include <algorithm>
#include <iterator>

namespace flynes::session {
namespace {

bool nonzero(const PlanHash& hash) noexcept
{
    return std::any_of(hash.begin(), hash.end(), [](std::uint8_t value) {
        return value != 0;
    });
}

std::uint8_t role_bit(PairRole role) noexcept
{
    return role == PairRole::Initiator ? 1u : 2u;
}

bool valid_role(PairRole role) noexcept
{
    return role == PairRole::Initiator || role == PairRole::Responder;
}

template <typename Container>
bool all_zero(const Container& value) noexcept
{
    return std::all_of(value.begin(), value.end(), [](auto element) {
        return element == 0;
    });
}

bool distinct_nonzero_operations(const PairAuthStartV1& start) noexcept
{
    const std::array<std::uint64_t, 4> operations{{
        start.signature_operation_ids[0], start.signature_operation_ids[1],
        start.key_confirm_operation_ids[0], start.key_confirm_operation_ids[1]}};
    for (std::size_t index = 0; index < operations.size(); ++index)
    {
        if (operations[index] == 0)
            return false;
        for (std::size_t prior = 0; prior < index; ++prior)
        {
            if (operations[index] == operations[prior])
                return false;
        }
    }
    return true;
}

} // namespace

bool PairAuthenticationReducer::reject() noexcept
{
    failed_ = true;
    return false;
}

bool PairAuthenticationReducer::begin(const PairAuthStartV1& start)
{
    if (begun_ || failed_ || start.generation == 0 || !valid_role(start.local_role) ||
        all_zero(start.engine_instance_id) || all_zero(start.link_id) ||
        !distinct_nonzero_operations(start) ||
        (start.entry_mode != 1 && start.entry_mode != 2) ||
        !nonzero(start.pair_context_hash) ||
        start.initiator_commit.sender != wire::PairRoleV1::Initiator ||
        start.initiator_commit.receiver != wire::PairRoleV1::Responder ||
        start.responder_commit.sender != wire::PairRoleV1::Responder ||
        start.responder_commit.receiver != wire::PairRoleV1::Initiator ||
        start.initiator_contribution.role != wire::PairRoleV1::Initiator ||
        start.responder_contribution.role != wire::PairRoleV1::Responder ||
        start.initiator_commit.pair_context_hash != start.pair_context_hash ||
        start.responder_commit.pair_context_hash != start.pair_context_hash ||
        wire::bind_reveal_to_commit_v1(start.initiator_commit,
                                       start.initiator_contribution) != wire::Status::Ok ||
        wire::bind_reveal_to_commit_v1(start.responder_commit,
                                       start.responder_contribution) != wire::Status::Ok ||
        !nonzero(start.initiator_reveal_logical_hash) ||
        !nonzero(start.responder_reveal_logical_hash) ||
        start.initiator_reveal_logical_hash == start.responder_reveal_logical_hash)
        return reject();

    std::array<std::uint8_t, 752> preimage{};
    PlanHash transcript{};
    if (wire::build_pair_transcript_preimage_v1(
            start.entry_mode, start.entry_context_hash,
            start.initiator_commit.commitment, start.responder_commit.commitment,
            start.initiator_contribution, start.responder_contribution,
            &preimage, &transcript) != wire::Status::Ok)
        return reject();

    begun_ = true;
    generation_ = start.generation;
    engine_instance_id_ = start.engine_instance_id;
    link_id_ = start.link_id;
    signature_operation_ids_ = start.signature_operation_ids;
    key_confirm_operation_ids_ = start.key_confirm_operation_ids;
    local_role_ = start.local_role;
    entry_mode_ = start.entry_mode;
    known_path_ = start.known_path;
    transcript_hash_ = transcript;
    initiator_reveal_ = start.initiator_reveal_logical_hash;
    responder_reveal_ = start.responder_reveal_logical_hash;
    expected_initiator_summary_hash_ =
        start.initiator_contribution.capability_summary_hash;
    expected_responder_summary_hash_ =
        start.responder_contribution.capability_summary_hash;
    return true;
}

bool PairAuthenticationReducer::seen_operation(std::uint64_t operation_id) const noexcept
{
    return std::find(operation_ids_.begin(),
                     operation_ids_.begin() + static_cast<std::ptrdiff_t>(operation_count_),
                     operation_id) !=
           operation_ids_.begin() + static_cast<std::ptrdiff_t>(operation_count_);
}

bool PairAuthenticationReducer::accept_provider_verification(
    std::uint64_t operation_id, std::uint32_t payload_kind, PairRole role)
{
    if (!begun_ || failed_ || consumed_ || operation_count_ >= operation_ids_.size() ||
        seen_operation(operation_id) || !valid_role(role))
        return reject();
    const auto role_index = role == PairRole::Initiator ? 0u : 1u;

    if (payload_kind == FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2)
    {
        const PairRole expected = signature_mask_ == 0 ? PairRole::Initiator
                                                      : PairRole::Responder;
        if (signature_mask_ == 3 || role != expected ||
            operation_id != signature_operation_ids_[role_index])
            return reject();
        signature_mask_ = static_cast<std::uint8_t>(signature_mask_ | role_bit(role));
    }
    else if (payload_kind == FLY_SESSION_PAIR_KEY_CONFIRM_VERIFIED_V2)
    {
        if (signature_mask_ != 3 || approval_kind_ == 0 || key_confirm_mask_ == 3 ||
            (key_confirm_mask_ & role_bit(role)) != 0 ||
            operation_id != key_confirm_operation_ids_[role_index])
            return reject();
        key_confirm_mask_ = static_cast<std::uint8_t>(key_confirm_mask_ | role_bit(role));
    }
    else
    {
        return reject();
    }
    operation_ids_[operation_count_++] = operation_id;
    return true;
}

bool PairAuthenticationReducer::approve_local(std::uint32_t approval_kind)
{
    if (!begun_ || failed_ || consumed_ || signature_mask_ != 3 ||
        approval_kind_ != 0)
        return reject();
    std::uint32_t expected = 0;
    if (entry_mode_ == 2)
        expected = local_role_ == PairRole::Initiator
            ? FLY_SESSION_APPROVAL_QR_INVITER_ACCEPT_V2
            : FLY_SESSION_APPROVAL_QR_SCANNER_JOIN_V2;
    else if (known_path_)
        expected = local_role_ == PairRole::Initiator
            ? FLY_SESSION_APPROVAL_KNOWN_FRIEND_INVITE_V2
            : FLY_SESSION_APPROVAL_KNOWN_FRIEND_ACCEPT_V2;
    else
        expected = FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2;
    if (approval_kind != expected)
        return reject();
    approval_kind_ = approval_kind;
    return true;
}

bool PairAuthenticationReducer::accept_capability_reveal(
    PairRole sender, const CapabilitySummary& summary, const PlanHash& logical_hash)
{
    if (!begun_ || failed_ || consumed_ || key_confirm_mask_ != 3 ||
        !valid_role(sender) || !nonzero(logical_hash) ||
        (capability_mask_ & role_bit(sender)) != 0 ||
        (capability_mask_ == 0 && sender != PairRole::Initiator) ||
        wire::validate_pair_capability(summary.data(), summary.size()) != wire::Status::Ok)
        return reject();
    const auto summary_hash = wire::domain_hash(
        "flynes-pair-capability-summary-v1", summary.data(), summary.size());
    const auto& expected_hash = sender == PairRole::Initiator
        ? expected_initiator_summary_hash_ : expected_responder_summary_hash_;
    if (summary_hash != expected_hash)
        return reject();
    if (sender == PairRole::Initiator)
    {
        initiator_summary_ = summary;
        initiator_capability_ = logical_hash;
    }
    else
    {
        responder_summary_ = summary;
        responder_capability_ = logical_hash;
    }
    capability_mask_ = static_cast<std::uint8_t>(capability_mask_ | role_bit(sender));
    return true;
}

std::optional<VerifiedPairEvidence>
PairAuthenticationReducer::take_verified_evidence()
{
    if (!begun_ || failed_ || consumed_ || signature_mask_ != 3 ||
        key_confirm_mask_ != 3 || capability_mask_ != 3 || approval_kind_ == 0)
        return std::nullopt;
    VerifiedPairEvidence evidence{};
    evidence.local_role = local_role_;
    evidence.generation = generation_;
    evidence.transcript = transcript_hash_;
    evidence.initiator_reveal = initiator_reveal_;
    evidence.responder_reveal = responder_reveal_;
    evidence.initiator_capability = initiator_capability_;
    evidence.responder_capability = responder_capability_;
    evidence.initiator_summary = initiator_summary_;
    evidence.responder_summary = responder_summary_;
    evidence.seal_for_authenticated_pipeline();
    consumed_ = true;
    return evidence;
}

} // namespace flynes::session
