#include "pair_sas_scheduler.hpp"

#include "../wire/pair_crypto.hpp"
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

void append_u32be(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 24u));
    out.push_back(static_cast<std::uint8_t>(value >> 16u));
    out.push_back(static_cast<std::uint8_t>(value >> 8u));
    out.push_back(static_cast<std::uint8_t>(value));
}

} // namespace

fly_session_op_token_v2 PairSasScheduler::token(
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

bool PairSasScheduler::begin(const PairSasStartV1& start)
{
    if (begun_ || failed_ || start.generation == 0 ||
        start.first_operation_id == 0 || start.ecdh_secret == 0 ||
        !nonzero(start.engine_instance_id.data(), start.engine_instance_id.size()) ||
        !nonzero(start.link_id.data(), start.link_id.size()) ||
        !nonzero(start.transcript_hash.data(), start.transcript_hash.size()) ||
        start.first_operation_id >
            (std::numeric_limits<std::uint64_t>::max)() -
                static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)()) - 4)
        return false;
    start_ = start;
    next_operation_id_ = start.first_operation_id;
    begun_ = true;
    return prepare(Stage::DeriveGattI2r) == FLY_SESSION_V2_OK;
}

fly_session_result_v2 PairSasScheduler::prepare(Stage stage)
{
    try
    {
        PairSasEffect effect{};
        effect.token = token(next_operation_id_++);
        std::uint32_t expected = 0;
        if (stage == Stage::DeriveGattI2r ||
            stage == Stage::DeriveGattR2i || stage == Stage::DeriveSas)
        {
            static constexpr char salt_label[] = "flynes-pair-v1";
            static constexpr char gatt_i2r[] = "flynes-pair-gatt-i2r-v1";
            static constexpr char gatt_r2i[] = "flynes-pair-gatt-r2i-v1";
            static constexpr char sas[] = "flynes-pair-sas-v1";
            std::array<std::uint8_t, sizeof(salt_label) - 1 + 32> input{};
            std::copy_n(reinterpret_cast<const std::uint8_t*>(salt_label),
                        sizeof(salt_label) - 1, input.begin());
            std::copy(start_.transcript_hash.begin(),
                      start_.transcript_hash.end(),
                      input.begin() + sizeof(salt_label) - 1);
            const auto salt = wire::sha256(input.data(), input.size());
            const char* label = gatt_i2r;
            std::size_t label_size = sizeof(gatt_i2r) - 1;
            if (stage == Stage::DeriveGattR2i)
            {
                label = gatt_r2i;
                label_size = sizeof(gatt_r2i) - 1;
            }
            else if (stage == Stage::DeriveSas)
            {
                label = sas;
                label_size = sizeof(sas) - 1;
            }
            effect.kind = PairSasEffectKind::DeriveKey;
            effect.resource = start_.ecdh_secret;
            effect.byte_count = 32;
            effect.salt.assign(salt.begin(), salt.end());
            effect.info.assign(reinterpret_cast<const std::uint8_t*>(label),
                               reinterpret_cast<const std::uint8_t*>(label) +
                                   label_size);
            expected = FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2;
        }
        else if (stage == Stage::HmacSas)
        {
            static constexpr char retry_label[] = "flynes-sas-retry-v1";
            effect.kind = PairSasEffectKind::Hmac;
            effect.resource = secrets_.sas_key;
            if (retry_index_ == 0)
                effect.input.assign(start_.transcript_hash.begin(),
                                    start_.transcript_hash.end());
            else
            {
                effect.input.assign(
                    reinterpret_cast<const std::uint8_t*>(retry_label),
                    reinterpret_cast<const std::uint8_t*>(retry_label) +
                        sizeof(retry_label) - 1);
                effect.input.insert(effect.input.end(),
                                    start_.transcript_hash.begin(),
                                    start_.transcript_hash.end());
                append_u32be(effect.input, retry_index_);
            }
            expected = FLY_SESSION_PROVIDER_CRYPTO_MAC_V2;
        }
        else
            return reject(FLY_SESSION_V2_INVALID_STATE);
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

fly_session_result_v2 PairSasScheduler::complete(
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
    if (completed_stage == Stage::DeriveGattI2r ||
        completed_stage == Stage::DeriveGattR2i ||
        completed_stage == Stage::DeriveSas)
    {
        if (completion.payload.resource == 0)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        if (completed_stage == Stage::DeriveGattI2r)
        {
            secrets_.gatt_i2r_key = completion.payload.resource;
            return prepare(Stage::DeriveGattR2i);
        }
        if (completed_stage == Stage::DeriveGattR2i)
        {
            secrets_.gatt_r2i_key = completion.payload.resource;
            return prepare(Stage::DeriveSas);
        }
        secrets_.sas_key = completion.payload.resource;
        return prepare(Stage::HmacSas);
    }
    if (completed_stage != Stage::HmacSas || !completion.payload.buffer ||
        completion.payload.value0 != 32)
        return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    std::array<std::uint8_t, 32> block{};
    std::uint64_t written = 0;
    const fly_session_write_bytes_v2 destination{block.data(), block.size()};
    if (fly_session_buffer_read_v2(completion.payload.buffer, 0, destination,
                                   &written) != FLY_SESSION_V2_OK ||
        written != block.size())
        return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    std::array<std::uint8_t, 6> value{};
    const auto result = wire::pair_sas_from_blocks(&block, 1, &value);
    if (result == wire::PairCryptoStatus::Ok)
    {
        sas_ = value;
        ready_ = true;
        stage_ = Stage::Ready;
        return FLY_SESSION_V2_OK;
    }
    if (result != wire::PairCryptoStatus::Retry ||
        retry_index_ == (std::numeric_limits<std::uint32_t>::max)())
        return reject(FLY_SESSION_V2_AUTH_FAILED);
    ++retry_index_;
    return prepare(Stage::HmacSas);
}

fly_session_result_v2 PairSasScheduler::cancel_pending() noexcept
{
    if (!pending_)
        return FLY_SESSION_V2_INVALID_STATE;
    const auto result = operations_.cancel(pending_->token);
    if (result == FLY_SESSION_V2_OK)
        reject(FLY_SESSION_V2_CANCELLED);
    return result;
}

fly_session_result_v2 PairSasScheduler::reject(
    fly_session_result_v2 result) noexcept
{
    pending_.reset();
    failed_ = true;
    stage_ = Stage::Failed;
    return result == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_CONTRACT_VIOLATION : result;
}

} // namespace flynes::session
