#ifndef FLYNES_SESSION_WIRE_PAIR_SECURE_HPP
#define FLYNES_SESSION_WIRE_PAIR_SECURE_HPP

#include "pair_handshake.hpp"
#include "pair_capability.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace flynes::session::wire {

constexpr std::size_t kPairSignatureInnerSizeV1 = 176;
constexpr std::size_t kKeyConfirmBodySizeV1 = 112;
constexpr std::size_t kKeyConfirmInnerSizeV1 = 144;
constexpr std::size_t kKnownStatusBodySizeV1 = 112;
constexpr std::size_t kKnownStatusInnerSizeV1 = 144;
constexpr std::size_t kKnownBranchBodySizeV1 = 112;
constexpr std::size_t kKnownBranchInnerSizeV1 = 176;
constexpr std::size_t kPairCapabilityPretagSizeV1 = 560;
constexpr std::size_t kPairCapabilityInnerSizeV1 = 592;
constexpr std::size_t kInitialBearerPlanPretagSizeV1 = 176;
constexpr std::size_t kInitialBearerPlanInnerSizeV1 = 208;
constexpr std::size_t kInitialBearerPlanAckPretagSizeV1 = 112;
constexpr std::size_t kInitialBearerPlanAckInnerSizeV1 = 144;
constexpr std::size_t kInitialBearerPlanFinalPretagSizeV1 = 144;
constexpr std::size_t kInitialBearerPlanFinalInnerSizeV1 = 176;
constexpr std::size_t kPairSecureAadSizeV1 = 68;
constexpr std::size_t kPairSecureNonceSizeV1 = 12;

struct PairSignatureInnerV1
{
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    PairRoleV1 sender{};
    PairRoleV1 receiver{};
    std::array<std::uint8_t, 32> sender_key_id{};
    std::array<std::uint8_t, 32> receiver_key_id{};
    std::array<std::uint8_t, 64> signature{};
};

struct PairSecureEnvelopeV1
{
    std::uint64_t message_counter = 0;
    std::vector<std::uint8_t> ciphertext_and_tag;
};

struct KeyConfirmInnerV1
{
    std::uint8_t entry_mode = 0;
    std::uint8_t approval_kind = 0;
    PairRoleV1 sender{};
    PairRoleV1 receiver{};
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    std::array<std::uint8_t, 32> sender_key_id{};
    std::array<std::uint8_t, 32> receiver_key_id{};
    std::array<std::uint8_t, 32> tag{};
};

struct KnownStatusInnerV1
{
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    PairRoleV1 sender{};
    PairRoleV1 receiver{};
    bool known = false;
    std::array<std::uint8_t, 32> sender_key_id{};
    std::array<std::uint8_t, 32> peer_key_id{};
    std::array<std::uint8_t, 32> tag{};
};

enum class KnownBranchKindV1 : std::uint8_t
{
    Verified = 1,
    SasFallback = 2
};

struct KnownBranchInnerV1
{
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    PairRoleV1 sender{};
    PairRoleV1 receiver{};
    KnownBranchKindV1 kind{};
    std::array<std::uint8_t, 32> sender_key_id{};
    std::array<std::uint8_t, 32> peer_key_id{};
    std::array<std::uint8_t, 64> proof{};
};

struct PairCapabilityInnerV1
{
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    PairRoleV1 sender{};
    PairRoleV1 receiver{};
    std::array<std::uint8_t, 512> summary{};
    std::array<std::uint8_t, 32> tag{};
};

struct InitialBearerPlanInnerV1
{
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    std::array<std::uint8_t, 32> initiator_capability_logical_hash{};
    std::array<std::uint8_t, 32> responder_capability_logical_hash{};
    BearerPlanBytes selected_plan{};
    std::array<std::uint8_t, 16> plan_nonce{};
    PairRoleV1 sender{};
    PairRoleV1 receiver{};
    std::array<std::uint8_t, 32> tag{};
};

struct InitialBearerPlanAckInnerV1
{
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    std::array<std::uint8_t, 32> plan_logical_hash{};
    std::array<std::uint8_t, 32> selected_plan_hash{};
    PairRoleV1 sender{};
    PairRoleV1 receiver{};
    std::array<std::uint8_t, 32> tag{};
};

struct InitialBearerPlanFinalInnerV1
{
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    std::array<std::uint8_t, 32> plan_logical_hash{};
    std::array<std::uint8_t, 32> selected_plan_hash{};
    std::array<std::uint8_t, 32> plan_ack_logical_hash{};
    PairRoleV1 sender{};
    PairRoleV1 receiver{};
    std::array<std::uint8_t, 32> tag{};
};

Status identity_key_id_v1(
    const std::array<std::uint8_t, 65>& public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, 32>* out) noexcept;

Status encode_pair_signature_inner_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    PairRoleV1 sender,
    const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& receiver_public_key,
    const std::array<std::uint8_t, 64>& signature,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, kPairSignatureInnerSizeV1>* out) noexcept;

Status decode_pair_signature_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    PairRoleV1 expected_sender,
    const std::array<std::uint8_t, 65>& expected_sender_public_key,
    const std::array<std::uint8_t, 65>& expected_receiver_public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    PairSignatureInnerV1* out) noexcept;

Status encode_key_confirm_inner_v1(
    std::uint8_t entry_mode, std::uint8_t approval_kind,
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    PairRoleV1 sender,
    const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& receiver_public_key,
    const std::array<std::uint8_t, 32>& tag,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, kKeyConfirmInnerSizeV1>* out) noexcept;

Status decode_key_confirm_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    std::uint8_t expected_entry_mode, std::uint8_t expected_approval_kind,
    const std::array<std::uint8_t, 32>& expected_pair_transcript_hash,
    PairRoleV1 expected_sender,
    const std::array<std::uint8_t, 65>& expected_sender_public_key,
    const std::array<std::uint8_t, 65>& expected_receiver_public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    KeyConfirmInnerV1* out) noexcept;

Status build_key_confirm_hmac_input_v1(
    const std::uint8_t* body, std::size_t size,
    std::vector<std::uint8_t>* out);

Status encode_known_status_inner_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    PairRoleV1 sender, bool known,
    const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& peer_public_key,
    const std::array<std::uint8_t, 32>& tag,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, kKnownStatusInnerSizeV1>* out) noexcept;
Status decode_known_status_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    PairRoleV1 expected_sender,
    const std::array<std::uint8_t, 65>& expected_sender_public_key,
    const std::array<std::uint8_t, 65>& expected_peer_public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    KnownStatusInnerV1* out) noexcept;
Status build_known_status_hmac_input_v1(
    const std::uint8_t* body, std::size_t size,
    std::vector<std::uint8_t>* out);

Status encode_known_branch_inner_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    PairRoleV1 sender, KnownBranchKindV1 kind,
    const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& peer_public_key,
    const std::array<std::uint8_t, 64>& proof,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, kKnownBranchInnerSizeV1>* out) noexcept;
Status decode_known_branch_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    PairRoleV1 expected_sender, KnownBranchKindV1 expected_kind,
    const std::array<std::uint8_t, 65>& expected_sender_public_key,
    const std::array<std::uint8_t, 65>& expected_peer_public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    KnownBranchInnerV1* out) noexcept;
Status known_branch_digest_v1(
    const std::uint8_t* body, std::size_t size,
    std::array<std::uint8_t, 32>* out) noexcept;

Status encode_pair_capability_inner_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    PairRoleV1 sender, const std::array<std::uint8_t, 512>& summary,
    const std::array<std::uint8_t, 32>& tag,
    std::array<std::uint8_t, kPairCapabilityInnerSizeV1>* out) noexcept;

Status decode_pair_capability_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_pair_transcript_hash,
    PairRoleV1 expected_sender, PairCapabilityInnerV1* out) noexcept;

Status build_pair_capability_hmac_input_v1(
    const std::uint8_t* pretag, std::size_t size,
    std::vector<std::uint8_t>* out);

Status encode_initial_bearer_plan_inner_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 32>& initiator_capability_logical_hash,
    const std::array<std::uint8_t, 32>& responder_capability_logical_hash,
    const BearerPlanBytes& selected_plan,
    const std::array<std::uint8_t, 16>& plan_nonce,
    const std::array<std::uint8_t, 32>& tag,
    std::array<std::uint8_t, kInitialBearerPlanInnerSizeV1>* out) noexcept;
Status decode_initial_bearer_plan_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    const std::array<std::uint8_t, 32>& expected_initiator_capability_hash,
    const std::array<std::uint8_t, 32>& expected_responder_capability_hash,
    const BearerPlanBytes& expected_plan,
    InitialBearerPlanInnerV1* out) noexcept;
Status build_initial_bearer_plan_hmac_input_v1(
    const std::uint8_t* pretag, std::size_t size,
    std::vector<std::uint8_t>* out);

Status encode_initial_bearer_plan_ack_inner_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 32>& plan_logical_hash,
    const std::array<std::uint8_t, 32>& selected_plan_hash,
    const std::array<std::uint8_t, 32>& tag,
    std::array<std::uint8_t, kInitialBearerPlanAckInnerSizeV1>* out) noexcept;
Status decode_initial_bearer_plan_ack_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    const std::array<std::uint8_t, 32>& expected_plan_logical_hash,
    const std::array<std::uint8_t, 32>& expected_selected_plan_hash,
    InitialBearerPlanAckInnerV1* out) noexcept;
Status build_initial_bearer_plan_ack_hmac_input_v1(
    const std::uint8_t* pretag, std::size_t size,
    std::vector<std::uint8_t>* out);

Status encode_initial_bearer_plan_final_inner_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 32>& plan_logical_hash,
    const std::array<std::uint8_t, 32>& selected_plan_hash,
    const std::array<std::uint8_t, 32>& plan_ack_logical_hash,
    const std::array<std::uint8_t, 32>& tag,
    std::array<std::uint8_t, kInitialBearerPlanFinalInnerSizeV1>* out) noexcept;
Status decode_initial_bearer_plan_final_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    const std::array<std::uint8_t, 32>& expected_plan_logical_hash,
    const std::array<std::uint8_t, 32>& expected_selected_plan_hash,
    const std::array<std::uint8_t, 32>& expected_plan_ack_logical_hash,
    InitialBearerPlanFinalInnerV1* out) noexcept;
Status build_initial_bearer_plan_final_hmac_input_v1(
    const std::uint8_t* pretag, std::size_t size,
    std::vector<std::uint8_t>* out);

Status pair_secure_inner_size_v1(std::uint8_t logical_type,
                                 std::size_t* out) noexcept;

Status build_pair_secure_nonce_v1(
    PairRoleV1 sender, std::uint64_t message_counter,
    std::array<std::uint8_t, kPairSecureNonceSizeV1>* out) noexcept;

Status build_pair_secure_aad_v1(
    std::uint8_t logical_type, PairRoleV1 sender,
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    std::uint64_t message_counter,
    std::array<std::uint8_t, kPairSecureAadSizeV1>* out) noexcept;

Status encode_pair_secure_envelope_v1(
    std::uint8_t logical_type, std::uint64_t message_counter,
    const std::uint8_t* ciphertext_and_tag, std::size_t size,
    std::vector<std::uint8_t>* out);

Status decode_pair_secure_envelope_v1(
    std::uint8_t logical_type, const std::uint8_t* bytes, std::size_t size,
    PairSecureEnvelopeV1* out);

} // namespace flynes::session::wire

#endif
