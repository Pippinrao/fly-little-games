#ifndef FLYNES_SESSION_LINK_PAIR_SAS_SCHEDULER_HPP
#define FLYNES_SESSION_LINK_PAIR_SAS_SCHEDULER_HPP

#include "../ports/provider_operation_journal.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

enum class PairSasEffectKind : std::uint8_t
{
    DeriveKey,
    Hmac
};

struct PairSasStartV1
{
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t generation = 0;
    std::uint64_t first_operation_id = 0;
    fly_session_resource_handle_v2 ecdh_secret = 0;
    std::array<std::uint8_t, 32> transcript_hash{};
};

struct PairSasEffect final
{
    PairSasEffectKind kind{};
    fly_session_op_token_v2 token{};
    fly_session_resource_handle_v2 resource = 0;
    std::uint32_t byte_count = 0;
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> info;
    std::vector<std::uint8_t> input;
};

struct PairSasSecretsV1
{
    fly_session_resource_handle_v2 gatt_i2r_key = 0;
    fly_session_resource_handle_v2 gatt_r2i_key = 0;
    fly_session_resource_handle_v2 sas_key = 0;
};

class PairSasScheduler final
{
public:
    bool begin(const PairSasStartV1& start);
    [[nodiscard]] std::optional<PairSasEffect> poll_effect() const
    { return pending_; }
    fly_session_result_v2 complete(const fly_session_port_event_v2& event);
    fly_session_result_v2 cancel_pending() noexcept;

    [[nodiscard]] bool ready() const noexcept { return ready_ && !failed_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::optional<std::array<std::uint8_t, 6>> sas() const noexcept
    { return sas_; }
    [[nodiscard]] PairSasSecretsV1 owned_secrets() const noexcept
    { return secrets_; }
    [[nodiscard]] std::uint64_t next_operation_id() const noexcept
    { return next_operation_id_; }

private:
    enum class Stage : std::uint8_t
    {
        Empty,
        DeriveGattI2r,
        DeriveGattR2i,
        DeriveSas,
        HmacSas,
        Ready,
        Failed
    };

    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;
    fly_session_result_v2 prepare(Stage stage);
    fly_session_result_v2 reject(fly_session_result_v2 result) noexcept;

    ProviderOperationJournal operations_{1};
    PairSasStartV1 start_{};
    PairSasSecretsV1 secrets_{};
    std::optional<PairSasEffect> pending_{};
    std::optional<std::array<std::uint8_t, 6>> sas_{};
    std::uint64_t next_operation_id_ = 0;
    std::uint32_t retry_index_ = 0;
    Stage stage_ = Stage::Empty;
    bool begun_ = false;
    bool ready_ = false;
    bool failed_ = false;
};

} // namespace flynes::session

#endif
