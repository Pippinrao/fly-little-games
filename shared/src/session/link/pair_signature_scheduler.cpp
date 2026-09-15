#include "pair_signature_scheduler.hpp"

#include "../wire/p256_point.hpp"
#include "../wire/sha256.hpp"

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

PairSignatureScheduler::PairSignatureScheduler() = default;

bool PairSignatureScheduler::local_is_initiator() const noexcept
{
    return start_.local_role == wire::PairRoleV1::Initiator;
}

fly_session_op_token_v2 PairSignatureScheduler::token(
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

bool PairSignatureScheduler::begin(const PairSignatureStartV1& start)
{
    if (begun_ || failed_ || start.generation == 0 ||
        start.first_operation_id == 0 ||
        start.first_operation_id >
            (std::numeric_limits<std::uint64_t>::max)() - 9 ||
        !nonzero(start.engine_instance_id.data(), start.engine_instance_id.size()) ||
        !nonzero(start.link_id.data(), start.link_id.size()) ||
        (start.local_role != wire::PairRoleV1::Initiator &&
         start.local_role != wire::PairRoleV1::Responder) ||
        start.local_identity_key == 0 || start.ecdh_secret == 0)
        return false;
    start_ = start;
    PairAuthStartV1 auth{};
    auth.generation = start.generation;
    auth.engine_instance_id = start.engine_instance_id;
    auth.link_id = start.link_id;
    if (start.local_role == wire::PairRoleV1::Initiator)
        auth.signature_operation_ids = {{start.first_operation_id + 3,
                                         start.first_operation_id + 6}};
    else
        auth.signature_operation_ids = {{start.first_operation_id + 3,
                                         start.first_operation_id + 5}};
    auth.key_confirm_operation_ids = {{start.first_operation_id + 8,
                                       start.first_operation_id + 9}};
    auth.local_role = public_role(start.local_role);
    auth.entry_mode = 1;
    auth.pair_context_hash = start.context.hash;
    auth.initiator_commit = start.initiator_commit;
    auth.responder_commit = start.responder_commit;
    auth.initiator_contribution = start.initiator_contribution;
    auth.responder_contribution = start.responder_contribution;
    auth.initiator_reveal_logical_hash =
        start.initiator_reveal_logical_hash;
    auth.responder_reveal_logical_hash =
        start.responder_reveal_logical_hash;
    if (!verification_.begin(auth))
        return false;
    transcript_hash_ = verification_.transcript_hash();
    start_ = start;
    begun_ = true;
    next_operation_id_ = start.first_operation_id;
    return prepare(Stage::DeriveI2r) == FLY_SESSION_V2_OK;
}

fly_session_result_v2 PairSignatureScheduler::prepare(Stage stage)
{
    try
    {
        PairSignatureEffect effect{};
        effect.token = token(next_operation_id_++);
        std::uint32_t expected = 0;
        if (stage == Stage::DeriveI2r || stage == Stage::DeriveR2i)
        {
            static constexpr char i2r[] = "flynes-pair-control-i2r-v1";
            static constexpr char r2i[] = "flynes-pair-control-r2i-v1";
            static constexpr char salt_label[] = "flynes-pair-v1";
            std::array<std::uint8_t,
                       sizeof(salt_label) - 1 + 32> salt_input{};
            std::copy_n(reinterpret_cast<const std::uint8_t*>(salt_label),
                        sizeof(salt_label) - 1, salt_input.begin());
            std::copy(transcript_hash_.begin(), transcript_hash_.end(),
                      salt_input.begin() + sizeof(salt_label) - 1);
            const auto salt = wire::sha256(salt_input.data(), salt_input.size());
            const auto* label = stage == Stage::DeriveI2r ? i2r : r2i;
            const auto length = stage == Stage::DeriveI2r
                ? sizeof(i2r) - 1 : sizeof(r2i) - 1;
            effect.kind = PairSignatureEffectKind::DeriveKey;
            effect.resource = start_.ecdh_secret;
            effect.salt.assign(salt.begin(), salt.end());
            effect.info.assign(reinterpret_cast<const std::uint8_t*>(label),
                               reinterpret_cast<const std::uint8_t*>(label) + length);
            effect.byte_count = 32;
            expected = FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2;
        }
        else if (stage == Stage::SignLocal)
        {
            static constexpr char domain[] = "flynes-pair-signature-v1";
            effect.kind = PairSignatureEffectKind::Sign;
            effect.resource = start_.local_identity_key;
            effect.key_purpose = FLY_SESSION_KEY_DEVICE_IDENTITY_V2;
            effect.domain.assign(
                reinterpret_cast<const std::uint8_t*>(domain),
                reinterpret_cast<const std::uint8_t*>(domain) + sizeof(domain) - 1);
            effect.digest = transcript_hash_;
            expected = FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2;
        }
        else if (stage == Stage::SealLocal || stage == Stage::OpenPeer)
        {
            const auto sender = stage == Stage::SealLocal
                ? start_.local_role
                : local_is_initiator() ? wire::PairRoleV1::Responder
                                       : wire::PairRoleV1::Initiator;
            effect.kind = stage == Stage::SealLocal
                ? PairSignatureEffectKind::AeadSeal
                : PairSignatureEffectKind::AeadOpen;
            effect.resource = sender == wire::PairRoleV1::Initiator
                ? secrets_.control_i2r_key : secrets_.control_r2i_key;
            std::array<std::uint8_t, wire::kPairSecureNonceSizeV1> nonce{};
            std::array<std::uint8_t, wire::kPairSecureAadSizeV1> aad{};
            if (wire::build_pair_secure_nonce_v1(sender, 1, &nonce) !=
                    wire::Status::Ok ||
                wire::build_pair_secure_aad_v1(
                    6, sender, transcript_hash_, 1, &aad) != wire::Status::Ok)
                return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
            effect.nonce.assign(nonce.begin(), nonce.end());
            effect.aad.assign(aad.begin(), aad.end());
            if (stage == Stage::OpenPeer)
            {
                if (!peer_envelope_)
                    return reject(FLY_SESSION_V2_INVALID_STATE);
                effect.input = peer_envelope_->ciphertext_and_tag;
            }
            else
            {
                const auto& local_key = local_is_initiator()
                    ? start_.initiator_contribution.identity_public_key
                    : start_.responder_contribution.identity_public_key;
                const auto& peer_key = local_is_initiator()
                    ? start_.responder_contribution.identity_public_key
                    : start_.initiator_contribution.identity_public_key;
                std::array<std::uint8_t,
                           wire::kPairSignatureInnerSizeV1> inner{};
                if (wire::encode_pair_signature_inner_v1(
                        transcript_hash_, start_.local_role, local_key,
                        peer_key, local_signature_,
                        wire::validate_p256_uncompressed_point_callback,
                        nullptr, &inner) != wire::Status::Ok)
                    return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
                effect.input.assign(inner.begin(), inner.end());
            }
            expected = FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
        }
        else
            return reject(FLY_SESSION_V2_INVALID_STATE);
        const auto result = operations_.expect(effect.token, expected);
        if (result != FLY_SESSION_V2_OK) return reject(result);
        pending_ = std::move(effect);
        stage_ = stage;
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 PairSignatureScheduler::prepare_verify(
    PairRole role, const std::array<std::uint8_t, 64>& signature)
{
    const auto result = verification_.queue_signature(role, signature);
    if (result != FLY_SESSION_V2_ACCEPTED) return reject(result);
    const auto verification = verification_.poll_signature_effect();
    if (!verification || verification->token.operation_id != next_operation_id_)
        return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    PairSignatureEffect effect{};
    effect.kind = PairSignatureEffectKind::Verify;
    effect.token = verification->token;
    effect.public_key.assign(verification->public_key.begin(),
                             verification->public_key.end());
    effect.signature.assign(verification->signature.begin(),
                            verification->signature.end());
    effect.digest = verification->digest;
    static constexpr char domain[] = "flynes-pair-signature-v1";
    effect.domain.assign(reinterpret_cast<const std::uint8_t*>(domain),
                         reinterpret_cast<const std::uint8_t*>(domain) +
                             sizeof(domain) - 1);
    ++next_operation_id_;
    verifying_role_ = role;
    verifying_signature_ = signature;
    pending_ = std::move(effect);
    stage_ = Stage::VerifySignature;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 PairSignatureScheduler::prepare_transcript_persist()
{
    std::array<std::uint8_t, wire::kPairTranscriptPreimageSizeV1> preimage{};
    std::array<std::uint8_t, 32> hash{};
    if (wire::build_pair_transcript_preimage_v1(
            1, {}, start_.initiator_commit.commitment,
            start_.responder_commit.commitment,
            start_.initiator_contribution, start_.responder_contribution,
            &preimage, &hash) != wire::Status::Ok || hash != transcript_hash_)
        return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    std::array<std::uint8_t, wire::kPairTranscriptSizeV1> transcript{};
    std::array<std::uint8_t, 32> object_hash{};
    if (wire::build_pair_transcript_object_v1(
            preimage, transcript_hash_, initiator_signature_,
            responder_signature_, &transcript, &object_hash) != wire::Status::Ok)
        return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    try
    {
        PairSignatureEffect effect{};
        effect.kind = PairSignatureEffectKind::PersistTranscript;
        effect.token = token(next_operation_id_++);
        effect.object_kind = wire::kPairTranscriptObjectKindV1;
        effect.expected_hash = object_hash;
        effect.input.assign(transcript.begin(), transcript.end());
        const auto result = operations_.expect(
            effect.token, FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2);
        if (result != FLY_SESSION_V2_OK) return reject(result);
        pending_ = std::move(effect);
        stage_ = Stage::PersistTranscript;
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

std::optional<PairSignatureEffect> PairSignatureScheduler::poll_effect() const
{
    return pending_;
}

fly_session_result_v2 PairSignatureScheduler::read_buffer(
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

fly_session_result_v2 PairSignatureScheduler::complete(
    const fly_session_port_event_v2& event)
{
    if (!pending_ || failed_) return FLY_SESSION_V2_INVALID_STATE;
    const auto completed_stage = stage_;
    if (completed_stage == Stage::VerifySignature)
    {
        pending_.reset();
        const auto result = verification_.complete_signature(event);
        if (result != FLY_SESSION_V2_OK) return reject(result);
        if (verifying_role_ == PairRole::Initiator)
            initiator_signature_ = verifying_signature_;
        else
            responder_signature_ = verifying_signature_;
        if (verifying_role_ == PairRole::Initiator && local_is_initiator())
            return prepare(Stage::SealLocal);
        if (verifying_role_ == PairRole::Initiator && !local_is_initiator())
            return prepare(Stage::SignLocal);
        if (verifying_role_ == PairRole::Responder && !local_is_initiator())
            return prepare(Stage::SealLocal);
        if (local_sent_)
            return prepare_transcript_persist();
        stage_ = Stage::WaitLocalSent;
        return FLY_SESSION_V2_OK;
    }

    const auto expected_hash = pending_->expected_hash;
    ProviderOperationCompletion completion{};
    const auto accepted = operations_.accept(event, completion);
    if (accepted != FLY_SESSION_V2_OK) return accepted;
    if (completion.result != FLY_SESSION_V2_OK) return reject(completion.result);
    pending_.reset();
    std::vector<std::uint8_t> bytes;
    if (completed_stage == Stage::DeriveI2r)
    {
        if (!completion.payload.resource)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        secrets_.control_i2r_key = completion.payload.resource;
        return prepare(Stage::DeriveR2i);
    }
    if (completed_stage == Stage::DeriveR2i)
    {
        if (!completion.payload.resource)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        secrets_.control_r2i_key = completion.payload.resource;
        if (local_is_initiator()) return prepare(Stage::SignLocal);
        if (peer_envelope_) return prepare(Stage::OpenPeer);
        stage_ = Stage::WaitPeer;
        return FLY_SESSION_V2_OK;
    }
    if (completed_stage == Stage::SignLocal)
    {
        auto result = read_buffer(completion, bytes);
        if (result != FLY_SESSION_V2_OK || bytes.size() != 64)
            return reject(result == FLY_SESSION_V2_OK
                              ? FLY_SESSION_V2_CONTRACT_VIOLATION : result);
        std::array<std::uint8_t, 64> signature{};
        std::copy(bytes.begin(), bytes.end(), signature.begin());
        local_signature_ = signature;
        const auto local = public_role(start_.local_role);
        return prepare_verify(local, signature);
    }
    if (completed_stage == Stage::SealLocal)
    {
        auto result = read_buffer(completion, bytes);
        if (result != FLY_SESSION_V2_OK || bytes.size() != 192)
            return reject(result == FLY_SESSION_V2_OK
                              ? FLY_SESSION_V2_CONTRACT_VIOLATION : result);
        std::vector<std::uint8_t> envelope;
        if (wire::encode_pair_secure_envelope_v1(
                6, 1, bytes.data(), bytes.size(), &envelope) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        local_envelope_ = std::move(envelope);
        stage_ = Stage::WaitLocalSent;
        return FLY_SESSION_V2_OK;
    }
    if (completed_stage == Stage::OpenPeer)
    {
        auto result = read_buffer(completion, bytes);
        if (result != FLY_SESSION_V2_OK || bytes.size() != 176)
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
        wire::PairSignatureInnerV1 inner{};
        if (wire::decode_pair_signature_inner_v1(
                bytes.data(), bytes.size(), transcript_hash_, peer,
                sender_key, receiver_key,
                wire::validate_p256_uncompressed_point_callback, nullptr,
                &inner) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        return prepare_verify(public_role(peer), inner.signature);
    }
    if (completed_stage == Stage::PersistTranscript)
    {
        if (completion.payload.resource == 0 ||
            completion.payload.hash != expected_hash)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        transcript_object_ref_ = completion.payload.resource;
        transcript_object_hash_ = completion.payload.hash;
        ready_ = true;
        stage_ = Stage::Ready;
        return FLY_SESSION_V2_OK;
    }
    return reject(FLY_SESSION_V2_INVALID_STATE);
}

fly_session_result_v2 PairSignatureScheduler::accept_peer_envelope(
    const std::uint8_t* body, std::size_t size,
    const std::array<std::uint8_t, 32>& logical_hash)
{
    if (!begun_ || failed_ || peer_envelope_ ||
        !nonzero(logical_hash.data(), logical_hash.size()) ||
        (local_is_initiator() && stage_ != Stage::WaitPeer) ||
        (!local_is_initiator() && stage_ != Stage::DeriveI2r &&
         stage_ != Stage::DeriveR2i && stage_ != Stage::WaitPeer))
        return FLY_SESSION_V2_INVALID_STATE;
    wire::PairSecureEnvelopeV1 envelope{};
    if (wire::decode_pair_secure_envelope_v1(6, body, size, &envelope) !=
            wire::Status::Ok || envelope.message_counter != 1)
        return reject(FLY_SESSION_V2_AUTH_FAILED);
    peer_envelope_ = std::move(envelope);
    peer_logical_hash_ = logical_hash;
    if (pending_) return FLY_SESSION_V2_ACCEPTED;
    return prepare(Stage::OpenPeer) == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_ACCEPTED : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 PairSignatureScheduler::mark_local_signature_sent() noexcept
{
    if (!local_envelope_ || local_sent_ || stage_ != Stage::WaitLocalSent)
        return FLY_SESSION_V2_INVALID_STATE;
    local_sent_ = true;
    if (local_is_initiator())
        stage_ = peer_envelope_ ? Stage::OpenPeer : Stage::WaitPeer;
    else
    {
        if (!verification_.signatures_verified())
            stage_ = Stage::Failed;
        else
            return prepare_transcript_persist();
    }
    return stage_ == Stage::Failed
        ? reject(FLY_SESSION_V2_INVALID_STATE) : FLY_SESSION_V2_OK;
}

fly_session_result_v2 PairSignatureScheduler::cancel_pending() noexcept
{
    if (!pending_) return FLY_SESSION_V2_INVALID_STATE;
    if (stage_ == Stage::VerifySignature)
    {
        failed_ = true;
        pending_.reset();
        stage_ = Stage::Failed;
        return FLY_SESSION_V2_OK;
    }
    const auto result = operations_.cancel(pending_->token);
    if (result == FLY_SESSION_V2_OK)
        reject(FLY_SESSION_V2_CANCELLED);
    return result;
}

bool PairSignatureScheduler::approve_local(
    std::uint32_t approval_kind) noexcept
{
    if (!ready() || approved_ ||
        !verification_.approve_local(approval_kind))
        return false;
    approved_ = true;
    return true;
}

fly_session_result_v2 PairSignatureScheduler::queue_key_confirmation(
    PairRole role, fly_session_resource_handle_v2 key,
    const std::vector<std::uint8_t>& exact_input,
    const std::array<std::uint8_t, 32>& expected_mac)
{
    if (!approved_ || !ready())
        return FLY_SESSION_V2_INVALID_STATE;
    return verification_.queue_key_confirmation(
        role, key, exact_input, expected_mac);
}

std::optional<PairMacVerificationEffect>
PairSignatureScheduler::poll_key_confirmation_effect() const
{
    return verification_.poll_key_confirmation_effect();
}

fly_session_result_v2 PairSignatureScheduler::complete_key_confirmation(
    const fly_session_port_event_v2& event)
{
    if (!approved_ || !ready())
        return FLY_SESSION_V2_INVALID_STATE;
    return verification_.complete_key_confirmation(event);
}

bool PairSignatureScheduler::accept_capability(
    PairRole sender, const CapabilitySummary& summary,
    const std::array<std::uint8_t, 32>& logical_hash)
{
    return verification_.accept_capability(sender, summary, logical_hash);
}

std::optional<VerifiedPairEvidence>
PairSignatureScheduler::take_verified_evidence()
{
    return verification_.take_verified_evidence();
}

fly_session_result_v2 PairSignatureScheduler::reject(
    fly_session_result_v2 result) noexcept
{
    pending_.reset();
    failed_ = true;
    stage_ = Stage::Failed;
    return result == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_CONTRACT_VIOLATION : result;
}

} // namespace flynes::session
