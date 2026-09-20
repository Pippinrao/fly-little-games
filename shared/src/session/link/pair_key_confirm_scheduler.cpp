#include "pair_key_confirm_scheduler.hpp"

#include "../wire/p256_point.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace flynes::session {
namespace {

bool nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return bytes && std::any_of(bytes, bytes + size,
        [](std::uint8_t value) { return value != 0; });
}

PairRole public_role(wire::PairRoleV1 role) noexcept
{
    return role == wire::PairRoleV1::Initiator
        ? PairRole::Initiator : PairRole::Responder;
}

} // namespace

bool PairKeyConfirmScheduler::local_is_initiator() const noexcept
{
    return start_.local_role == wire::PairRoleV1::Initiator;
}

fly_session_op_token_v2 PairKeyConfirmScheduler::token(
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

bool PairKeyConfirmScheduler::begin(const PairKeyConfirmStartV1& start)
{
    if (begun_ || failed_ || !verification_ || !verification_->ready() ||
        !verification_->local_approved() || start.generation == 0 ||
        start.first_operation_id == 0 ||
        start.first_operation_id > (std::numeric_limits<std::uint64_t>::max)() - 6 ||
        !nonzero(start.engine_instance_id.data(), start.engine_instance_id.size()) ||
        !nonzero(start.link_id.data(), start.link_id.size()) ||
        !nonzero(start.transcript_hash.data(), start.transcript_hash.size()) ||
        (start.local_role != wire::PairRoleV1::Initiator &&
         start.local_role != wire::PairRoleV1::Responder) ||
        start.gatt_i2r_key == 0 || start.gatt_r2i_key == 0 ||
        start.control_i2r_key == 0 || start.control_r2i_key == 0)
        return false;
    start_ = start;
    next_operation_id_ = start.first_operation_id;
    begun_ = true;
    if (local_is_initiator())
        return prepare_local_hmac() == FLY_SESSION_V2_OK;
    stage_ = Stage::WaitPeer;
    return true;
}

fly_session_result_v2 PairKeyConfirmScheduler::prepare_local_hmac()
{
    try
    {
        const auto& local_key = local_is_initiator()
            ? start_.initiator_contribution.identity_public_key
            : start_.responder_contribution.identity_public_key;
        const auto& peer_key = local_is_initiator()
            ? start_.responder_contribution.identity_public_key
            : start_.initiator_contribution.identity_public_key;
        std::array<std::uint8_t, wire::kKeyConfirmInnerSizeV1> inner{};
        std::array<std::uint8_t, 32> empty_tag{};
        if (wire::encode_key_confirm_inner_v1(
                start_.entry_mode, start_.approval_kind,
                start_.transcript_hash, start_.local_role, local_key, peer_key,
                empty_tag, wire::validate_p256_uncompressed_point_callback,
                nullptr, &inner) != wire::Status::Ok ||
            wire::build_key_confirm_hmac_input_v1(
                inner.data(), wire::kKeyConfirmBodySizeV1,
                &local_hmac_input_) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        PairKeyConfirmEffect effect{};
        effect.kind = PairKeyConfirmEffectKind::Hmac;
        effect.token = token(next_operation_id_++);
        effect.resource = local_is_initiator()
            ? start_.gatt_i2r_key : start_.gatt_r2i_key;
        effect.input = local_hmac_input_;
        const auto expected = operations_.expect(
            effect.token, FLY_SESSION_PROVIDER_CRYPTO_MAC_V2);
        if (expected != FLY_SESSION_V2_OK)
            return reject(expected);
        pending_ = std::move(effect);
        stage_ = Stage::HmacLocal;
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 PairKeyConfirmScheduler::prepare_verify(
    PairRole role, fly_session_resource_handle_v2 key,
    const std::vector<std::uint8_t>& input,
    const std::array<std::uint8_t, 32>& tag, Stage stage)
{
    const auto queued = verification_->queue_key_confirmation(
        role, key, input, tag);
    if (queued != FLY_SESSION_V2_ACCEPTED)
        return reject(queued);
    const auto verification = verification_->poll_key_confirmation_effect();
    if (!verification)
        return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    PairKeyConfirmEffect effect{};
    effect.kind = PairKeyConfirmEffectKind::Hmac;
    effect.token = verification->token;
    effect.resource = verification->key;
    effect.input = verification->exact_input;
    pending_ = std::move(effect);
    stage_ = stage;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 PairKeyConfirmScheduler::prepare_aead(bool seal)
{
    try
    {
        const auto sender = seal
            ? start_.local_role
            : local_is_initiator() ? wire::PairRoleV1::Responder
                                   : wire::PairRoleV1::Initiator;
        PairKeyConfirmEffect effect{};
        effect.kind = seal ? PairKeyConfirmEffectKind::AeadSeal
                           : PairKeyConfirmEffectKind::AeadOpen;
        effect.token = token(next_operation_id_++);
        effect.resource = sender == wire::PairRoleV1::Initiator
            ? start_.control_i2r_key : start_.control_r2i_key;
        std::array<std::uint8_t, wire::kPairSecureNonceSizeV1> nonce{};
        std::array<std::uint8_t, wire::kPairSecureAadSizeV1> aad{};
        if (wire::build_pair_secure_nonce_v1(sender, 4, &nonce) !=
                wire::Status::Ok ||
            wire::build_pair_secure_aad_v1(
                7, sender, start_.transcript_hash, 4, &aad) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        effect.nonce.assign(nonce.begin(), nonce.end());
        effect.aad.assign(aad.begin(), aad.end());
        if (seal)
        {
            const auto& local_key = local_is_initiator()
                ? start_.initiator_contribution.identity_public_key
                : start_.responder_contribution.identity_public_key;
            const auto& peer_key = local_is_initiator()
                ? start_.responder_contribution.identity_public_key
                : start_.initiator_contribution.identity_public_key;
            std::array<std::uint8_t, wire::kKeyConfirmInnerSizeV1> inner{};
            if (wire::encode_key_confirm_inner_v1(
                    start_.entry_mode, start_.approval_kind,
                    start_.transcript_hash, start_.local_role, local_key,
                    peer_key, local_tag_,
                    wire::validate_p256_uncompressed_point_callback,
                    nullptr, &inner) != wire::Status::Ok)
                return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
            effect.input.assign(inner.begin(), inner.end());
        }
        else
        {
            if (!peer_envelope_)
                return reject(FLY_SESSION_V2_INVALID_STATE);
            effect.input = peer_envelope_->ciphertext_and_tag;
        }
        const auto expected = operations_.expect(
            effect.token, FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2);
        if (expected != FLY_SESSION_V2_OK)
            return reject(expected);
        pending_ = std::move(effect);
        stage_ = seal ? Stage::SealLocal : Stage::OpenPeer;
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 PairKeyConfirmScheduler::read_buffer(
    const ProviderOperationCompletion& completion,
    std::vector<std::uint8_t>& out) const
{
    if (!completion.payload.buffer || completion.payload.value0 == 0 ||
        completion.payload.value0 > 4096)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    try { out.assign(static_cast<std::size_t>(completion.payload.value0), 0); }
    catch (const std::bad_alloc&) { return FLY_SESSION_V2_OUT_OF_MEMORY; }
    std::uint64_t written = 0;
    const fly_session_write_bytes_v2 destination{out.data(), out.size()};
    return fly_session_buffer_read_v2(
               completion.payload.buffer, 0, destination, &written) ==
               FLY_SESSION_V2_OK && written == out.size()
        ? FLY_SESSION_V2_OK : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 PairKeyConfirmScheduler::complete(
    const fly_session_port_event_v2& event)
{
    if (!pending_ || failed_)
        return FLY_SESSION_V2_INVALID_STATE;
    const auto completed_stage = stage_;
    if (completed_stage == Stage::VerifyLocal ||
        completed_stage == Stage::VerifyPeer)
    {
        pending_.reset();
        const auto result = verification_->complete_key_confirmation(event);
        if (result != FLY_SESSION_V2_OK)
            return reject(result);
        if (completed_stage == Stage::VerifyLocal)
            return prepare_aead(true);
        if (!local_is_initiator())
            return prepare_local_hmac();
        ready_ = local_sent_ && verification_->key_confirmations_verified();
        stage_ = ready_ ? Stage::Ready : Stage::WaitLocalSent;
        return FLY_SESSION_V2_OK;
    }
    ProviderOperationCompletion completion{};
    const auto accepted = operations_.accept(event, completion);
    if (accepted != FLY_SESSION_V2_OK)
        return accepted;
    if (completion.result != FLY_SESSION_V2_OK)
        return reject(completion.result);
    pending_.reset();
    std::vector<std::uint8_t> bytes;
    if (completed_stage == Stage::HmacLocal)
    {
        auto result = read_buffer(completion, bytes);
        if (result != FLY_SESSION_V2_OK || bytes.size() != 32)
            return reject(result == FLY_SESSION_V2_OK
                              ? FLY_SESSION_V2_CONTRACT_VIOLATION : result);
        std::copy(bytes.begin(), bytes.end(), local_tag_.begin());
        return prepare_verify(public_role(start_.local_role),
                              local_is_initiator() ? start_.gatt_i2r_key
                                                   : start_.gatt_r2i_key,
                              local_hmac_input_, local_tag_,
                              Stage::VerifyLocal);
    }
    if (completed_stage == Stage::SealLocal)
    {
        auto result = read_buffer(completion, bytes);
        if (result != FLY_SESSION_V2_OK || bytes.size() != 160)
            return reject(result == FLY_SESSION_V2_OK
                              ? FLY_SESSION_V2_CONTRACT_VIOLATION : result);
        std::vector<std::uint8_t> envelope;
        if (wire::encode_pair_secure_envelope_v1(
                7, 4, bytes.data(), bytes.size(), &envelope) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        local_envelope_ = std::move(envelope);
        stage_ = Stage::WaitLocalSent;
        return FLY_SESSION_V2_OK;
    }
    if (completed_stage == Stage::OpenPeer)
    {
        auto result = read_buffer(completion, bytes);
        if (result != FLY_SESSION_V2_OK || bytes.size() != 144)
            return reject(result == FLY_SESSION_V2_OK
                              ? FLY_SESSION_V2_AUTH_FAILED : result);
        const auto peer = local_is_initiator()
            ? wire::PairRoleV1::Responder : wire::PairRoleV1::Initiator;
        const auto& sender_key = peer == wire::PairRoleV1::Initiator
            ? start_.initiator_contribution.identity_public_key
            : start_.responder_contribution.identity_public_key;
        const auto& receiver_key = peer == wire::PairRoleV1::Initiator
            ? start_.responder_contribution.identity_public_key
            : start_.initiator_contribution.identity_public_key;
        wire::KeyConfirmInnerV1 inner{};
        if (wire::decode_key_confirm_inner_v1(
                bytes.data(), bytes.size(), start_.entry_mode,
                start_.approval_kind, start_.transcript_hash, peer,
                sender_key, receiver_key,
                wire::validate_p256_uncompressed_point_callback, nullptr,
                &inner) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        std::vector<std::uint8_t> input;
        try
        {
            if (wire::build_key_confirm_hmac_input_v1(
                    bytes.data(), wire::kKeyConfirmBodySizeV1,
                    &input) != wire::Status::Ok)
                return reject(FLY_SESSION_V2_AUTH_FAILED);
        }
        catch (const std::bad_alloc&)
        {
            return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
        }
        return prepare_verify(public_role(peer),
                              peer == wire::PairRoleV1::Initiator
                                  ? start_.gatt_i2r_key
                                  : start_.gatt_r2i_key,
                              input, inner.tag, Stage::VerifyPeer);
    }
    return reject(FLY_SESSION_V2_INVALID_STATE);
}

fly_session_result_v2 PairKeyConfirmScheduler::accept_peer_envelope(
    const std::uint8_t* body, std::size_t size,
    const std::array<std::uint8_t, 32>& logical_hash)
{
    if (!begun_ || failed_ || peer_envelope_ ||
        !nonzero(logical_hash.data(), logical_hash.size()) ||
        stage_ != Stage::WaitPeer)
        return FLY_SESSION_V2_INVALID_STATE;
    wire::PairSecureEnvelopeV1 envelope{};
    if (wire::decode_pair_secure_envelope_v1(7, body, size, &envelope) !=
            wire::Status::Ok || envelope.message_counter != 4)
        return reject(FLY_SESSION_V2_AUTH_FAILED);
    peer_envelope_ = std::move(envelope);
    return prepare_aead(false) == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_ACCEPTED : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 PairKeyConfirmScheduler::mark_local_sent() noexcept
{
    if (!local_envelope_ || local_sent_ || stage_ != Stage::WaitLocalSent)
        return FLY_SESSION_V2_INVALID_STATE;
    local_sent_ = true;
    if (local_is_initiator())
        stage_ = Stage::WaitPeer;
    else
    {
        ready_ = verification_->key_confirmations_verified();
        stage_ = ready_ ? Stage::Ready : Stage::Failed;
    }
    return stage_ == Stage::Failed
        ? reject(FLY_SESSION_V2_INVALID_STATE) : FLY_SESSION_V2_OK;
}

fly_session_result_v2 PairKeyConfirmScheduler::cancel_pending() noexcept
{
    if (!pending_)
        return FLY_SESSION_V2_INVALID_STATE;
    if (stage_ == Stage::VerifyLocal || stage_ == Stage::VerifyPeer)
        return reject(FLY_SESSION_V2_CANCELLED);
    const auto result = operations_.cancel(pending_->token);
    if (result == FLY_SESSION_V2_OK)
        reject(FLY_SESSION_V2_CANCELLED);
    return result;
}

fly_session_result_v2 PairKeyConfirmScheduler::reject(
    fly_session_result_v2 result) noexcept
{
    pending_.reset();
    failed_ = true;
    stage_ = Stage::Failed;
    return result == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_CONTRACT_VIOLATION : result;
}

} // namespace flynes::session
