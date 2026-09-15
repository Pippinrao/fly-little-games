#include "endpoint_offer_scheduler.hpp"

#include "../wire/gatt_fragment.hpp"
#include "../wire/sha256.hpp"

#include <algorithm>
#include <limits>
#include <new>

namespace flynes::session {
namespace {
bool nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return bytes && std::any_of(bytes, bytes + size,
        [](std::uint8_t value) { return value != 0; });
}
wire::PairRoleV1 plan_role(std::uint8_t value) noexcept
{
    return value == 1 ? wire::PairRoleV1::Initiator
                      : wire::PairRoleV1::Responder;
}
} // namespace

fly_session_op_token_v2 EndpointOfferScheduler::token(
    std::uint64_t id) const noexcept
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
    value.operation_id = id;
    return value;
}

bool EndpointOfferScheduler::begin(const EndpointOfferStartV1& start)
{
    if (begun_ || failed_ || !bearer_ || !bearer_->ready() ||
        start.generation == 0 || start.first_operation_id == 0 ||
        start.first_operation_id > (std::numeric_limits<std::uint64_t>::max)() - 10 ||
        !nonzero(start.engine_instance_id.data(), start.engine_instance_id.size()) ||
        !nonzero(start.link_id.data(), start.link_id.size()) ||
        !nonzero(start.transcript_hash.data(), start.transcript_hash.size()) ||
        !nonzero(start.session_id.data(), start.session_id.size()) ||
        !nonzero(start.listener_spki_hash.data(), start.listener_spki_hash.size()) ||
        start.ecdh_secret == 0 || start.bearer_path == 0 ||
        (start.local_role != wire::PairRoleV1::Initiator &&
         start.local_role != wire::PairRoleV1::Responder) ||
        (start.selected_plan[1] != 1 && start.selected_plan[1] != 2) ||
        (start.selected_plan[2] != 1 && start.selected_plan[2] != 2) ||
        start.selected_plan[4] < 1 || start.selected_plan[4] > 3 ||
        bearer_->owned_resources().bearer != start.bearer_path)
        return false;
    start_ = start;
    next_operation_id_ = start.first_operation_id;
    local_listener_ = start.selected_plan[2] ==
        static_cast<std::uint8_t>(start.local_role);
    begun_ = true;
    return derive(true) == FLY_SESSION_V2_OK;
}

fly_session_result_v2 EndpointOfferScheduler::issue(
    EndpointOfferEffect effect, std::uint32_t payload, Stage stage)
{
    const auto result = operations_.expect(effect.token, payload);
    if (result != FLY_SESSION_V2_OK) return reject(result);
    pending_ = std::move(effect);
    stage_ = stage;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 EndpointOfferScheduler::derive(bool i2r)
{
    try
    {
        static constexpr char salt_label[] = "flynes-pair-v1";
        static constexpr char i2r_label[] = "flynes-endpoint-offer-i2r-v1";
        static constexpr char r2i_label[] = "flynes-endpoint-offer-r2i-v1";
        std::array<std::uint8_t, sizeof(salt_label) - 1 + 32> salt_input{};
        std::copy_n(reinterpret_cast<const std::uint8_t*>(salt_label),
                    sizeof(salt_label) - 1, salt_input.begin());
        std::copy(start_.transcript_hash.begin(), start_.transcript_hash.end(),
                  salt_input.begin() + sizeof(salt_label) - 1);
        const auto salt = wire::sha256(salt_input.data(), salt_input.size());
        const auto* label = i2r ? i2r_label : r2i_label;
        const auto size = i2r ? sizeof(i2r_label) - 1 : sizeof(r2i_label) - 1;
        EndpointOfferEffect effect{};
        effect.kind = EndpointOfferEffectKind::DeriveKey;
        effect.token = token(next_operation_id_++);
        effect.resource = start_.ecdh_secret;
        effect.salt.assign(salt.begin(), salt.end());
        effect.info.assign(reinterpret_cast<const std::uint8_t*>(label),
                           reinterpret_cast<const std::uint8_t*>(label) + size);
        return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2,
                     i2r ? Stage::DeriveI2r : Stage::DeriveR2i);
    }
    catch (const std::bad_alloc&) { return reject(FLY_SESSION_V2_OUT_OF_MEMORY); }
}

fly_session_result_v2 EndpointOfferScheduler::random(
    std::uint32_t size, Stage stage)
{
    EndpointOfferEffect effect{};
    effect.kind = EndpointOfferEffectKind::Random;
    effect.token = token(next_operation_id_++);
    effect.byte_count = size;
    return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2, stage);
}

fly_session_result_v2 EndpointOfferScheduler::resolve()
{
    try
    {
        EndpointOfferEffect effect{};
        effect.kind = EndpointOfferEffectKind::ResolveEndpoint;
        effect.token = token(next_operation_id_++);
        effect.resource = start_.bearer_path;
        effect.listener_token = listener_token_;
        return issue(std::move(effect), FLY_SESSION_PROVIDER_BEARER_ENDPOINT_V2,
                     Stage::Resolve);
    }
    catch (const std::bad_alloc&) { return reject(FLY_SESSION_V2_OUT_OF_MEMORY); }
}

fly_session_result_v2 EndpointOfferScheduler::seal()
{
    try
    {
        if (endpoint_.size() != 18) return reject(FLY_SESSION_V2_INVALID_STATE);
        std::array<std::uint8_t, 16> endpoint_value{};
        std::copy_n(endpoint_.begin(), endpoint_value.size(), endpoint_value.begin());
        const auto port = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(endpoint_[16]) << 8u) | endpoint_[17]);
        const auto creator = plan_role(start_.selected_plan[1]);
        const auto listener = plan_role(start_.selected_plan[2]);
        const auto binding = wire::initial_bearer_binding_hash_v1(
            start_.transcript_hash, creator,
            bearer_->credential_logical_hash());
        std::array<std::uint8_t, wire::kEndpointOfferPlaintextSizeV1> plaintext{};
        if (wire::encode_initial_endpoint_offer_plaintext_v1(
                start_.transcript_hash, start_.session_id, listener,
                static_cast<wire::EndpointKindV1>(start_.selected_plan[4]),
                endpoint_value, port, binding, start_.listener_spki_hash,
                &plaintext) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::array<std::uint8_t, wire::kEndpointOfferAadSizeV1> aad{};
        if (wire::build_endpoint_offer_aad_v1(
                listener, wire::EndpointBindingKindV1::Initial,
                start_.transcript_hash, start_.session_id, {}, &aad) !=
                wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        EndpointOfferEffect effect{};
        effect.kind = EndpointOfferEffectKind::AeadSeal;
        effect.token = token(next_operation_id_++);
        effect.resource = listener == wire::PairRoleV1::Initiator
            ? i2r_key_ : r2i_key_;
        effect.nonce.assign(nonce_.begin(), nonce_.end());
        effect.aad.assign(aad.begin(), aad.end());
        effect.input.assign(plaintext.begin(), plaintext.end());
        return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
                     Stage::Seal);
    }
    catch (const std::bad_alloc&) { return reject(FLY_SESSION_V2_OUT_OF_MEMORY); }
}

fly_session_result_v2 EndpointOfferScheduler::persist()
{
    if (peer_exact_.empty()) return reject(FLY_SESSION_V2_INVALID_STATE);
    EndpointOfferEffect effect{};
    effect.kind = EndpointOfferEffectKind::Persist;
    effect.token = token(next_operation_id_++);
    try { effect.input = peer_exact_; }
    catch (const std::bad_alloc&) { return reject(FLY_SESSION_V2_OUT_OF_MEMORY); }
    return issue(std::move(effect), FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2,
                 Stage::Persist);
}

fly_session_result_v2 EndpointOfferScheduler::read_buffer(
    const ProviderOperationCompletion& completion,
    std::vector<std::uint8_t>& out) const
{
    if (!completion.payload.buffer) return FLY_SESSION_V2_CONTRACT_VIOLATION;
    std::uint64_t size = 0;
    if (fly_session_buffer_size_v2(completion.payload.buffer, &size) !=
            FLY_SESSION_V2_OK || size == 0 || size > 4096)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    try { out.assign(static_cast<std::size_t>(size), 0); }
    catch (const std::bad_alloc&) { return FLY_SESSION_V2_OUT_OF_MEMORY; }
    std::uint64_t written = 0;
    const fly_session_write_bytes_v2 destination{out.data(), out.size()};
    return fly_session_buffer_read_v2(completion.payload.buffer, 0, destination,
                                      &written) == FLY_SESSION_V2_OK &&
                   written == out.size()
        ? FLY_SESSION_V2_OK : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 EndpointOfferScheduler::complete(
    const fly_session_port_event_v2& event)
{
    if (!pending_ || failed_) return FLY_SESSION_V2_INVALID_STATE;
    const auto completed = stage_;
    ProviderOperationCompletion completion{};
    const auto accepted = operations_.accept(event, completion);
    if (accepted != FLY_SESSION_V2_OK) return accepted;
    if (completion.result != FLY_SESSION_V2_OK) return reject(completion.result);
    pending_.reset();
    if (completed == Stage::DeriveI2r || completed == Stage::DeriveR2i)
    {
        if (completion.payload.resource == 0)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        if (completed == Stage::DeriveI2r)
        {
            i2r_key_ = completion.payload.resource;
            return derive(false);
        }
        r2i_key_ = completion.payload.resource;
        if (!local_listener_) { stage_ = Stage::WaitPeer; return FLY_SESSION_V2_OK; }
        return start_.selected_plan[4] ==
                   static_cast<std::uint8_t>(wire::EndpointKindV1::AppleBonjourP2p)
            ? random(16, Stage::RandomToken) : resolve();
    }
    std::vector<std::uint8_t> bytes;
    if (completed != Stage::Persist)
    {
        const auto read = read_buffer(completion, bytes);
        if (read != FLY_SESSION_V2_OK) return reject(read);
    }
    if (completed == Stage::RandomToken)
    {
        if (bytes.size() != 16 || !nonzero(bytes.data(), bytes.size()))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        listener_token_ = std::move(bytes);
        return resolve();
    }
    if (completed == Stage::Resolve)
    {
        if (bytes.size() != 18)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        endpoint_ = std::move(bytes);
        return random(12, Stage::RandomNonce);
    }
    if (completed == Stage::RandomNonce)
    {
        if (bytes.size() != nonce_.size() || !nonzero(bytes.data(), bytes.size()))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::copy(bytes.begin(), bytes.end(), nonce_.begin());
        return seal();
    }
    if (completed == Stage::Seal)
    {
        if (bytes.size() != wire::kEndpointOfferPlaintextSizeV1 + 16)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::array<std::uint8_t, wire::kEndpointOfferEnvelopeSizeV1> envelope{};
        std::vector<std::uint8_t> logical;
        if (wire::encode_endpoint_offer_envelope_v1(
                nonce_, bytes.data(), bytes.size(), &envelope) != wire::Status::Ok ||
            wire::encode_gatt_logical_message(22, envelope.data(), envelope.size(),
                                              &logical) !=
                wire::GattFragmentResult::Accepted)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        peer_exact_ = std::move(logical);
        return persist();
    }
    if (completed == Stage::Open)
    {
        if (bytes.size() != wire::kEndpointOfferPlaintextSizeV1)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        const auto listener = plan_role(start_.selected_plan[2]);
        const auto creator = plan_role(start_.selected_plan[1]);
        const auto binding = wire::initial_bearer_binding_hash_v1(
            start_.transcript_hash, creator,
            bearer_->credential_logical_hash());
        wire::EndpointOfferPlaintextV1 decoded{};
        if (wire::decode_initial_endpoint_offer_plaintext_v1(
                bytes.data(), bytes.size(), start_.transcript_hash,
                start_.session_id, listener,
                static_cast<wire::EndpointKindV1>(start_.selected_plan[4]),
                binding, start_.listener_spki_hash, &decoded) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        endpoint_.assign(decoded.endpoint_value.begin(), decoded.endpoint_value.end());
        endpoint_.push_back(static_cast<std::uint8_t>(decoded.port >> 8u));
        endpoint_.push_back(static_cast<std::uint8_t>(decoded.port));
        return persist();
    }
    if (completed == Stage::Persist)
    {
        if (local_listener_)
        {
            local_send_ = peer_exact_;
            stage_ = Stage::WaitSend;
        }
        else
        {
            ready_ = true;
            stage_ = Stage::Ready;
        }
        return FLY_SESSION_V2_OK;
    }
    return reject(FLY_SESSION_V2_INVALID_STATE);
}

fly_session_result_v2 EndpointOfferScheduler::accept_peer_logical(
    const std::uint8_t* bytes, std::size_t size)
{
    if (!begun_ || failed_ || local_listener_ || stage_ != Stage::WaitPeer ||
        !bytes) return FLY_SESSION_V2_INVALID_STATE;
    wire::GattLogicalMessageView logical{};
    wire::EndpointOfferEnvelopeV1 envelope{};
    if (wire::decode_gatt_logical_message(bytes, size, 22, &logical) !=
            wire::GattFragmentResult::Complete ||
        wire::decode_endpoint_offer_envelope_v1(
            logical.body, logical.body_size, &envelope) != wire::Status::Ok)
        return reject(FLY_SESSION_V2_AUTH_FAILED);
    try { peer_exact_.assign(bytes, bytes + size); }
    catch (const std::bad_alloc&) { return reject(FLY_SESSION_V2_OUT_OF_MEMORY); }
    const auto listener = plan_role(start_.selected_plan[2]);
    std::array<std::uint8_t, wire::kEndpointOfferAadSizeV1> aad{};
    if (wire::build_endpoint_offer_aad_v1(
            listener, wire::EndpointBindingKindV1::Initial,
            start_.transcript_hash, start_.session_id, {}, &aad) != wire::Status::Ok)
        return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    EndpointOfferEffect effect{};
    effect.kind = EndpointOfferEffectKind::AeadOpen;
    effect.token = token(next_operation_id_++);
    effect.resource = listener == wire::PairRoleV1::Initiator ? i2r_key_ : r2i_key_;
    effect.nonce.assign(envelope.public_nonce.begin(), envelope.public_nonce.end());
    effect.aad.assign(aad.begin(), aad.end());
    effect.input = std::move(envelope.ciphertext_and_tag);
    return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
                 Stage::Open) == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_ACCEPTED : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 EndpointOfferScheduler::mark_local_sent() noexcept
{
    if (!local_listener_ || !local_send_ || stage_ != Stage::WaitSend || failed_)
        return reject(FLY_SESSION_V2_INVALID_STATE);
    local_send_.reset();
    ready_ = true;
    stage_ = Stage::Ready;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 EndpointOfferScheduler::cancel_pending() noexcept
{
    if (!pending_) return FLY_SESSION_V2_INVALID_STATE;
    const auto result = operations_.cancel(pending_->token);
    if (result == FLY_SESSION_V2_OK) reject(FLY_SESSION_V2_CANCELLED);
    return result;
}

fly_session_result_v2 EndpointOfferScheduler::reject(
    fly_session_result_v2 result) noexcept
{
    pending_.reset();
    failed_ = true;
    stage_ = Stage::Failed;
    return result == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_CONTRACT_VIOLATION : result;
}

} // namespace flynes::session
