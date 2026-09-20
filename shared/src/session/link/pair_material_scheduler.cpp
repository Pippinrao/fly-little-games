#include "pair_material_scheduler.hpp"

#include "../wire/p256_point.hpp"
#include "../wire/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace flynes::session {

PairMaterialScheduler::PairMaterialScheduler() = default;

bool PairMaterialScheduler::nonzero(const std::uint8_t* bytes,
                                    std::size_t size) noexcept
{
    return bytes != nullptr &&
        std::any_of(bytes, bytes + size,
                    [](std::uint8_t value) { return value != 0; });
}

bool PairMaterialScheduler::trusted_generated_point(
    void*, const std::uint8_t point[65])
{
    return wire::validate_p256_uncompressed_point(point);
}

fly_session_op_token_v2 PairMaterialScheduler::token(
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

bool PairMaterialScheduler::begin(const PairMaterialStartV1& start)
{
    wire::PairContextV1 canonical{};
    if (begun_ || failed_ || start.generation == 0 ||
        start.first_operation_id == 0 ||
        start.first_operation_id >
            (std::numeric_limits<std::uint64_t>::max)() - 7 ||
        !nonzero(start.engine_instance_id.data(), start.engine_instance_id.size()) ||
        !nonzero(start.link_id.data(), start.link_id.size()) ||
        !nonzero(start.capability_summary_hash.data(),
                 start.capability_summary_hash.size()) ||
        (start.role != wire::PairRoleV1::Initiator &&
         start.role != wire::PairRoleV1::Responder) ||
        wire::decode_pair_context_v1(start.context.bytes.data(),
                                     start.context.bytes.size(), &canonical) !=
            wire::Status::Ok ||
        canonical.hash != start.context.hash)
    {
        failed_ = true;
        stage_ = Stage::Failed;
        return false;
    }
    start_ = start;
    begun_ = true;
    return prepare(Stage::GenerateIdentity) == FLY_SESSION_V2_OK;
}

fly_session_result_v2 PairMaterialScheduler::prepare(Stage stage)
{
    try
    {
        PairMaterialEffect effect{};
        const auto ordinal = static_cast<std::uint64_t>(stage) - 1;
        effect.token = token(start_.first_operation_id + ordinal);
        std::uint32_t expected = 0;
        switch (stage)
        {
        case Stage::GenerateIdentity:
        case Stage::GenerateEcdh:
        case Stage::GenerateTls:
            effect.kind = PairMaterialEffectKind::GenerateKey;
            effect.key_purpose = stage == Stage::GenerateIdentity
                ? FLY_SESSION_KEY_DEVICE_IDENTITY_V2
                : stage == Stage::GenerateEcdh
                    ? FLY_SESSION_KEY_PAIR_ECDH_V2
                    : FLY_SESSION_KEY_TLS_V2;
            effect.exact_bytes.assign(start_.context.bytes.begin(),
                                      start_.context.bytes.end());
            expected = FLY_SESSION_PROVIDER_KEY_HANDLE_V2;
            break;
        case Stage::ReadIdentityPublic:
        case Stage::ReadEcdhPublic:
        case Stage::ReadTlsSpki:
            effect.kind = PairMaterialEffectKind::ReadPublicKey;
            effect.resource = stage == Stage::ReadIdentityPublic
                ? material_.identity_key
                : stage == Stage::ReadEcdhPublic ? material_.ecdh_key
                                                  : material_.tls_key;
            effect.public_key_encoding = stage == Stage::ReadTlsSpki
                ? FLY_SESSION_PUBLIC_KEY_DER_SPKI_V2
                : FLY_SESSION_PUBLIC_KEY_X963_UNCOMPRESSED_V2;
            expected = FLY_SESSION_PROVIDER_KEY_PUBLIC_V2;
            break;
        case Stage::CreateTlsMaterial:
            effect.kind = PairMaterialEffectKind::CreateTlsMaterial;
            effect.resource = material_.tls_key;
            effect.exact_bytes.assign(start_.context.hash.begin(),
                                      start_.context.hash.end());
            expected = FLY_SESSION_PROVIDER_TLS_MATERIAL_V2;
            break;
        case Stage::RandomNonce:
        {
            static constexpr char purpose[] =
                "flynes-pair-contribution-nonce-v1";
            effect.kind = PairMaterialEffectKind::RandomBytes;
            effect.byte_count = 32;
            effect.exact_bytes.assign(
                reinterpret_cast<const std::uint8_t*>(purpose),
                reinterpret_cast<const std::uint8_t*>(purpose) +
                    sizeof(purpose) - 1);
            expected = FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2;
            break;
        }
        default:
            return reject(FLY_SESSION_V2_INVALID_STATE);
        }
        const auto registered = operations_.expect(effect.token, expected);
        if (registered != FLY_SESSION_V2_OK)
            return reject(registered);
        stage_ = stage;
        pending_ = std::move(effect);
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

std::optional<PairMaterialEffect> PairMaterialScheduler::poll_effect() const
{
    return pending_;
}

fly_session_result_v2 PairMaterialScheduler::read_buffer(
    const ParsedProviderEvent& payload, std::vector<std::uint8_t>& out) const
{
    if (payload.buffer == nullptr || payload.value0 == 0 ||
        payload.value0 > 4096)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    try
    {
        out.assign(static_cast<std::size_t>(payload.value0), 0);
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
    std::uint64_t written = 0;
    const fly_session_write_bytes_v2 destination{out.data(), out.size()};
    const auto result = fly_session_buffer_read_v2(
        payload.buffer, 0, destination, &written);
    return result == FLY_SESSION_V2_OK && written == out.size()
        ? FLY_SESSION_V2_OK : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 PairMaterialScheduler::complete(
    const fly_session_port_event_v2& event)
{
    if (!pending_ || failed_ || ready_)
        return FLY_SESSION_V2_INVALID_STATE;
    ProviderOperationCompletion completion{};
    const auto accepted = operations_.accept(event, completion);
    if (accepted != FLY_SESSION_V2_OK)
        return accepted;
    if (completion.result != FLY_SESSION_V2_OK)
        return reject(completion.result);

    std::vector<std::uint8_t> bytes;
    switch (stage_)
    {
    case Stage::GenerateIdentity:
    case Stage::GenerateEcdh:
    case Stage::GenerateTls:
        if (completion.payload.resource == 0)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        if (stage_ == Stage::GenerateIdentity)
            material_.identity_key = completion.payload.resource;
        else if (stage_ == Stage::GenerateEcdh)
            material_.ecdh_key = completion.payload.resource;
        else
            material_.tls_key = completion.payload.resource;
        break;
    case Stage::ReadIdentityPublic:
    case Stage::ReadEcdhPublic:
        if (read_buffer(completion.payload, bytes) != FLY_SESSION_V2_OK ||
            bytes.size() != 65 || !trusted_generated_point(nullptr, bytes.data()))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        if (stage_ == Stage::ReadIdentityPublic)
            std::copy(bytes.begin(), bytes.end(), identity_public_.begin());
        else
            std::copy(bytes.begin(), bytes.end(), ecdh_public_.begin());
        break;
    case Stage::ReadTlsSpki:
    {
        const auto result = read_buffer(completion.payload, bytes);
        if (result != FLY_SESSION_V2_OK)
            return reject(result);
        tls_spki_ = std::move(bytes);
        tls_spki_hash_ = wire::sha256(tls_spki_.data(), tls_spki_.size());
        break;
    }
    case Stage::CreateTlsMaterial:
        if (completion.payload.resource == 0 ||
            completion.payload.hash != tls_spki_hash_)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        material_.tls_material = completion.payload.resource;
        break;
    case Stage::RandomNonce:
    {
        const auto result = read_buffer(completion.payload, bytes);
        if (result != FLY_SESSION_V2_OK || bytes.size() != 32 ||
            !nonzero(bytes.data(), bytes.size()))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::copy(bytes.begin(), bytes.end(), contribution_nonce_.begin());
        pending_.reset();
        return seal();
    }
    default:
        return reject(FLY_SESSION_V2_INVALID_STATE);
    }

    const auto next = static_cast<Stage>(
        static_cast<std::uint8_t>(stage_) + 1);
    pending_.reset();
    return prepare(next);
}

fly_session_result_v2 PairMaterialScheduler::seal()
{
    std::array<std::uint8_t, 320> contribution_bytes{};
    const auto encoded = wire::encode_pair_contribution_v1(
        start_.context, start_.role, identity_public_, ecdh_public_,
        tls_spki_hash_, contribution_nonce_, start_.capability_summary_hash,
        trusted_generated_point, nullptr, &contribution_bytes);
    if (encoded != wire::Status::Ok ||
        wire::decode_pair_contribution_v1(
            contribution_bytes.data(), contribution_bytes.size(), start_.context,
            start_.role, trusted_generated_point, nullptr,
            &material_.contribution) != wire::Status::Ok ||
        wire::encode_pair_commit_v1(
            start_.context, material_.contribution, trusted_generated_point,
            nullptr, &material_.commit_bytes) != wire::Status::Ok)
        return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    stage_ = Stage::Ready;
    ready_ = true;
    tls_spki_.clear();
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 PairMaterialScheduler::reject(
    fly_session_result_v2 result) noexcept
{
    pending_.reset();
    stage_ = Stage::Failed;
    failed_ = true;
    return result == FLY_SESSION_V2_OK ? FLY_SESSION_V2_CONTRACT_VIOLATION
                                       : result;
}

fly_session_result_v2 PairMaterialScheduler::cancel_pending() noexcept
{
    if (!pending_)
        return FLY_SESSION_V2_INVALID_STATE;
    const auto result = operations_.cancel(pending_->token);
    if (result == FLY_SESSION_V2_OK)
        reject(FLY_SESSION_V2_CANCELLED);
    return result;
}

std::optional<PairLocalMaterialV1> PairMaterialScheduler::material() const
{
    return ready_ && !failed_ ? std::optional<PairLocalMaterialV1>(material_)
                              : std::nullopt;
}

} // namespace flynes::session
