#include "session_signing_scheduler.hpp"

#include "../wire/p256_point.hpp"
#include "../wire/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace flynes::session {
namespace {

void put_be32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

} // namespace

bool SessionSigningScheduler::nonzero(const std::uint8_t* bytes,
                                      std::size_t size) noexcept
{
    return bytes && std::any_of(bytes, bytes + size,
        [](std::uint8_t value) { return value != 0; });
}

fly_session_op_token_v2 SessionSigningScheduler::token(
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

bool SessionSigningScheduler::begin(const SessionSigningStartV1& start)
{
    if (begun_ || failed_ || start.generation == 0 ||
        start.first_operation_id == 0 ||
        start.first_operation_id >
            (std::numeric_limits<std::uint64_t>::max)() - 5 ||
        start.identity_key == 0 ||
        !nonzero(start.engine_instance_id.data(), start.engine_instance_id.size()) ||
        !nonzero(start.link_id.data(), start.link_id.size()) ||
        !nonzero(start.pair_transcript_hash.data(),
                 start.pair_transcript_hash.size()) ||
        !nonzero(start.session_id.data(), start.session_id.size()) ||
        (start.local_role != wire::PairRoleV1::Initiator &&
         start.local_role != wire::PairRoleV1::Responder) ||
        !wire::validate_p256_uncompressed_point(
            start.identity_public_key.data()))
    {
        failed_ = true;
        stage_ = Stage::Failed;
        return false;
    }
    start_ = start;
    begun_ = true;
    next_operation_id_ = start.first_operation_id;
    return prepare(Stage::GenerateKey) == FLY_SESSION_V2_OK;
}

fly_session_result_v2 SessionSigningScheduler::prepare(Stage stage)
{
    try
    {
        SessionSigningEffect effect{};
        effect.token = token(next_operation_id_++);
        std::uint32_t expected = 0;
        if (stage == Stage::GenerateKey)
        {
            effect.kind = SessionSigningEffectKind::GenerateKey;
            effect.key_purpose = FLY_SESSION_KEY_SESSION_SIGNING_V2;
            effect.scope_binding.assign(start_.pair_transcript_hash.begin(),
                                        start_.pair_transcript_hash.end());
            effect.scope_binding.insert(effect.scope_binding.end(),
                                        start_.session_id.begin(),
                                        start_.session_id.end());
            effect.scope_binding.push_back(
                static_cast<std::uint8_t>(start_.local_role));
            expected = FLY_SESSION_PROVIDER_KEY_HANDLE_V2;
        }
        else if (stage == Stage::ReadPublicKey)
        {
            effect.kind = SessionSigningEffectKind::ReadPublicKey;
            effect.resource = material_.key;
            effect.public_key_encoding =
                FLY_SESSION_PUBLIC_KEY_X963_UNCOMPRESSED_V2;
            expected = FLY_SESSION_PROVIDER_KEY_PUBLIC_V2;
        }
        else if (stage == Stage::SignBinding)
        {
            effect.kind = SessionSigningEffectKind::SignBinding;
            effect.key_purpose = FLY_SESSION_KEY_DEVICE_IDENTITY_V2;
            effect.resource = start_.identity_key;
            static constexpr char domain[] =
                "flynes-session-signing-key-binding-v1";
            effect.domain.assign(
                reinterpret_cast<const std::uint8_t*>(domain),
                reinterpret_cast<const std::uint8_t*>(domain) +
                    sizeof(domain) - 1);
            effect.digest = binding_digest_;
            expected = FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2;
        }
        else if (stage == Stage::PersistBinding)
        {
            effect.kind = SessionSigningEffectKind::PersistBinding;
            static constexpr char name_space[] = "flynes/session-signing/v1";
            effect.name_space.assign(
                reinterpret_cast<const std::uint8_t*>(name_space),
                reinterpret_cast<const std::uint8_t*>(name_space) +
                    sizeof(name_space) - 1);
            effect.record_key.assign(start_.session_id.begin(),
                                     start_.session_id.end());
            effect.record_key.push_back(
                static_cast<std::uint8_t>(start_.local_role));
            effect.expected_revision = 0;
            put_be32(effect.value,
                     static_cast<std::uint32_t>(durable_key_ref_.size()));
            effect.value.insert(effect.value.end(), durable_key_ref_.begin(),
                                durable_key_ref_.end());
            effect.value.insert(effect.value.end(), material_.binding.begin(),
                                material_.binding.end());
            effect.value.insert(effect.value.end(),
                                material_.binding_hash.begin(),
                                material_.binding_hash.end());
            expected = FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2;
        }
        else if (stage == Stage::PersistBindingObject)
        {
            effect.kind = SessionSigningEffectKind::PersistBindingObject;
            effect.object_kind = wire::kSessionSigningBindingObjectKindV1;
            effect.expected_hash = material_.binding_hash;
            effect.value.assign(material_.binding.begin(),
                                material_.binding.end());
            expected = FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2;
        }
        else return reject(FLY_SESSION_V2_INVALID_STATE);
        const auto registered = operations_.expect(effect.token, expected);
        if (registered != FLY_SESSION_V2_OK) return reject(registered);
        stage_ = stage;
        pending_ = std::move(effect);
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

std::optional<SessionSigningEffect> SessionSigningScheduler::poll_effect() const
{
    return pending_;
}

fly_session_result_v2 SessionSigningScheduler::read_buffer(
    const ParsedProviderEvent& event, std::vector<std::uint8_t>& out) const
{
    std::uint64_t size = 0;
    if (!event.buffer ||
        fly_session_buffer_size_v2(event.buffer, &size) != FLY_SESSION_V2_OK ||
        size == 0 || size > 4096)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    try { out.assign(static_cast<std::size_t>(size), 0); }
    catch (const std::bad_alloc&) { return FLY_SESSION_V2_OUT_OF_MEMORY; }
    std::uint64_t written = 0;
    const fly_session_write_bytes_v2 destination{out.data(), size};
    return fly_session_buffer_read_v2(
               event.buffer, 0, destination, &written) == FLY_SESSION_V2_OK &&
           written == size
        ? FLY_SESSION_V2_OK : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 SessionSigningScheduler::complete(
    const fly_session_port_event_v2& event)
{
    if (!pending_ || failed_ || ready_) return FLY_SESSION_V2_INVALID_STATE;
    ProviderOperationCompletion completion{};
    const auto accepted = operations_.accept(event, completion);
    if (accepted != FLY_SESSION_V2_OK) return accepted;
    if (completion.result != FLY_SESSION_V2_OK)
        return reject(completion.result);
    std::vector<std::uint8_t> bytes;
    if (stage_ == Stage::GenerateKey)
    {
        if (completion.payload.resource == 0 ||
            read_buffer(completion.payload, bytes) != FLY_SESSION_V2_OK ||
            !nonzero(completion.payload.hash.data(),
                     completion.payload.hash.size()))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        material_.key = completion.payload.resource;
        durable_key_ref_ = std::move(bytes);
        expected_public_hash_ = completion.payload.hash;
        pending_.reset();
        return prepare(Stage::ReadPublicKey);
    }
    if (stage_ == Stage::ReadPublicKey)
    {
        if (read_buffer(completion.payload, bytes) != FLY_SESSION_V2_OK ||
            bytes.size() != material_.public_key.size() ||
            !wire::validate_p256_uncompressed_point(bytes.data()) ||
            std::equal(bytes.begin(), bytes.end(),
                       start_.identity_public_key.begin()) ||
            wire::sha256(bytes.data(), bytes.size()) != expected_public_hash_)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        std::copy(bytes.begin(), bytes.end(), material_.public_key.begin());
        if (wire::build_session_signing_binding_pretag_v1(
                start_.pair_transcript_hash, start_.session_id,
                start_.local_role, start_.identity_public_key,
                material_.public_key,
                wire::validate_p256_uncompressed_point_callback, nullptr,
                &pretag_, &binding_digest_) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        pending_.reset();
        return prepare(Stage::SignBinding);
    }
    if (stage_ == Stage::SignBinding)
    {
        if (read_buffer(completion.payload, bytes) != FLY_SESSION_V2_OK ||
            bytes.size() != 64)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        std::array<std::uint8_t, 64> signature{};
        std::copy(bytes.begin(), bytes.end(), signature.begin());
        if (wire::finish_session_signing_binding_v1(
                pretag_, signature, &material_.binding,
                &material_.binding_hash) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        pending_.reset();
        return prepare(Stage::PersistBinding);
    }
    if (stage_ == Stage::PersistBinding)
    {
        if (completion.payload.resource == 0)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        material_.record_revision = completion.payload.resource;
        pending_.reset();
        return prepare(Stage::PersistBindingObject);
    }
    if (stage_ == Stage::PersistBindingObject)
    {
        if (completion.payload.resource == 0 ||
            completion.payload.hash != material_.binding_hash)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        material_.binding_object_ref = completion.payload.resource;
        pending_.reset();
        stage_ = Stage::Ready;
        ready_ = true;
        durable_key_ref_.clear();
        return FLY_SESSION_V2_OK;
    }
    return reject(FLY_SESSION_V2_INVALID_STATE);
}

fly_session_result_v2 SessionSigningScheduler::reject(
    fly_session_result_v2 result) noexcept
{
    pending_.reset();
    stage_ = Stage::Failed;
    failed_ = true;
    return result == FLY_SESSION_V2_OK ? FLY_SESSION_V2_CONTRACT_VIOLATION
                                       : result;
}

fly_session_result_v2 SessionSigningScheduler::cancel_pending() noexcept
{
    if (!pending_) return FLY_SESSION_V2_INVALID_STATE;
    const auto result = operations_.cancel(pending_->token);
    if (result == FLY_SESSION_V2_OK) reject(FLY_SESSION_V2_CANCELLED);
    return result;
}

} // namespace flynes::session
