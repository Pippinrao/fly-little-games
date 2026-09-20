#include "pair_capability_scheduler.hpp"

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

bool PairCapabilityScheduler::local_is_initiator() const noexcept
{
    return start_.local_role == wire::PairRoleV1::Initiator;
}

fly_session_op_token_v2 PairCapabilityScheduler::token(
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

bool PairCapabilityScheduler::begin(const PairCapabilityStartV1& start)
{
    if (begun_ || failed_ || !verification_ ||
        !verification_->key_confirmations_verified() || start.generation == 0 ||
        start.first_operation_id == 0 ||
        start.first_operation_id > (std::numeric_limits<std::uint64_t>::max)() - 5 ||
        !nonzero(start.engine_instance_id.data(), start.engine_instance_id.size()) ||
        !nonzero(start.link_id.data(), start.link_id.size()) ||
        !nonzero(start.transcript_hash.data(), start.transcript_hash.size()) ||
        (start.local_role != wire::PairRoleV1::Initiator &&
         start.local_role != wire::PairRoleV1::Responder) ||
        wire::validate_pair_capability(start.local_summary.data(),
                                       start.local_summary.size()) !=
            wire::Status::Ok ||
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

fly_session_result_v2 PairCapabilityScheduler::prepare_local_hmac()
{
    try
    {
        std::array<std::uint8_t, wire::kPairCapabilityInnerSizeV1> inner{};
        std::array<std::uint8_t, 32> empty_tag{};
        if (wire::encode_pair_capability_inner_v1(
                start_.transcript_hash, start_.local_role,
                start_.local_summary, empty_tag, &inner) != wire::Status::Ok ||
            wire::build_pair_capability_hmac_input_v1(
                inner.data(), wire::kPairCapabilityPretagSizeV1,
                &local_hmac_input_) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        PairCapabilityEffect effect{};
        effect.kind = PairCapabilityEffectKind::Hmac;
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

fly_session_result_v2 PairCapabilityScheduler::prepare_aead(bool seal)
{
    try
    {
        const auto sender = seal
            ? start_.local_role
            : local_is_initiator() ? wire::PairRoleV1::Responder
                                   : wire::PairRoleV1::Initiator;
        PairCapabilityEffect effect{};
        effect.kind = seal ? PairCapabilityEffectKind::AeadSeal
                           : PairCapabilityEffectKind::AeadOpen;
        effect.token = token(next_operation_id_++);
        effect.resource = sender == wire::PairRoleV1::Initiator
            ? start_.control_i2r_key : start_.control_r2i_key;
        std::array<std::uint8_t, wire::kPairSecureNonceSizeV1> nonce{};
        std::array<std::uint8_t, wire::kPairSecureAadSizeV1> aad{};
        if (wire::build_pair_secure_nonce_v1(sender, 5, &nonce) !=
                wire::Status::Ok ||
            wire::build_pair_secure_aad_v1(
                23, sender, start_.transcript_hash, 5, &aad) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        effect.nonce.assign(nonce.begin(), nonce.end());
        effect.aad.assign(aad.begin(), aad.end());
        if (seal)
        {
            std::array<std::uint8_t, wire::kPairCapabilityInnerSizeV1> inner{};
            if (wire::encode_pair_capability_inner_v1(
                    start_.transcript_hash, start_.local_role,
                    start_.local_summary, local_tag_, &inner) != wire::Status::Ok)
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

fly_session_result_v2 PairCapabilityScheduler::prepare_peer_hmac()
{
    try
    {
        PairCapabilityEffect effect{};
        effect.kind = PairCapabilityEffectKind::Hmac;
        effect.token = token(next_operation_id_++);
        effect.resource = local_is_initiator()
            ? start_.gatt_r2i_key : start_.gatt_i2r_key;
        effect.input = peer_hmac_input_;
        const auto expected = operations_.expect(
            effect.token, FLY_SESSION_PROVIDER_CRYPTO_MAC_V2);
        if (expected != FLY_SESSION_V2_OK)
            return reject(expected);
        pending_ = std::move(effect);
        stage_ = Stage::HmacPeer;
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 PairCapabilityScheduler::read_buffer(
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

fly_session_result_v2 PairCapabilityScheduler::complete(
    const fly_session_port_event_v2& event)
{
    if (!pending_ || failed_)
        return FLY_SESSION_V2_INVALID_STATE;
    const auto completed_stage = stage_;
    ProviderOperationCompletion completion{};
    const auto accepted = operations_.accept(event, completion);
    if (accepted != FLY_SESSION_V2_OK)
        return accepted;
    if (completion.result != FLY_SESSION_V2_OK)
        return reject(completion.result);
    pending_.reset();
    std::vector<std::uint8_t> bytes;
    auto result = read_buffer(completion, bytes);
    if (result != FLY_SESSION_V2_OK)
        return reject(result);
    if (completed_stage == Stage::HmacLocal)
    {
        if (bytes.size() != 32)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::copy(bytes.begin(), bytes.end(), local_tag_.begin());
        return prepare_aead(true);
    }
    if (completed_stage == Stage::SealLocal)
    {
        if (bytes.size() != 608)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::vector<std::uint8_t> envelope;
        if (wire::encode_pair_secure_envelope_v1(
                23, 5, bytes.data(), bytes.size(), &envelope) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        local_envelope_ = std::move(envelope);
        stage_ = Stage::WaitLocalSent;
        return FLY_SESSION_V2_OK;
    }
    if (completed_stage == Stage::OpenPeer)
    {
        if (bytes.size() != wire::kPairCapabilityInnerSizeV1)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        wire::PairCapabilityInnerV1 inner{};
        const auto peer = local_is_initiator()
            ? wire::PairRoleV1::Responder : wire::PairRoleV1::Initiator;
        if (wire::decode_pair_capability_inner_v1(
                bytes.data(), bytes.size(), start_.transcript_hash,
                peer, &inner) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        peer_summary_ = inner.summary;
        peer_tag_ = inner.tag;
        try
        {
            if (wire::build_pair_capability_hmac_input_v1(
                    bytes.data(), wire::kPairCapabilityPretagSizeV1,
                    &peer_hmac_input_) != wire::Status::Ok)
                return reject(FLY_SESSION_V2_AUTH_FAILED);
        }
        catch (const std::bad_alloc&)
        {
            return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
        }
        return prepare_peer_hmac();
    }
    if (completed_stage == Stage::HmacPeer)
    {
        if (bytes.size() != 32 || !peer_summary_)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        std::uint8_t difference = 0;
        for (std::size_t i = 0; i < peer_tag_.size(); ++i)
            difference = static_cast<std::uint8_t>(
                difference | (bytes[i] ^ peer_tag_[i]));
        const auto peer = local_is_initiator()
            ? PairRole::Responder : PairRole::Initiator;
        if (difference != 0 ||
            !verification_->accept_capability(
                peer, *peer_summary_, peer_logical_hash_))
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        peer_verified_ = true;
        if (!local_is_initiator())
            return prepare_local_hmac();
        ready_ = local_sent_;
        stage_ = ready_ ? Stage::Ready : Stage::WaitLocalSent;
        return FLY_SESSION_V2_OK;
    }
    return reject(FLY_SESSION_V2_INVALID_STATE);
}

fly_session_result_v2 PairCapabilityScheduler::accept_peer_envelope(
    const std::uint8_t* body, std::size_t size,
    const std::array<std::uint8_t, 32>& logical_hash)
{
    if (!begun_ || failed_ || peer_envelope_ || stage_ != Stage::WaitPeer ||
        !nonzero(logical_hash.data(), logical_hash.size()))
        return FLY_SESSION_V2_INVALID_STATE;
    wire::PairSecureEnvelopeV1 envelope{};
    if (wire::decode_pair_secure_envelope_v1(23, body, size, &envelope) !=
            wire::Status::Ok || envelope.message_counter != 5)
        return reject(FLY_SESSION_V2_AUTH_FAILED);
    peer_envelope_ = std::move(envelope);
    peer_logical_hash_ = logical_hash;
    return prepare_aead(false) == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_ACCEPTED : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 PairCapabilityScheduler::mark_local_sent(
    const std::array<std::uint8_t, 32>& logical_hash) noexcept
{
    if (!local_envelope_ || local_sent_ || stage_ != Stage::WaitLocalSent ||
        !nonzero(logical_hash.data(), logical_hash.size()) ||
        !verification_->accept_capability(
            public_role(start_.local_role), start_.local_summary, logical_hash))
        return reject(FLY_SESSION_V2_INVALID_STATE);
    local_sent_ = true;
    local_logical_hash_ = logical_hash;
    if (local_is_initiator())
        stage_ = Stage::WaitPeer;
    else
    {
        ready_ = peer_verified_;
        stage_ = ready_ ? Stage::Ready : Stage::Failed;
    }
    return stage_ == Stage::Failed
        ? reject(FLY_SESSION_V2_INVALID_STATE) : FLY_SESSION_V2_OK;
}

fly_session_result_v2 PairCapabilityScheduler::cancel_pending() noexcept
{
    if (!pending_)
        return FLY_SESSION_V2_INVALID_STATE;
    const auto result = operations_.cancel(pending_->token);
    if (result == FLY_SESSION_V2_OK)
        reject(FLY_SESSION_V2_CANCELLED);
    return result;
}

fly_session_result_v2 PairCapabilityScheduler::reject(
    fly_session_result_v2 result) noexcept
{
    pending_.reset();
    failed_ = true;
    stage_ = Stage::Failed;
    return result == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_CONTRACT_VIOLATION : result;
}

} // namespace flynes::session
