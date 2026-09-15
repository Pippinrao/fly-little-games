#ifndef FLYNES_SESSION_LINK_PAIR_CAPABILITY_SCHEDULER_HPP
#define FLYNES_SESSION_LINK_PAIR_CAPABILITY_SCHEDULER_HPP

#include "pair_signature_scheduler.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

enum class PairCapabilityEffectKind : std::uint8_t
{
    Hmac,
    AeadSeal,
    AeadOpen
};

struct PairCapabilityStartV1
{
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t generation = 0;
    std::uint64_t first_operation_id = 0;
    wire::PairRoleV1 local_role{};
    std::array<std::uint8_t, 32> transcript_hash{};
    CapabilitySummary local_summary{};
    fly_session_resource_handle_v2 gatt_i2r_key = 0;
    fly_session_resource_handle_v2 gatt_r2i_key = 0;
    fly_session_resource_handle_v2 control_i2r_key = 0;
    fly_session_resource_handle_v2 control_r2i_key = 0;
};

struct PairCapabilityEffect final
{
    PairCapabilityEffectKind kind{};
    fly_session_op_token_v2 token{};
    fly_session_resource_handle_v2 resource = 0;
    std::vector<std::uint8_t> nonce;
    std::vector<std::uint8_t> aad;
    std::vector<std::uint8_t> input;
};

class PairCapabilityScheduler final
{
public:
    explicit PairCapabilityScheduler(PairSignatureScheduler& verification)
        noexcept : verification_(&verification) {}
    bool begin(const PairCapabilityStartV1& start);
    [[nodiscard]] std::optional<PairCapabilityEffect> poll_effect() const
    { return pending_; }
    fly_session_result_v2 complete(const fly_session_port_event_v2& event);
    fly_session_result_v2 accept_peer_envelope(
        const std::uint8_t* body, std::size_t size,
        const std::array<std::uint8_t, 32>& logical_hash);
    fly_session_result_v2 mark_local_sent(
        const std::array<std::uint8_t, 32>& logical_hash) noexcept;
    fly_session_result_v2 cancel_pending() noexcept;

    [[nodiscard]] bool local_envelope_ready() const noexcept
    { return local_envelope_.has_value() && !failed_; }
    [[nodiscard]] bool local_sent() const noexcept { return local_sent_; }
    [[nodiscard]] bool ready() const noexcept { return ready_ && !failed_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> local_envelope() const
    { return local_envelope_; }
    [[nodiscard]] std::optional<CapabilitySummary> peer_summary() const
    { return peer_summary_; }
    [[nodiscard]] const std::array<std::uint8_t, 32>& local_logical_hash() const
        noexcept { return local_logical_hash_; }
    [[nodiscard]] const std::array<std::uint8_t, 32>& peer_logical_hash() const
        noexcept { return peer_logical_hash_; }
    [[nodiscard]] std::uint64_t next_operation_id() const noexcept
    { return next_operation_id_; }

private:
    enum class Stage : std::uint8_t
    {
        Empty, WaitPeer, HmacLocal, SealLocal, OpenPeer, HmacPeer,
        WaitLocalSent, Ready, Failed
    };
    bool local_is_initiator() const noexcept;
    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;
    fly_session_result_v2 prepare_local_hmac();
    fly_session_result_v2 prepare_aead(bool seal);
    fly_session_result_v2 prepare_peer_hmac();
    fly_session_result_v2 reject(fly_session_result_v2 result) noexcept;
    fly_session_result_v2 read_buffer(
        const ProviderOperationCompletion& completion,
        std::vector<std::uint8_t>& out) const;

    PairSignatureScheduler* verification_ = nullptr;
    ProviderOperationJournal operations_{1};
    PairCapabilityStartV1 start_{};
    std::optional<PairCapabilityEffect> pending_{};
    std::optional<wire::PairSecureEnvelopeV1> peer_envelope_{};
    std::optional<std::vector<std::uint8_t>> local_envelope_{};
    std::optional<CapabilitySummary> peer_summary_{};
    std::array<std::uint8_t, 32> peer_tag_{};
    std::array<std::uint8_t, 32> peer_logical_hash_{};
    std::array<std::uint8_t, 32> local_logical_hash_{};
    std::array<std::uint8_t, 32> local_tag_{};
    std::vector<std::uint8_t> local_hmac_input_{};
    std::vector<std::uint8_t> peer_hmac_input_{};
    std::uint64_t next_operation_id_ = 0;
    Stage stage_ = Stage::Empty;
    bool begun_ = false;
    bool local_sent_ = false;
    bool peer_verified_ = false;
    bool ready_ = false;
    bool failed_ = false;
};

} // namespace flynes::session

#endif
