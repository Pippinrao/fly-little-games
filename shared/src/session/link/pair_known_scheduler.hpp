#ifndef FLYNES_SESSION_LINK_PAIR_KNOWN_SCHEDULER_HPP
#define FLYNES_SESSION_LINK_PAIR_KNOWN_SCHEDULER_HPP

#include "../ports/provider_operation_journal.hpp"
#include "../wire/pair_secure.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

enum class PairKnownEffectKind : std::uint8_t
{
    Hmac,
    Sign,
    Verify,
    AeadSeal,
    AeadOpen
};

struct PairKnownStartV1
{
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t generation = 0;
    std::uint64_t first_operation_id = 0;
    wire::PairRoleV1 local_role{};
    std::array<std::uint8_t, 32> transcript_hash{};
    std::array<std::uint8_t, 65> initiator_public_key{};
    std::array<std::uint8_t, 65> responder_public_key{};
    fly_session_resource_handle_v2 local_identity_key = 0;
    fly_session_resource_handle_v2 gatt_i2r_key = 0;
    fly_session_resource_handle_v2 gatt_r2i_key = 0;
    fly_session_resource_handle_v2 control_i2r_key = 0;
    fly_session_resource_handle_v2 control_r2i_key = 0;
    bool local_known = false;
};

struct PairKnownEffect
{
    PairKnownEffectKind kind{};
    fly_session_op_token_v2 token{};
    fly_session_resource_handle_v2 resource = 0;
    std::uint32_t key_purpose = 0;
    std::vector<std::uint8_t> domain;
    std::vector<std::uint8_t> public_key;
    std::vector<std::uint8_t> signature;
    std::array<std::uint8_t, 32> digest{};
    std::vector<std::uint8_t> nonce;
    std::vector<std::uint8_t> aad;
    std::vector<std::uint8_t> input;
};

class PairKnownScheduler final
{
public:
    bool begin(const PairKnownStartV1& start);
    [[nodiscard]] std::optional<PairKnownEffect> poll_effect() const
    { return pending_; }
    fly_session_result_v2 complete(const fly_session_port_event_v2& event);
    fly_session_result_v2 accept_peer_envelope(
        std::uint8_t logical_type, const std::uint8_t* body, std::size_t size,
        const std::array<std::uint8_t, 32>& logical_hash);
    fly_session_result_v2 mark_local_sent(
        const std::array<std::uint8_t, 32>& logical_hash) noexcept;
    fly_session_result_v2 cancel_pending() noexcept;

    [[nodiscard]] bool local_envelope_ready() const noexcept
    { return local_envelope_.has_value() && !failed_; }
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> local_envelope() const
    { return local_envelope_; }
    [[nodiscard]] std::uint8_t local_message_type() const noexcept
    { return local_message_type_; }
    [[nodiscard]] bool known_path() const noexcept { return known_path_; }
    [[nodiscard]] bool ready() const noexcept { return ready_ && !failed_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::uint64_t next_operation_id() const noexcept
    { return next_operation_id_; }

private:
    enum class Stage : std::uint8_t
    {
        Empty,
        WaitPeerStatus,
        HmacLocalStatus,
        SealLocalStatus,
        OpenPeerStatus,
        HmacPeerStatus,
        WaitLocalStatusSent,
        SignLocalBranch,
        SealLocalBranch,
        WaitPeerBranch,
        OpenPeerBranch,
        VerifyPeerBranch,
        WaitLocalBranchSent,
        Ready,
        Failed
    };

    [[nodiscard]] bool local_is_initiator() const noexcept;
    [[nodiscard]] wire::PairRoleV1 peer_role() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 65>& local_public() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 65>& peer_public() const noexcept;
    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;
    fly_session_result_v2 issue(PairKnownEffect effect,
                                std::uint32_t payload_kind, Stage stage);
    fly_session_result_v2 prepare_local_status();
    fly_session_result_v2 prepare_aead(bool status, bool seal);
    fly_session_result_v2 prepare_peer_status_hmac();
    fly_session_result_v2 prepare_local_branch();
    fly_session_result_v2 prepare_peer_branch_verify();
    fly_session_result_v2 read_buffer(
        const ProviderOperationCompletion& completion,
        std::vector<std::uint8_t>& out) const;
    fly_session_result_v2 reject(fly_session_result_v2 result) noexcept;

    ProviderOperationJournal operations_{1};
    PairKnownStartV1 start_{};
    std::optional<PairKnownEffect> pending_{};
    std::optional<wire::PairSecureEnvelopeV1> peer_envelope_{};
    std::optional<std::vector<std::uint8_t>> local_envelope_{};
    std::array<std::uint8_t, 32> local_status_tag_{};
    std::array<std::uint8_t, 32> peer_status_tag_{};
    std::array<std::uint8_t, 64> local_branch_proof_{};
    std::array<std::uint8_t, 64> peer_branch_proof_{};
    std::vector<std::uint8_t> peer_status_hmac_input_{};
    std::array<std::uint8_t, 32> peer_logical_hash_{};
    std::array<std::uint8_t, 32> local_logical_hash_{};
    std::uint64_t next_operation_id_ = 0;
    std::uint8_t local_message_type_ = 0;
    Stage stage_ = Stage::Empty;
    bool begun_ = false;
    bool local_status_sent_ = false;
    bool peer_status_verified_ = false;
    bool peer_known_ = false;
    bool known_path_ = false;
    bool local_branch_sent_ = false;
    bool peer_branch_verified_ = false;
    bool ready_ = false;
    bool failed_ = false;
};

} // namespace flynes::session

#endif
