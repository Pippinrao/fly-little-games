#include "pair_pipeline.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <new>

namespace flynes::session {
namespace {

bool valid_role(PairRole role) noexcept
{
    return role == PairRole::Initiator || role == PairRole::Responder;
}

std::uint8_t role_bit(PairRole role) noexcept
{
    return role == PairRole::Initiator ? 1u : 2u;
}

} // namespace

PairPipeline::PairPipeline(PairPipelineCryptoProvider& provider) noexcept
    : provider_(provider)
{
}

bool PairPipeline::reject() noexcept
{
    failed_ = true;
    return false;
}

std::size_t PairPipeline::role_index(PairRole role) noexcept
{
    return role == PairRole::Initiator ? 0u : 1u;
}

fly_session_op_token_v2 PairPipeline::token(
    std::uint64_t operation_id) const noexcept
{
    fly_session_op_token_v2 value{};
    value.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    value.abi_version = FLY_SESSION_ABI_VERSION_2;
    std::copy(start_.engine_instance_id.begin(), start_.engine_instance_id.end(),
              value.engine_instance_id);
    value.scope.struct_size = FLY_SESSION_SCOPE_V2_SIZE;
    value.scope.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.scope.kind = FLY_SESSION_SCOPE_LINK_V2;
    std::copy(start_.link_id.begin(), start_.link_id.end(),
              value.scope.link_id);
    value.connection_generation = start_.generation;
    value.operation_id = operation_id;
    return value;
}

bool PairPipeline::record_provider_terminal(
    const fly_session_op_token_v2& operation_token,
    std::uint32_t payload_kind, fly_session_result_v2 result)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = operation_token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = result;
    event.payload_kind = payload_kind;
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));

    ProviderOperationCompletion completion{};
    return operations_.accept(event, completion) == FLY_SESSION_V2_OK &&
           completion.terminal && completion.payload_kind == payload_kind &&
           completion.result == FLY_SESSION_V2_OK;
}

bool PairPipeline::begin(const PairAuthStartV1& start)
{
    const auto& signatures = start.signature_operation_ids;
    const auto& confirmations = start.key_confirm_operation_ids;
    if (begun_ || failed_ || !(signatures[0] < signatures[1] &&
        signatures[1] < confirmations[0] && confirmations[0] < confirmations[1]) ||
        !reducer_.begin(start))
        return reject();
    start_ = start;
    begun_ = true;
    return true;
}

bool PairPipeline::canonical_low_s(
    const std::array<std::uint8_t, 64>& signature) noexcept
{
    static constexpr std::array<std::uint8_t, 32> half_order{{
        0x7f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00,
        0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xde, 0x73, 0x7d, 0x56, 0xd3, 0x8b, 0xcf, 0x42,
        0x79, 0xdc, 0xe5, 0x61, 0x7e, 0x31, 0x92, 0xa8}};
    const bool r_nonzero = std::any_of(signature.begin(), signature.begin() + 32,
                                       [](std::uint8_t value) { return value != 0; });
    const bool s_nonzero = std::any_of(signature.begin() + 32, signature.end(),
                                       [](std::uint8_t value) { return value != 0; });
    return r_nonzero && s_nonzero &&
           !std::lexicographical_compare(
               half_order.begin(), half_order.end(), signature.begin() + 32,
               signature.end());
}

bool PairPipeline::submit_signature(
    PairRole role, const std::array<std::uint8_t, 64>& signature)
{
    if (!begun_ || failed() || !valid_role(role) ||
        (signature_mask_ & role_bit(role)) != 0 || !canonical_low_s(signature))
        return reject();
    const auto index = role_index(role);
    const auto& public_key = role == PairRole::Initiator
        ? start_.initiator_contribution.identity_public_key
        : start_.responder_contribution.identity_public_key;
    const auto operation = start_.signature_operation_ids[index];
    const auto operation_token = token(operation);
    if (operations_.expect(operation_token,
                           FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2) !=
            FLY_SESSION_V2_OK)
        return reject();
    const bool verified = provider_.verify_signature(
        operation_token, role, public_key, reducer_.transcript_hash(), signature);
    if (!record_provider_terminal(
            operation_token, FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2,
            verified ? FLY_SESSION_V2_OK : FLY_SESSION_V2_AUTH_FAILED) ||
        !reducer_.accept_provider_verification(
            operation, FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2, role))
        return reject();
    signature_mask_ = static_cast<std::uint8_t>(signature_mask_ | role_bit(role));
    return true;
}

std::optional<std::array<std::uint8_t, 6>> PairPipeline::sas()
{
    if (!begun_ || failed() || signature_mask_ != 3 ||
        start_.entry_mode != 1 || start_.known_path)
        return std::nullopt;
    if (!sas_)
    {
        std::array<std::uint8_t, 6> value{};
        if (!provider_.derive_sas(reducer_.transcript_hash(), value))
        {
            reject();
            return std::nullopt;
        }
        sas_ = value;
    }
    return sas_;
}

bool PairPipeline::approve_local(
    std::uint32_t approval_kind,
    const std::array<std::uint8_t, 6>& displayed_sas)
{
    const auto expected = sas();
    if (!expected || displayed_sas != *expected || approved_ ||
        !reducer_.approve_local(approval_kind))
        return reject();
    approved_ = true;
    return true;
}

bool PairPipeline::approve_local(std::uint32_t approval_kind)
{
    if (!begun_ || failed() || signature_mask_ != 3 || approved_ ||
        (start_.entry_mode == 1 && !start_.known_path) ||
        !reducer_.approve_local(approval_kind))
        return reject();
    approved_ = true;
    return true;
}

bool PairPipeline::submit_key_confirmation(
    PairRole role, const PlanHash& confirmation)
{
    if (!begun_ || failed() || !approved_ || !valid_role(role) ||
        (confirmation_mask_ & role_bit(role)) != 0)
        return reject();
    const auto operation = start_.key_confirm_operation_ids[role_index(role)];
    const auto operation_token = token(operation);
    if (operations_.expect(operation_token,
                           FLY_SESSION_PAIR_KEY_CONFIRM_VERIFIED_V2) !=
            FLY_SESSION_V2_OK)
        return reject();
    const bool verified = provider_.verify_key_confirmation(
        operation_token, role, reducer_.transcript_hash(), confirmation);
    if (!record_provider_terminal(
            operation_token, FLY_SESSION_PAIR_KEY_CONFIRM_VERIFIED_V2,
            verified ? FLY_SESSION_V2_OK : FLY_SESSION_V2_AUTH_FAILED) ||
        !reducer_.accept_provider_verification(
            operation, FLY_SESSION_PAIR_KEY_CONFIRM_VERIFIED_V2, role))
        return reject();
    confirmation_mask_ = static_cast<std::uint8_t>(
        confirmation_mask_ | role_bit(role));
    return true;
}

bool PairPipeline::accept_capability(
    PairRole sender, const CapabilitySummary& summary,
    const PlanHash& logical_hash)
{
    if (!begun_ || failed() || confirmation_mask_ != 3 ||
        !reducer_.accept_capability_reveal(sender, summary, logical_hash))
        return reject();
    return true;
}

std::optional<VerifiedPairEvidence> PairPipeline::take_verified_evidence()
{
    if (!begun_ || failed())
        return std::nullopt;
    return reducer_.take_verified_evidence();
}

PairVerificationScheduler::PairVerificationScheduler() = default;

bool PairVerificationScheduler::valid_role(PairRole role) noexcept
{
    return role == PairRole::Initiator || role == PairRole::Responder;
}

std::size_t PairVerificationScheduler::role_index(PairRole role) noexcept
{
    return role == PairRole::Initiator ? 0u : 1u;
}

std::uint8_t PairVerificationScheduler::role_bit(PairRole role) noexcept
{
    return role == PairRole::Initiator ? 1u : 2u;
}

bool PairVerificationScheduler::canonical_low_s(
    const std::array<std::uint8_t, 64>& signature) noexcept
{
    static constexpr std::array<std::uint8_t, 32> half_order{{
        0x7f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00,
        0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xde, 0x73, 0x7d, 0x56, 0xd3, 0x8b, 0xcf, 0x42,
        0x79, 0xdc, 0xe5, 0x61, 0x7e, 0x31, 0x92, 0xa8}};
    const bool r_nonzero = std::any_of(
        signature.begin(), signature.begin() + 32,
        [](std::uint8_t value) { return value != 0; });
    const bool s_nonzero = std::any_of(
        signature.begin() + 32, signature.end(),
        [](std::uint8_t value) { return value != 0; });
    return r_nonzero && s_nonzero &&
           !std::lexicographical_compare(
               half_order.begin(), half_order.end(), signature.begin() + 32,
               signature.end());
}

fly_session_op_token_v2 PairVerificationScheduler::token(
    std::uint64_t operation_id) const noexcept
{
    fly_session_op_token_v2 value{};
    value.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    value.abi_version = FLY_SESSION_ABI_VERSION_2;
    std::copy(start_.engine_instance_id.begin(), start_.engine_instance_id.end(),
              value.engine_instance_id);
    value.scope.struct_size = FLY_SESSION_SCOPE_V2_SIZE;
    value.scope.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.scope.kind = FLY_SESSION_SCOPE_LINK_V2;
    std::copy(start_.link_id.begin(), start_.link_id.end(), value.scope.link_id);
    value.connection_generation = start_.generation;
    value.operation_id = operation_id;
    return value;
}

bool PairVerificationScheduler::begin(const PairAuthStartV1& start)
{
    if (begun_ || failed_ || !reducer_.begin(start))
    {
        failed_ = true;
        return false;
    }
    start_ = start;
    begun_ = true;
    return true;
}

fly_session_result_v2 PairVerificationScheduler::queue_signature(
    PairRole role, const std::array<std::uint8_t, 64>& signature)
{
    if (!begun_ || failed() || pending_ || !valid_role(role) ||
        (queued_mask_ & role_bit(role)) != 0 || !canonical_low_s(signature) ||
        (role == PairRole::Initiator && verified_mask_ != 0) ||
        (role == PairRole::Responder && verified_mask_ != 1))
        return FLY_SESSION_V2_INVALID_STATE;
    const auto index = role_index(role);
    PairSignatureVerificationEffect effect{};
    effect.token = token(start_.signature_operation_ids[index]);
    effect.role = role;
    effect.public_key = role == PairRole::Initiator
        ? start_.initiator_contribution.identity_public_key
        : start_.responder_contribution.identity_public_key;
    effect.digest = reducer_.transcript_hash();
    effect.signature = signature;
    const auto expected = operations_.expect(
        effect.token, FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2);
    if (expected != FLY_SESSION_V2_OK)
        return expected;
    queued_mask_ = static_cast<std::uint8_t>(queued_mask_ | role_bit(role));
    pending_ = effect;
    return FLY_SESSION_V2_ACCEPTED;
}

std::optional<PairSignatureVerificationEffect>
PairVerificationScheduler::poll_signature_effect() const noexcept
{
    return pending_;
}

fly_session_result_v2 PairVerificationScheduler::complete_signature(
    const fly_session_port_event_v2& event)
{
    if (!pending_)
        return FLY_SESSION_V2_INVALID_STATE;
    ProviderOperationCompletion completion{};
    const auto accepted = operations_.accept(event, completion);
    if (accepted != FLY_SESSION_V2_OK)
        return accepted;
    if (completion.result != FLY_SESSION_V2_OK)
    {
        failed_ = true;
        pending_.reset();
        return completion.result;
    }
    const auto role = pending_->role;
    const auto operation_id = pending_->token.operation_id;
    if (!reducer_.accept_provider_verification(
            operation_id, FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2, role))
    {
        failed_ = true;
        pending_.reset();
        return FLY_SESSION_V2_AUTH_FAILED;
    }
    verified_mask_ = static_cast<std::uint8_t>(
        verified_mask_ | role_bit(role));
    pending_.reset();
    return FLY_SESSION_V2_OK;
}

bool PairVerificationScheduler::signature_verified(PairRole role) const noexcept
{
    return valid_role(role) && (verified_mask_ & role_bit(role)) != 0;
}

bool PairVerificationScheduler::approve_local(std::uint32_t approval_kind)
{
    if (!begun_ || failed() || !signatures_verified() || approved_ ||
        !reducer_.approve_local(approval_kind))
    {
        failed_ = true;
        return false;
    }
    approved_ = true;
    return true;
}

fly_session_result_v2 PairVerificationScheduler::queue_key_confirmation(
    PairRole role, fly_session_resource_handle_v2 key,
    const std::vector<std::uint8_t>& exact_input,
    const PlanHash& expected_mac)
{
    if (!begun_ || failed() || !approved_ || pending_ ||
        pending_key_confirm_ || !valid_role(role) || key == 0 ||
        exact_input.empty() || exact_input.size() > 4096 ||
        (key_confirm_queued_mask_ & role_bit(role)) != 0 ||
        (role == PairRole::Initiator && key_confirm_verified_mask_ != 0) ||
        (role == PairRole::Responder && key_confirm_verified_mask_ != 1))
        return FLY_SESSION_V2_INVALID_STATE;
    try
    {
        PairMacVerificationEffect effect{};
        const auto index = role_index(role);
        effect.token = token(start_.key_confirm_operation_ids[index]);
        effect.role = role;
        effect.key = key;
        effect.exact_input = exact_input;
        effect.expected_mac = expected_mac;
        const auto expected = operations_.expect(
            effect.token, FLY_SESSION_PROVIDER_CRYPTO_MAC_V2);
        if (expected != FLY_SESSION_V2_OK)
            return expected;
        key_confirm_queued_mask_ = static_cast<std::uint8_t>(
            key_confirm_queued_mask_ | role_bit(role));
        pending_key_confirm_ = std::move(effect);
        return FLY_SESSION_V2_ACCEPTED;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
}

std::optional<PairMacVerificationEffect>
PairVerificationScheduler::poll_key_confirmation_effect() const
{
    return pending_key_confirm_;
}

fly_session_result_v2 PairVerificationScheduler::complete_key_confirmation(
    const fly_session_port_event_v2& event)
{
    if (!pending_key_confirm_)
        return FLY_SESSION_V2_INVALID_STATE;
    ProviderOperationCompletion completion{};
    const auto accepted = operations_.accept(event, completion);
    if (accepted != FLY_SESSION_V2_OK)
        return accepted;
    if (completion.result != FLY_SESSION_V2_OK ||
        completion.payload.buffer == nullptr || completion.payload.value0 != 32)
    {
        failed_ = true;
        pending_key_confirm_.reset();
        return FLY_SESSION_V2_AUTH_FAILED;
    }
    PlanHash actual{};
    std::uint64_t written = 0;
    fly_session_write_bytes_v2 destination{actual.data(), actual.size()};
    if (fly_session_buffer_read_v2(completion.payload.buffer, 0, destination,
                                   &written) != FLY_SESSION_V2_OK ||
        written != actual.size())
    {
        failed_ = true;
        pending_key_confirm_.reset();
        return FLY_SESSION_V2_AUTH_FAILED;
    }
    std::uint8_t difference = 0;
    for (std::size_t index = 0; index < actual.size(); ++index)
        difference = static_cast<std::uint8_t>(
            difference | (actual[index] ^
                          pending_key_confirm_->expected_mac[index]));
    const auto role = pending_key_confirm_->role;
    const auto operation_id = pending_key_confirm_->token.operation_id;
    if (difference != 0 || !reducer_.accept_provider_verification(
                               operation_id,
                               FLY_SESSION_PAIR_KEY_CONFIRM_VERIFIED_V2, role))
    {
        failed_ = true;
        pending_key_confirm_.reset();
        return FLY_SESSION_V2_AUTH_FAILED;
    }
    key_confirm_verified_mask_ = static_cast<std::uint8_t>(
        key_confirm_verified_mask_ | role_bit(role));
    pending_key_confirm_.reset();
    return FLY_SESSION_V2_OK;
}

bool PairVerificationScheduler::accept_capability(
    PairRole sender, const CapabilitySummary& summary,
    const PlanHash& logical_hash)
{
    if (!key_confirmations_verified() || failed() ||
        !reducer_.accept_capability_reveal(sender, summary, logical_hash))
    {
        failed_ = true;
        return false;
    }
    return true;
}

std::optional<VerifiedPairEvidence>
PairVerificationScheduler::take_verified_evidence()
{
    if (failed())
        return std::nullopt;
    return reducer_.take_verified_evidence();
}

} // namespace flynes::session
