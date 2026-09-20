#include "pair_known_scheduler.hpp"

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

bool validate_point(void*, const std::uint8_t point[65]) noexcept
{
    return wire::validate_p256_uncompressed_point(point);
}

constexpr char kBranchDomain[] = "flynes-known-branch-v1";

} // namespace

bool PairKnownScheduler::local_is_initiator() const noexcept
{
    return start_.local_role == wire::PairRoleV1::Initiator;
}

wire::PairRoleV1 PairKnownScheduler::peer_role() const noexcept
{
    return local_is_initiator() ? wire::PairRoleV1::Responder
                                : wire::PairRoleV1::Initiator;
}

const std::array<std::uint8_t, 65>& PairKnownScheduler::local_public() const noexcept
{
    return local_is_initiator() ? start_.initiator_public_key
                                : start_.responder_public_key;
}

const std::array<std::uint8_t, 65>& PairKnownScheduler::peer_public() const noexcept
{
    return local_is_initiator() ? start_.responder_public_key
                                : start_.initiator_public_key;
}

fly_session_op_token_v2 PairKnownScheduler::token(
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

bool PairKnownScheduler::begin(const PairKnownStartV1& start)
{
    if (begun_ || failed_ || start.generation == 0 ||
        start.first_operation_id == 0 ||
        start.first_operation_id > (std::numeric_limits<std::uint64_t>::max)() - 12 ||
        !nonzero(start.engine_instance_id.data(), start.engine_instance_id.size()) ||
        !nonzero(start.link_id.data(), start.link_id.size()) ||
        !nonzero(start.transcript_hash.data(), start.transcript_hash.size()) ||
        (start.local_role != wire::PairRoleV1::Initiator &&
         start.local_role != wire::PairRoleV1::Responder) ||
        !wire::validate_p256_uncompressed_point(start.initiator_public_key.data()) ||
        !wire::validate_p256_uncompressed_point(start.responder_public_key.data()) ||
        start.local_identity_key == 0 || start.gatt_i2r_key == 0 ||
        start.gatt_r2i_key == 0 || start.control_i2r_key == 0 ||
        start.control_r2i_key == 0)
        return false;
    start_ = start;
    next_operation_id_ = start.first_operation_id;
    begun_ = true;
    if (local_is_initiator())
        return prepare_local_status() == FLY_SESSION_V2_OK;
    stage_ = Stage::WaitPeerStatus;
    return true;
}

fly_session_result_v2 PairKnownScheduler::issue(
    PairKnownEffect effect, std::uint32_t payload_kind, Stage stage)
{
    const auto expected = operations_.expect(effect.token, payload_kind);
    if (expected != FLY_SESSION_V2_OK) return reject(expected);
    pending_ = std::move(effect);
    stage_ = stage;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 PairKnownScheduler::prepare_local_status()
{
    try
    {
        std::array<std::uint8_t, wire::kKnownStatusInnerSizeV1> inner{};
        std::array<std::uint8_t, 32> empty{};
        if (wire::encode_known_status_inner_v1(
                start_.transcript_hash, start_.local_role, start_.local_known,
                local_public(), peer_public(), empty, validate_point, nullptr,
                &inner) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        PairKnownEffect effect{};
        effect.kind = PairKnownEffectKind::Hmac;
        effect.token = token(next_operation_id_++);
        effect.resource = local_is_initiator() ? start_.gatt_i2r_key
                                              : start_.gatt_r2i_key;
        if (wire::build_known_status_hmac_input_v1(
                inner.data(), wire::kKnownStatusBodySizeV1, &effect.input) !=
            wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
                     Stage::HmacLocalStatus);
    }
    catch (const std::bad_alloc&) { return reject(FLY_SESSION_V2_OUT_OF_MEMORY); }
}

fly_session_result_v2 PairKnownScheduler::prepare_aead(bool status, bool seal)
{
    try
    {
        const auto sender = seal ? start_.local_role : peer_role();
        const std::uint8_t type = status ? 21 : 17;
        const std::uint32_t counter = status ? 2 : 3;
        PairKnownEffect effect{};
        effect.kind = seal ? PairKnownEffectKind::AeadSeal
                           : PairKnownEffectKind::AeadOpen;
        effect.token = token(next_operation_id_++);
        effect.resource = sender == wire::PairRoleV1::Initiator
            ? start_.control_i2r_key : start_.control_r2i_key;
        std::array<std::uint8_t, wire::kPairSecureNonceSizeV1> nonce{};
        std::array<std::uint8_t, wire::kPairSecureAadSizeV1> aad{};
        if (wire::build_pair_secure_nonce_v1(sender, counter, &nonce) !=
                wire::Status::Ok ||
            wire::build_pair_secure_aad_v1(type, sender, start_.transcript_hash,
                                           counter, &aad) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        effect.nonce.assign(nonce.begin(), nonce.end());
        effect.aad.assign(aad.begin(), aad.end());
        if (seal)
        {
            if (status)
            {
                std::array<std::uint8_t, wire::kKnownStatusInnerSizeV1> inner{};
                if (wire::encode_known_status_inner_v1(
                        start_.transcript_hash, start_.local_role,
                        start_.local_known, local_public(), peer_public(),
                        local_status_tag_, validate_point, nullptr, &inner) !=
                    wire::Status::Ok)
                    return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
                effect.input.assign(inner.begin(), inner.end());
            }
            else
            {
                std::array<std::uint8_t, wire::kKnownBranchInnerSizeV1> inner{};
                const auto kind = known_path_
                    ? wire::KnownBranchKindV1::Verified
                    : wire::KnownBranchKindV1::SasFallback;
                if (wire::encode_known_branch_inner_v1(
                        start_.transcript_hash, start_.local_role, kind,
                        local_public(), peer_public(), local_branch_proof_,
                        validate_point, nullptr, &inner) != wire::Status::Ok)
                    return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
                effect.input.assign(inner.begin(), inner.end());
            }
        }
        else
        {
            if (!peer_envelope_) return reject(FLY_SESSION_V2_INVALID_STATE);
            effect.input = peer_envelope_->ciphertext_and_tag;
        }
        return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
                     status ? (seal ? Stage::SealLocalStatus : Stage::OpenPeerStatus)
                            : (seal ? Stage::SealLocalBranch : Stage::OpenPeerBranch));
    }
    catch (const std::bad_alloc&) { return reject(FLY_SESSION_V2_OUT_OF_MEMORY); }
}

fly_session_result_v2 PairKnownScheduler::prepare_peer_status_hmac()
{
    PairKnownEffect effect{};
    effect.kind = PairKnownEffectKind::Hmac;
    effect.token = token(next_operation_id_++);
    effect.resource = local_is_initiator() ? start_.gatt_r2i_key
                                          : start_.gatt_i2r_key;
    effect.input = peer_status_hmac_input_;
    return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
                 Stage::HmacPeerStatus);
}

fly_session_result_v2 PairKnownScheduler::prepare_local_branch()
{
    known_path_ = start_.local_known && peer_known_;
    local_branch_proof_.fill(0);
    if (!known_path_) return prepare_aead(false, true);
    std::array<std::uint8_t, 64> placeholder{};
    placeholder[31] = 1;
    placeholder[63] = 1;
    std::array<std::uint8_t, wire::kKnownBranchInnerSizeV1> inner{};
    std::array<std::uint8_t, 32> digest{};
    if (wire::encode_known_branch_inner_v1(
            start_.transcript_hash, start_.local_role,
            wire::KnownBranchKindV1::Verified, local_public(), peer_public(),
            placeholder, validate_point, nullptr, &inner) != wire::Status::Ok ||
        wire::known_branch_digest_v1(
            inner.data(), wire::kKnownBranchBodySizeV1, &digest) != wire::Status::Ok)
        return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    PairKnownEffect effect{};
    effect.kind = PairKnownEffectKind::Sign;
    effect.token = token(next_operation_id_++);
    effect.resource = start_.local_identity_key;
    effect.key_purpose = FLY_SESSION_KEY_DEVICE_IDENTITY_V2;
    effect.domain.assign(reinterpret_cast<const std::uint8_t*>(kBranchDomain),
                         reinterpret_cast<const std::uint8_t*>(kBranchDomain) +
                             sizeof(kBranchDomain) - 1);
    effect.digest = digest;
    return issue(std::move(effect), FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2,
                 Stage::SignLocalBranch);
}

fly_session_result_v2 PairKnownScheduler::prepare_peer_branch_verify()
{
    std::array<std::uint8_t, wire::kKnownBranchInnerSizeV1> inner{};
    const auto kind = known_path_ ? wire::KnownBranchKindV1::Verified
                                  : wire::KnownBranchKindV1::SasFallback;
    if (wire::encode_known_branch_inner_v1(
            start_.transcript_hash, peer_role(), kind, peer_public(),
            local_public(), peer_branch_proof_, validate_point, nullptr,
            &inner) != wire::Status::Ok)
        return reject(FLY_SESSION_V2_AUTH_FAILED);
    if (!known_path_)
    {
        peer_branch_verified_ = true;
        if (local_is_initiator())
        {
            ready_ = local_branch_sent_;
            stage_ = ready_ ? Stage::Ready : Stage::WaitLocalBranchSent;
            return FLY_SESSION_V2_OK;
        }
        return prepare_local_branch();
    }
    PairKnownEffect effect{};
    effect.kind = PairKnownEffectKind::Verify;
    effect.token = token(next_operation_id_++);
    effect.public_key.assign(peer_public().begin(), peer_public().end());
    effect.signature.assign(peer_branch_proof_.begin(), peer_branch_proof_.end());
    effect.domain.assign(reinterpret_cast<const std::uint8_t*>(kBranchDomain),
                         reinterpret_cast<const std::uint8_t*>(kBranchDomain) +
                             sizeof(kBranchDomain) - 1);
    if (wire::known_branch_digest_v1(
            inner.data(), wire::kKnownBranchBodySizeV1, &effect.digest) !=
        wire::Status::Ok)
        return reject(FLY_SESSION_V2_AUTH_FAILED);
    return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2,
                 Stage::VerifyPeerBranch);
}

fly_session_result_v2 PairKnownScheduler::read_buffer(
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
    return fly_session_buffer_read_v2(completion.payload.buffer, 0, destination,
                                      &written) == FLY_SESSION_V2_OK &&
                   written == out.size()
        ? FLY_SESSION_V2_OK : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 PairKnownScheduler::complete(
    const fly_session_port_event_v2& event)
{
    if (!pending_ || failed_) return FLY_SESSION_V2_INVALID_STATE;
    const auto completed = stage_;
    ProviderOperationCompletion completion{};
    const auto accepted = operations_.accept(event, completion);
    if (accepted != FLY_SESSION_V2_OK) return accepted;
    if (completion.result != FLY_SESSION_V2_OK) return reject(completion.result);
    pending_.reset();
    std::vector<std::uint8_t> output;
    if (completed != Stage::VerifyPeerBranch)
    {
        const auto read = read_buffer(completion, output);
        if (read != FLY_SESSION_V2_OK) return reject(read);
    }
    if (completed == Stage::HmacLocalStatus)
    {
        if (output.size() != 32) return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::copy(output.begin(), output.end(), local_status_tag_.begin());
        return prepare_aead(true, true);
    }
    if (completed == Stage::SealLocalStatus || completed == Stage::SealLocalBranch)
    {
        const bool status = completed == Stage::SealLocalStatus;
        const auto expected = (status ? wire::kKnownStatusInnerSizeV1
                                      : wire::kKnownBranchInnerSizeV1) + 16;
        if (output.size() != expected)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::vector<std::uint8_t> envelope;
        const std::uint8_t type = status ? 21 : 17;
        const std::uint32_t counter = status ? 2 : 3;
        if (wire::encode_pair_secure_envelope_v1(
                type, counter, output.data(), output.size(), &envelope) !=
            wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        local_envelope_ = std::move(envelope);
        local_message_type_ = type;
        stage_ = status ? Stage::WaitLocalStatusSent : Stage::WaitLocalBranchSent;
        return FLY_SESSION_V2_OK;
    }
    if (completed == Stage::OpenPeerStatus)
    {
        if (output.size() != wire::kKnownStatusInnerSizeV1)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        wire::KnownStatusInnerV1 decoded{};
        if (wire::decode_known_status_inner_v1(
                output.data(), output.size(), start_.transcript_hash,
                peer_role(), peer_public(), local_public(), validate_point,
                nullptr, &decoded) != wire::Status::Ok ||
            wire::build_known_status_hmac_input_v1(
                output.data(), wire::kKnownStatusBodySizeV1,
                &peer_status_hmac_input_) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        peer_status_tag_ = decoded.tag;
        peer_known_ = decoded.known;
        return prepare_peer_status_hmac();
    }
    if (completed == Stage::HmacPeerStatus)
    {
        if (output.size() != peer_status_tag_.size())
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        std::uint8_t difference = 0;
        for (std::size_t i = 0; i < output.size(); ++i)
            difference = static_cast<std::uint8_t>(
                difference | (output[i] ^ peer_status_tag_[i]));
        if (difference != 0) return reject(FLY_SESSION_V2_AUTH_FAILED);
        peer_status_verified_ = true;
        known_path_ = start_.local_known && peer_known_;
        peer_envelope_.reset();
        if (local_is_initiator()) return prepare_local_branch();
        return prepare_local_status();
    }
    if (completed == Stage::SignLocalBranch)
    {
        if (output.size() != local_branch_proof_.size())
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::copy(output.begin(), output.end(), local_branch_proof_.begin());
        return prepare_aead(false, true);
    }
    if (completed == Stage::OpenPeerBranch)
    {
        if (output.size() != wire::kKnownBranchInnerSizeV1)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        wire::KnownBranchInnerV1 decoded{};
        const auto kind = known_path_ ? wire::KnownBranchKindV1::Verified
                                      : wire::KnownBranchKindV1::SasFallback;
        if (wire::decode_known_branch_inner_v1(
                output.data(), output.size(), start_.transcript_hash,
                peer_role(), kind, peer_public(), local_public(), validate_point,
                nullptr, &decoded) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        peer_branch_proof_ = decoded.proof;
        peer_envelope_.reset();
        return prepare_peer_branch_verify();
    }
    if (completed == Stage::VerifyPeerBranch)
    {
        peer_branch_verified_ = true;
        if (local_is_initiator())
        {
            ready_ = local_branch_sent_;
            stage_ = ready_ ? Stage::Ready : Stage::WaitLocalBranchSent;
            return FLY_SESSION_V2_OK;
        }
        return prepare_local_branch();
    }
    return reject(FLY_SESSION_V2_INVALID_STATE);
}

fly_session_result_v2 PairKnownScheduler::accept_peer_envelope(
    std::uint8_t logical_type, const std::uint8_t* body, std::size_t size,
    const std::array<std::uint8_t, 32>& logical_hash)
{
    const bool status = logical_type == 21;
    if (!begun_ || failed_ || peer_envelope_ ||
        (status ? stage_ != Stage::WaitPeerStatus
                : stage_ != Stage::WaitPeerBranch) ||
        (!status && logical_type != 17) ||
        !nonzero(logical_hash.data(), logical_hash.size()))
        return FLY_SESSION_V2_INVALID_STATE;
    wire::PairSecureEnvelopeV1 envelope{};
    const std::uint32_t counter = status ? 2 : 3;
    if (wire::decode_pair_secure_envelope_v1(
            logical_type, body, size, &envelope) != wire::Status::Ok ||
        envelope.message_counter != counter)
        return reject(FLY_SESSION_V2_AUTH_FAILED);
    peer_envelope_ = std::move(envelope);
    peer_logical_hash_ = logical_hash;
    return prepare_aead(status, false) == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_ACCEPTED : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 PairKnownScheduler::mark_local_sent(
    const std::array<std::uint8_t, 32>& logical_hash) noexcept
{
    if (!local_envelope_ || !nonzero(logical_hash.data(), logical_hash.size()))
        return reject(FLY_SESSION_V2_INVALID_STATE);
    local_logical_hash_ = logical_hash;
    local_envelope_.reset();
    if (local_message_type_ == 21 && stage_ == Stage::WaitLocalStatusSent)
    {
        local_status_sent_ = true;
        stage_ = local_is_initiator() ? Stage::WaitPeerStatus
                                      : Stage::WaitPeerBranch;
        return FLY_SESSION_V2_OK;
    }
    if (local_message_type_ == 17 && stage_ == Stage::WaitLocalBranchSent)
    {
        local_branch_sent_ = true;
        if (local_is_initiator()) stage_ = Stage::WaitPeerBranch;
        else
        {
            ready_ = peer_branch_verified_;
            stage_ = ready_ ? Stage::Ready : Stage::Failed;
        }
        return stage_ == Stage::Failed
            ? reject(FLY_SESSION_V2_INVALID_STATE) : FLY_SESSION_V2_OK;
    }
    return reject(FLY_SESSION_V2_INVALID_STATE);
}

fly_session_result_v2 PairKnownScheduler::cancel_pending() noexcept
{
    if (!pending_) return FLY_SESSION_V2_INVALID_STATE;
    const auto result = operations_.cancel(pending_->token);
    if (result == FLY_SESSION_V2_OK) reject(FLY_SESSION_V2_CANCELLED);
    return result;
}

fly_session_result_v2 PairKnownScheduler::reject(
    fly_session_result_v2 result) noexcept
{
    pending_.reset();
    local_envelope_.reset();
    failed_ = true;
    stage_ = Stage::Failed;
    return result == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_CONTRACT_VIOLATION : result;
}

} // namespace flynes::session
