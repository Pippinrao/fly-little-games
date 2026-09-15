#ifndef FLYNES_SESSION_LINK_PAIR_KEY_CONFIRM_SCHEDULER_HPP
#define FLYNES_SESSION_LINK_PAIR_KEY_CONFIRM_SCHEDULER_HPP

#include "pair_signature_scheduler.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

enum class PairKeyConfirmEffectKind : std::uint8_t
{
    Hmac,
    AeadSeal,
    AeadOpen
};

struct PairKeyConfirmStartV1
{
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t generation = 0;
    std::uint64_t first_operation_id = 0;
    wire::PairRoleV1 local_role{};
    std::uint8_t entry_mode = 1;
    std::uint8_t approval_kind = FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2;
    std::array<std::uint8_t, 32> transcript_hash{};
    wire::PairContributionV1 initiator_contribution{};
    wire::PairContributionV1 responder_contribution{};
    fly_session_resource_handle_v2 gatt_i2r_key = 0;
    fly_session_resource_handle_v2 gatt_r2i_key = 0;
    fly_session_resource_handle_v2 control_i2r_key = 0;
    fly_session_resource_handle_v2 control_r2i_key = 0;
};

struct PairKeyConfirmEffect final
{
    PairKeyConfirmEffectKind kind{};
    fly_session_op_token_v2 token{};
    fly_session_resource_handle_v2 resource = 0;
    std::vector<std::uint8_t> nonce;
    std::vector<std::uint8_t> aad;
    std::vector<std::uint8_t> input;
};

class PairKeyConfirmScheduler final
{
public:
    explicit PairKeyConfirmScheduler(PairSignatureScheduler& verification)
        noexcept : verification_(&verification) {}
    bool begin(const PairKeyConfirmStartV1& start);
    [[nodiscard]] std::optional<PairKeyConfirmEffect> poll_effect() const
    { return pending_; }
    fly_session_result_v2 complete(const fly_session_port_event_v2& event);
    fly_session_result_v2 accept_peer_envelope(
        const std::uint8_t* body, std::size_t size,
        const std::array<std::uint8_t, 32>& logical_hash);
    fly_session_result_v2 mark_local_sent() noexcept;
    fly_session_result_v2 cancel_pending() noexcept;

    [[nodiscard]] bool local_envelope_ready() const noexcept
    { return local_envelope_.has_value() && !failed_; }
    [[nodiscard]] bool local_sent() const noexcept { return local_sent_; }
    [[nodiscard]] bool ready() const noexcept { return ready_ && !failed_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> local_envelope() const
    { return local_envelope_; }
    [[nodiscard]] std::uint64_t next_operation_id() const noexcept
    { return next_operation_id_; }

private:
    enum class Stage : std::uint8_t
    {
        Empty,
        WaitPeer,
        HmacLocal,
        VerifyLocal,
        SealLocal,
        OpenPeer,
        VerifyPeer,
        WaitLocalSent,
        Ready,
        Failed
    };

    bool local_is_initiator() const noexcept;
    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;
    fly_session_result_v2 prepare_local_hmac();
    fly_session_result_v2 prepare_aead(bool seal);
    fly_session_result_v2 prepare_verify(
        PairRole role, fly_session_resource_handle_v2 key,
        const std::vector<std::uint8_t>& input,
        const std::array<std::uint8_t, 32>& tag, Stage stage);
    fly_session_result_v2 reject(fly_session_result_v2 result) noexcept;
    fly_session_result_v2 read_buffer(
        const ProviderOperationCompletion& completion,
        std::vector<std::uint8_t>& out) const;

    PairSignatureScheduler* verification_ = nullptr;
    ProviderOperationJournal operations_{1};
    PairKeyConfirmStartV1 start_{};
    std::optional<PairKeyConfirmEffect> pending_{};
    std::optional<wire::PairSecureEnvelopeV1> peer_envelope_{};
    std::optional<std::vector<std::uint8_t>> local_envelope_{};
    std::vector<std::uint8_t> local_hmac_input_{};
    std::array<std::uint8_t, 32> local_tag_{};
    std::uint64_t next_operation_id_ = 0;
    Stage stage_ = Stage::Empty;
    bool begun_ = false;
    bool local_sent_ = false;
    bool ready_ = false;
    bool failed_ = false;
};

} // namespace flynes::session

#endif
