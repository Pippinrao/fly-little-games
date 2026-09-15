#ifndef FLYNES_SESSION_LINK_PAIR_SIGNATURE_SCHEDULER_HPP
#define FLYNES_SESSION_LINK_PAIR_SIGNATURE_SCHEDULER_HPP

#include "pair_pipeline.hpp"
#include "pair_reveal_scheduler.hpp"
#include "../wire/pair_secure.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

enum class PairSignatureEffectKind : std::uint8_t
{
    DeriveKey,
    Sign,
    Verify,
    AeadSeal,
    AeadOpen,
    PersistTranscript
};

struct PairSignatureStartV1
{
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t generation = 0;
    std::uint64_t first_operation_id = 0;
    wire::PairRoleV1 local_role{};
    fly_session_resource_handle_v2 local_identity_key = 0;
    fly_session_resource_handle_v2 ecdh_secret = 0;
    wire::PairContextV1 context{};
    wire::PairCommitV1 initiator_commit{};
    wire::PairCommitV1 responder_commit{};
    wire::PairContributionV1 initiator_contribution{};
    wire::PairContributionV1 responder_contribution{};
    std::array<std::uint8_t, 32> initiator_reveal_logical_hash{};
    std::array<std::uint8_t, 32> responder_reveal_logical_hash{};
};

struct PairSignatureEffect final
{
    PairSignatureEffectKind kind{};
    fly_session_op_token_v2 token{};
    fly_session_resource_handle_v2 resource = 0;
    std::uint32_t key_purpose = 0;
    std::uint32_t byte_count = 0;
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> info;
    std::vector<std::uint8_t> domain;
    std::vector<std::uint8_t> public_key;
    std::vector<std::uint8_t> signature;
    std::array<std::uint8_t, 32> digest{};
    std::array<std::uint8_t, 32> expected_hash{};
    std::uint32_t object_kind = 0;
    std::vector<std::uint8_t> nonce;
    std::vector<std::uint8_t> aad;
    std::vector<std::uint8_t> input;
};

struct PairSignatureSecretsV1
{
    fly_session_resource_handle_v2 control_i2r_key = 0;
    fly_session_resource_handle_v2 control_r2i_key = 0;
};

class PairSignatureScheduler final
{
public:
    PairSignatureScheduler();
    bool begin(const PairSignatureStartV1& start);
    [[nodiscard]] std::optional<PairSignatureEffect> poll_effect() const;
    fly_session_result_v2 complete(const fly_session_port_event_v2& event);
    fly_session_result_v2 accept_peer_envelope(
        const std::uint8_t* body, std::size_t size,
        const std::array<std::uint8_t, 32>& logical_hash);
    fly_session_result_v2 mark_local_signature_sent() noexcept;
    fly_session_result_v2 cancel_pending() noexcept;
    bool approve_local(std::uint32_t approval_kind) noexcept;
    fly_session_result_v2 queue_key_confirmation(
        PairRole role, fly_session_resource_handle_v2 key,
        const std::vector<std::uint8_t>& exact_input,
        const std::array<std::uint8_t, 32>& expected_mac);
    [[nodiscard]] std::optional<PairMacVerificationEffect>
    poll_key_confirmation_effect() const;
    fly_session_result_v2 complete_key_confirmation(
        const fly_session_port_event_v2& event);
    bool accept_capability(PairRole sender, const CapabilitySummary& summary,
                           const std::array<std::uint8_t, 32>& logical_hash);
    std::optional<VerifiedPairEvidence> take_verified_evidence();

    [[nodiscard]] bool local_envelope_ready() const noexcept
    { return local_envelope_.has_value() && !failed_; }
    [[nodiscard]] bool local_signature_sent() const noexcept
    { return local_sent_; }
    [[nodiscard]] bool ready() const noexcept { return ready_ && !failed_; }
    [[nodiscard]] bool local_approved() const noexcept { return approved_; }
    [[nodiscard]] bool key_confirmations_verified() const noexcept
    { return verification_.key_confirmations_verified(); }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] const std::array<std::uint8_t, 32>& transcript_hash() const noexcept
    { return transcript_hash_; }
    [[nodiscard]] const std::array<std::uint8_t, 32>& transcript_object_hash() const noexcept
    { return transcript_object_hash_; }
    [[nodiscard]] fly_session_resource_handle_v2 transcript_object_ref() const noexcept
    { return transcript_object_ref_; }
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> local_envelope() const
    { return local_envelope_; }
    [[nodiscard]] PairSignatureSecretsV1 owned_secrets() const noexcept
    { return secrets_; }

private:
    enum class Stage : std::uint8_t
    {
        Empty,
        DeriveI2r,
        DeriveR2i,
        WaitPeer,
        OpenPeer,
        SignLocal,
        VerifySignature,
        SealLocal,
        WaitLocalSent,
        PersistTranscript,
        Ready,
        Failed
    };

    bool local_is_initiator() const noexcept;
    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;
    fly_session_result_v2 prepare(Stage stage);
    fly_session_result_v2 prepare_verify(
        PairRole role, const std::array<std::uint8_t, 64>& signature);
    fly_session_result_v2 prepare_transcript_persist();
    fly_session_result_v2 reject(fly_session_result_v2 result) noexcept;
    fly_session_result_v2 read_buffer(
        const ProviderOperationCompletion& completion,
        std::vector<std::uint8_t>& out) const;

    ProviderOperationJournal operations_{1};
    PairVerificationScheduler verification_{};
    PairSignatureStartV1 start_{};
    PairSignatureSecretsV1 secrets_{};
    std::optional<PairSignatureEffect> pending_{};
    std::optional<wire::PairSecureEnvelopeV1> peer_envelope_{};
    std::optional<std::vector<std::uint8_t>> local_envelope_{};
    std::array<std::uint8_t, 32> peer_logical_hash_{};
    std::array<std::uint8_t, 32> transcript_hash_{};
    std::array<std::uint8_t, 64> local_signature_{};
    std::array<std::uint8_t, 64> verifying_signature_{};
    std::array<std::uint8_t, 64> initiator_signature_{};
    std::array<std::uint8_t, 64> responder_signature_{};
    std::array<std::uint8_t, 32> transcript_object_hash_{};
    fly_session_resource_handle_v2 transcript_object_ref_ = 0;
    std::uint64_t next_operation_id_ = 0;
    Stage stage_ = Stage::Empty;
    PairRole verifying_role_{};
    bool begun_ = false;
    bool local_sent_ = false;
    bool ready_ = false;
    bool approved_ = false;
    bool failed_ = false;
};

} // namespace flynes::session

#endif
