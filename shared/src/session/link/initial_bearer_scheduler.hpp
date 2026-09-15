#ifndef FLYNES_SESSION_LINK_INITIAL_BEARER_SCHEDULER_HPP
#define FLYNES_SESSION_LINK_INITIAL_BEARER_SCHEDULER_HPP

#include "initial_plan_scheduler.hpp"
#include "../wire/initial_bearer.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

enum class InitialBearerEffectKind : std::uint8_t
{
    DeriveKey,
    CreateBearer,
    PrepareCredential,
    AeadSeal,
    AeadOpen,
    Persist,
    JoinBearer
};

struct InitialBearerStartV1
{
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t generation = 0;
    std::uint64_t first_operation_id = 0;
    wire::PairRoleV1 local_role{};
    std::array<std::uint8_t, 32> transcript_hash{};
    std::array<std::uint8_t, 16> session_id{};
    wire::BearerPlanBytes selected_plan{};
    fly_session_resource_handle_v2 ecdh_secret = 0;
};

struct InitialBearerEffect final
{
    InitialBearerEffectKind kind{};
    fly_session_op_token_v2 token{};
    fly_session_resource_handle_v2 resource = 0;
    InitialPlanCommandKind persist_kind{};
    std::array<std::uint8_t, 32> plan_hash{};
    wire::BearerPlanBytes selected_plan{};
    std::uint32_t confirmation_budget = 0;
    bool creator = false;
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> info;
    std::vector<std::uint8_t> nonce;
    std::vector<std::uint8_t> aad;
    std::vector<std::uint8_t> input;
};

struct InitialBearerResourcesV1
{
    fly_session_resource_handle_v2 credential_i2r_key = 0;
    fly_session_resource_handle_v2 credential_r2i_key = 0;
    fly_session_resource_handle_v2 credential = 0;
    fly_session_resource_handle_v2 bearer = 0;
};

class InitialBearerScheduler final
{
public:
    explicit InitialBearerScheduler(InitialPlanScheduler& plan) noexcept
        : plan_(&plan) {}
    bool begin(const InitialBearerStartV1& start);
    [[nodiscard]] std::optional<InitialBearerEffect> poll_effect() const
    { return pending_; }
    fly_session_result_v2 complete(const fly_session_port_event_v2& event);
    fly_session_result_v2 accept_peer_logical(const std::uint8_t* bytes,
                                               std::size_t size);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> local_logical() const
    { return local_send_; }
    fly_session_result_v2 mark_local_sent();
    fly_session_result_v2 cancel_pending() noexcept;

    [[nodiscard]] bool ready() const noexcept { return ready_ && !failed_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] bool creator() const noexcept { return local_creator_; }
    [[nodiscard]] std::uint64_t next_operation_id() const noexcept
    { return next_operation_id_; }
    [[nodiscard]] InitialBearerResourcesV1 owned_resources() const noexcept
    { return resources_; }
    [[nodiscard]] const std::array<std::uint8_t, 32>& credential_logical_hash()
        const noexcept { return credential_logical_hash_; }

private:
    enum class Stage : std::uint8_t
    {
        Empty, DeriveI2r, DeriveR2i, AwaitCommand, Create,
        PrepareCreator, WaitPeer, OpenPeer, PrepareReceiver,
        SealLocal, PersistPrompt, PersistCredential, WaitLocalSend,
        Join, Ready, Failed
    };

    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;
    fly_session_result_v2 issue(InitialBearerEffect effect,
                                std::uint32_t payload_kind, Stage stage);
    fly_session_result_v2 derive(bool i2r);
    fly_session_result_v2 process_plan_command();
    fly_session_result_v2 prepare_credential(bool creator);
    fly_session_result_v2 seal_local_credential();
    fly_session_result_v2 persist(bool prompt);
    fly_session_result_v2 join_bearer();
    fly_session_result_v2 reject(fly_session_result_v2 result) noexcept;
    fly_session_result_v2 read_buffer(
        const ProviderOperationCompletion& completion,
        std::vector<std::uint8_t>& out) const;

    InitialPlanScheduler* plan_ = nullptr;
    ProviderOperationJournal operations_{1};
    InitialBearerStartV1 start_{};
    InitialBearerResourcesV1 resources_{};
    std::optional<InitialBearerEffect> pending_{};
    std::optional<std::vector<std::uint8_t>> local_send_{};
    std::vector<std::uint8_t> peer_exact_{};
    std::vector<std::uint8_t> peer_ciphertext_{};
    std::array<std::uint8_t, wire::kBearerJoinParamsSizeV1> join_params_{};
    std::array<std::uint8_t, 32> credential_logical_hash_{};
    std::uint64_t next_operation_id_ = 0;
    std::uint64_t pending_plan_command_id_ = 0;
    Stage stage_ = Stage::Empty;
    bool local_creator_ = false;
    bool begun_ = false;
    bool ready_ = false;
    bool failed_ = false;
};

} // namespace flynes::session

#endif
