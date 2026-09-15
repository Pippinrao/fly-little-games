#ifndef FLYNES_SESSION_LINK_INITIAL_PLAN_SCHEDULER_HPP
#define FLYNES_SESSION_LINK_INITIAL_PLAN_SCHEDULER_HPP

#include "../initial_plan_lock.hpp"
#include "../ports/provider_operation_journal.hpp"
#include "../wire/pair_secure.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

enum class InitialPlanEffectKind : std::uint8_t
{
    Random,
    Hmac,
    AeadSeal,
    AeadOpen,
    Persist
};

struct InitialPlanStartV1
{
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t generation = 0;
    std::uint64_t first_operation_id = 0;
    wire::PairRoleV1 local_role{};
    VerifiedPairEvidence evidence{};
    wire::BearerPlanBytes selected_plan{};
    fly_session_resource_handle_v2 gatt_i2r_key = 0;
    fly_session_resource_handle_v2 gatt_r2i_key = 0;
    fly_session_resource_handle_v2 control_i2r_key = 0;
    fly_session_resource_handle_v2 control_r2i_key = 0;
};

struct InitialPlanEffect final
{
    InitialPlanEffectKind kind{};
    fly_session_op_token_v2 token{};
    fly_session_resource_handle_v2 resource = 0;
    InitialPlanCommandKind persist_kind{};
    std::vector<std::uint8_t> nonce;
    std::vector<std::uint8_t> aad;
    std::vector<std::uint8_t> input;
};

class InitialPlanScheduler final
{
public:
    bool begin(const InitialPlanStartV1& start);
    [[nodiscard]] std::optional<InitialPlanEffect> poll_effect() const
    { return pending_; }
    fly_session_result_v2 complete(const fly_session_port_event_v2& event);
    fly_session_result_v2 accept_peer_logical(const std::uint8_t* bytes,
                                               std::size_t size);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> local_logical() const
    { return local_send_; }
    fly_session_result_v2 mark_local_sent();
    fly_session_result_v2 cancel_pending() noexcept;

    [[nodiscard]] bool mutually_locked() const noexcept
    { return lock_.mutually_locked() && !failed_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::uint64_t next_operation_id() const noexcept
    { return next_operation_id_; }
    [[nodiscard]] const VerifiedPlanEvidence& verified_plan() const noexcept
    { return plan_evidence_; }
    [[nodiscard]] std::optional<InitialPlanCommand> pending_command() const
    { return lock_.poll(); }
    bool accept_credentials(const VerifiedCredentialEvidence& evidence)
    { return lock_.accept_credentials(evidence); }
    bool complete_command(std::uint64_t id, bool success)
    { return lock_.complete(id, start_.generation, success); }

private:
    enum class Message : std::uint8_t { None, Plan, Ack, Final };
    enum class Stage : std::uint8_t
    {
        Empty, RandomPlan, HmacLocal, SealLocal, WaitPeer,
        OpenPeer, HmacPeer, Persist, WaitLocalSend, Ready, Failed
    };

    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;
    fly_session_result_v2 issue(InitialPlanEffect effect,
                                std::uint32_t payload_kind, Stage stage);
    fly_session_result_v2 prepare_random();
    fly_session_result_v2 prepare_local_hmac(Message message);
    fly_session_result_v2 prepare_aead(bool seal, Message message);
    fly_session_result_v2 prepare_peer_hmac();
    fly_session_result_v2 prepare_persist(const InitialPlanCommand& command);
    fly_session_result_v2 after_lock_transition();
    fly_session_result_v2 finalize_local_envelope(
        const std::vector<std::uint8_t>& ciphertext_and_tag);
    fly_session_result_v2 accept_verified_peer();
    fly_session_result_v2 reject(fly_session_result_v2 result) noexcept;
    fly_session_result_v2 read_buffer(
        const ProviderOperationCompletion& completion,
        std::vector<std::uint8_t>& out) const;
    std::uint8_t logical_type(Message message) const noexcept;
    std::uint64_t counter(Message message) const noexcept;
    wire::PairRoleV1 sender(Message message) const noexcept;
    std::vector<std::uint8_t>* exact_for(Message message) noexcept;

    InitialPlanLock lock_{};
    ProviderOperationJournal operations_{1};
    InitialPlanStartV1 start_{};
    std::optional<InitialPlanEffect> pending_{};
    std::optional<std::vector<std::uint8_t>> local_send_{};
    std::vector<std::uint8_t> plan_exact_{};
    std::vector<std::uint8_t> ack_exact_{};
    std::vector<std::uint8_t> final_exact_{};
    std::vector<std::uint8_t> peer_exact_{};
    std::optional<wire::PairSecureEnvelopeV1> peer_envelope_{};
    VerifiedPlanEvidence plan_evidence_{};
    std::array<std::uint8_t, 16> plan_nonce_{};
    std::array<std::uint8_t, 32> local_tag_{};
    std::array<std::uint8_t, 32> peer_tag_{};
    std::vector<std::uint8_t> local_hmac_input_{};
    std::vector<std::uint8_t> peer_hmac_input_{};
    std::uint64_t next_operation_id_ = 0;
    std::uint64_t pending_lock_command_id_ = 0;
    Message current_message_ = Message::None;
    Stage stage_ = Stage::Empty;
    bool begun_ = false;
    bool failed_ = false;
};

} // namespace flynes::session

#endif
