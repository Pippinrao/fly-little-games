#include "pair_reveal_scheduler.hpp"

#include "../wire/p256_point.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace flynes::session {
namespace {

bool nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return bytes != nullptr &&
        std::any_of(bytes, bytes + size,
                    [](std::uint8_t byte) { return byte != 0; });
}

} // namespace

PairRevealScheduler::PairRevealScheduler() = default;

bool PairRevealScheduler::local_is_initiator() const noexcept
{
    return start_.local_role == wire::PairRoleV1::Initiator;
}

bool PairRevealScheduler::start_valid() const noexcept
{
    wire::PairContextV1 canonical{};
    const auto& local_commit = local_is_initiator()
        ? start_.initiator_commit : start_.responder_commit;
    std::array<std::uint8_t, wire::kPairRevealAadSizeV1> aad{};
    return start_.generation != 0 && start_.first_operation_id != 0 &&
        start_.first_operation_id <=
            (std::numeric_limits<std::uint64_t>::max)() - 7 &&
        nonzero(start_.engine_instance_id.data(),
                start_.engine_instance_id.size()) &&
        nonzero(start_.link_id.data(), start_.link_id.size()) &&
        (start_.local_role == wire::PairRoleV1::Initiator ||
         start_.local_role == wire::PairRoleV1::Responder) &&
        start_.local_material.ecdh_key != 0 &&
        start_.local_material.contribution.role == start_.local_role &&
        wire::decode_pair_context_v1(
            start_.context.bytes.data(), start_.context.bytes.size(),
            &canonical) == wire::Status::Ok &&
        canonical.hash == start_.context.hash &&
        wire::bind_reveal_to_commit_v1(
            local_commit, start_.local_material.contribution) ==
            wire::Status::Ok &&
        wire::build_pair_reveal_aad_v1(
            start_.context, start_.initiator_commit, start_.responder_commit,
            start_.local_role, &aad) == wire::Status::Ok;
}

fly_session_op_token_v2 PairRevealScheduler::token(
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

bool PairRevealScheduler::begin(const PairRevealStartV1& start)
{
    if (begun_ || failed_)
        return false;
    start_ = start;
    if (!start_valid())
    {
        failed_ = true;
        stage_ = Stage::Failed;
        return false;
    }
    begun_ = true;
    next_operation_id_ = start_.first_operation_id;
    return prepare(Stage::Agree) == FLY_SESSION_V2_OK;
}

fly_session_result_v2 PairRevealScheduler::prepare(Stage stage)
{
    try
    {
        PairRevealEffect effect{};
        effect.token = token(next_operation_id_++);
        std::uint32_t expected = 0;
        const auto& peer_commit = local_is_initiator()
            ? start_.responder_commit : start_.initiator_commit;
        switch (stage)
        {
        case Stage::Agree:
            effect.kind = PairRevealEffectKind::AgreeKey;
            effect.resource = start_.local_material.ecdh_key;
            effect.peer_public_key.assign(peer_commit.ephemeral_public_key.begin(),
                                          peer_commit.ephemeral_public_key.end());
            effect.input.assign(start_.context.hash.begin(),
                                start_.context.hash.end());
            expected = FLY_SESSION_PROVIDER_KEY_AGREEMENT_V2;
            break;
        case Stage::DeriveI2r:
        case Stage::DeriveR2i:
        {
            static constexpr char i2r[] = "flynes-pair-reveal-i2r-v1";
            static constexpr char r2i[] = "flynes-pair-reveal-r2i-v1";
            const auto* label = stage == Stage::DeriveI2r ? i2r : r2i;
            const auto size = stage == Stage::DeriveI2r
                ? sizeof(i2r) - 1 : sizeof(r2i) - 1;
            effect.kind = PairRevealEffectKind::DeriveKey;
            effect.resource = secrets_.ecdh_secret;
            effect.salt.assign(start_.context.hash.begin(),
                               start_.context.hash.end());
            effect.info.assign(
                reinterpret_cast<const std::uint8_t*>(label),
                reinterpret_cast<const std::uint8_t*>(label) + size);
            effect.byte_count = 32;
            expected = FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2;
            break;
        }
        case Stage::RandomLocal:
        {
            static constexpr char purpose[] = "flynes-pair-reveal-nonce-v1";
            effect.kind = PairRevealEffectKind::RandomBytes;
            effect.byte_count = 12;
            effect.info.assign(
                reinterpret_cast<const std::uint8_t*>(purpose),
                reinterpret_cast<const std::uint8_t*>(purpose) +
                    sizeof(purpose) - 1);
            expected = FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2;
            break;
        }
        case Stage::SealLocal:
        case Stage::OpenPeer:
        {
            effect.kind = stage == Stage::SealLocal
                ? PairRevealEffectKind::AeadSeal
                : PairRevealEffectKind::AeadOpen;
            const auto sender = stage == Stage::SealLocal
                ? start_.local_role
                : local_is_initiator() ? wire::PairRoleV1::Responder
                                       : wire::PairRoleV1::Initiator;
            effect.resource = sender == wire::PairRoleV1::Initiator
                ? secrets_.i2r_key : secrets_.r2i_key;
            std::array<std::uint8_t, wire::kPairRevealAadSizeV1> aad{};
            if (wire::build_pair_reveal_aad_v1(
                    start_.context, start_.initiator_commit,
                    start_.responder_commit, sender, &aad) != wire::Status::Ok)
                return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
            effect.aad.assign(aad.begin(), aad.end());
            if (stage == Stage::SealLocal)
            {
                effect.nonce.assign(local_nonce_.begin(), local_nonce_.end());
                effect.input.assign(start_.local_material.contribution.bytes.begin(),
                                    start_.local_material.contribution.bytes.end());
            }
            else
            {
                if (!peer_envelope_)
                    return reject(FLY_SESSION_V2_INVALID_STATE);
                effect.nonce.assign(peer_envelope_->public_nonce.begin(),
                                    peer_envelope_->public_nonce.end());
                effect.input.assign(peer_envelope_->ciphertext_and_tag.begin(),
                                    peer_envelope_->ciphertext_and_tag.end());
            }
            expected = FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
            break;
        }
        default:
            return reject(FLY_SESSION_V2_INVALID_STATE);
        }
        const auto result = operations_.expect(effect.token, expected);
        if (result != FLY_SESSION_V2_OK)
            return reject(result);
        pending_ = std::move(effect);
        stage_ = stage;
        return FLY_SESSION_V2_OK;
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

std::optional<PairRevealEffect> PairRevealScheduler::poll_effect() const
{
    return pending_;
}

fly_session_result_v2 PairRevealScheduler::read_buffer(
    const ParsedProviderEvent& payload, std::vector<std::uint8_t>& out) const
{
    if (payload.buffer == nullptr || payload.value0 == 0 || payload.value0 > 4096)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    try { out.assign(static_cast<std::size_t>(payload.value0), 0); }
    catch (const std::bad_alloc&) { return FLY_SESSION_V2_OUT_OF_MEMORY; }
    std::uint64_t written = 0;
    const fly_session_write_bytes_v2 destination{out.data(), out.size()};
    return fly_session_buffer_read_v2(
               payload.buffer, 0, destination, &written) == FLY_SESSION_V2_OK &&
            written == out.size()
        ? FLY_SESSION_V2_OK : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 PairRevealScheduler::complete(
    const fly_session_port_event_v2& event)
{
    if (!pending_ || failed_)
        return FLY_SESSION_V2_INVALID_STATE;
    ProviderOperationCompletion completion{};
    const auto accepted = operations_.accept(event, completion);
    if (accepted != FLY_SESSION_V2_OK)
        return accepted;
    if (completion.result != FLY_SESSION_V2_OK)
        return reject(completion.result);

    std::vector<std::uint8_t> bytes;
    const auto completed_stage = stage_;
    pending_.reset();
    switch (completed_stage)
    {
    case Stage::Agree:
        if (completion.payload.resource == 0)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        secrets_.ecdh_secret = completion.payload.resource;
        return prepare(Stage::DeriveI2r);
    case Stage::DeriveI2r:
        if (completion.payload.resource == 0)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        secrets_.i2r_key = completion.payload.resource;
        return prepare(Stage::DeriveR2i);
    case Stage::DeriveR2i:
        if (completion.payload.resource == 0)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        secrets_.r2i_key = completion.payload.resource;
        if (local_is_initiator())
            return prepare(Stage::RandomLocal);
        if (peer_envelope_)
            return prepare(Stage::OpenPeer);
        stage_ = Stage::WaitPeer;
        return FLY_SESSION_V2_OK;
    case Stage::RandomLocal:
    {
        const auto result = read_buffer(completion.payload, bytes);
        if (result != FLY_SESSION_V2_OK || bytes.size() != local_nonce_.size())
            return reject(result == FLY_SESSION_V2_OK
                              ? FLY_SESSION_V2_CONTRACT_VIOLATION : result);
        std::copy(bytes.begin(), bytes.end(), local_nonce_.begin());
        return prepare(Stage::SealLocal);
    }
    case Stage::SealLocal:
    {
        const auto result = read_buffer(completion.payload, bytes);
        if (result != FLY_SESSION_V2_OK ||
            bytes.size() != wire::kPairRevealCiphertextAndTagSizeV1)
            return reject(result == FLY_SESSION_V2_OK
                              ? FLY_SESSION_V2_CONTRACT_VIOLATION : result);
        std::array<std::uint8_t, wire::kPairRevealCiphertextAndTagSizeV1>
            ciphertext{};
        std::copy(bytes.begin(), bytes.end(), ciphertext.begin());
        std::array<std::uint8_t, wire::kPairRevealBodySizeV1> envelope{};
        if (wire::encode_pair_reveal_envelope_v1(
                local_nonce_, ciphertext, &envelope) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        local_envelope_ = envelope;
        stage_ = Stage::WaitLocalSent;
        return FLY_SESSION_V2_OK;
    }
    case Stage::OpenPeer:
    {
        const auto result = read_buffer(completion.payload, bytes);
        if (result != FLY_SESSION_V2_OK ||
            bytes.size() != wire::kPairRevealPlaintextSizeV1)
            return reject(result == FLY_SESSION_V2_OK
                              ? FLY_SESSION_V2_AUTH_FAILED : result);
        const auto peer_role = local_is_initiator()
            ? wire::PairRoleV1::Responder : wire::PairRoleV1::Initiator;
        const auto& peer_commit = local_is_initiator()
            ? start_.responder_commit : start_.initiator_commit;
        wire::PairContributionV1 contribution{};
        if (wire::decode_pair_contribution_v1(
                bytes.data(), bytes.size(), start_.context, peer_role,
                wire::validate_p256_uncompressed_point_callback, nullptr,
                &contribution) != wire::Status::Ok ||
            wire::bind_reveal_to_commit_v1(peer_commit, contribution) !=
                wire::Status::Ok)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        peer_contribution_ = contribution;
        if (!local_is_initiator())
            return prepare(Stage::RandomLocal);
        stage_ = local_sent_ ? Stage::Ready : Stage::WaitLocalSent;
        return FLY_SESSION_V2_OK;
    }
    default:
        return reject(FLY_SESSION_V2_INVALID_STATE);
    }
}

fly_session_result_v2 PairRevealScheduler::accept_peer_envelope(
    const std::uint8_t* body, std::size_t size,
    const std::array<std::uint8_t, 32>& logical_hash)
{
    const bool responder_can_queue = !local_is_initiator() &&
        (stage_ == Stage::Agree || stage_ == Stage::DeriveI2r ||
         stage_ == Stage::DeriveR2i || stage_ == Stage::WaitPeer);
    const bool initiator_can_queue = local_is_initiator() &&
        stage_ == Stage::WaitPeer;
    if (!begun_ || failed_ || peer_envelope_ ||
        (!responder_can_queue && !initiator_can_queue) ||
        !nonzero(logical_hash.data(), logical_hash.size()))
        return FLY_SESSION_V2_INVALID_STATE;
    wire::PairRevealEnvelopeV1 envelope{};
    if (wire::decode_pair_reveal_envelope_v1(body, size, &envelope) !=
        wire::Status::Ok)
        return reject(FLY_SESSION_V2_AUTH_FAILED);
    peer_envelope_ = envelope;
    peer_logical_hash_ = logical_hash;
    if (pending_)
        return FLY_SESSION_V2_ACCEPTED;
    return prepare(Stage::OpenPeer) == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_ACCEPTED : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 PairRevealScheduler::mark_local_reveal_sent() noexcept
{
    if (!local_envelope_ || local_sent_ || stage_ != Stage::WaitLocalSent)
        return FLY_SESSION_V2_INVALID_STATE;
    local_sent_ = true;
    if (local_is_initiator())
        stage_ = peer_contribution_ ? Stage::Ready : Stage::WaitPeer;
    else
        stage_ = peer_contribution_ ? Stage::Ready : Stage::Failed;
    return stage_ == Stage::Failed ? reject(FLY_SESSION_V2_INVALID_STATE)
                                   : FLY_SESSION_V2_OK;
}

fly_session_result_v2 PairRevealScheduler::cancel_pending() noexcept
{
    if (!pending_)
        return FLY_SESSION_V2_INVALID_STATE;
    const auto result = operations_.cancel(pending_->token);
    if (result == FLY_SESSION_V2_OK)
        reject(FLY_SESSION_V2_CANCELLED);
    return result;
}

bool PairRevealScheduler::local_reveal_ready() const noexcept
{
    return local_envelope_.has_value() && !failed_;
}

bool PairRevealScheduler::peer_reveal_verified() const noexcept
{
    return peer_contribution_.has_value() && !failed_;
}

bool PairRevealScheduler::ready() const noexcept
{
    return stage_ == Stage::Ready && !failed_;
}

std::optional<std::array<std::uint8_t, wire::kPairRevealBodySizeV1>>
PairRevealScheduler::local_envelope() const
{
    return local_reveal_ready() ? local_envelope_ : std::nullopt;
}

std::optional<wire::PairContributionV1>
PairRevealScheduler::peer_contribution() const
{
    return peer_reveal_verified() ? peer_contribution_ : std::nullopt;
}

fly_session_result_v2 PairRevealScheduler::reject(
    fly_session_result_v2 result) noexcept
{
    pending_.reset();
    stage_ = Stage::Failed;
    failed_ = true;
    return result == FLY_SESSION_V2_OK ? FLY_SESSION_V2_CONTRACT_VIOLATION
                                       : result;
}

} // namespace flynes::session
