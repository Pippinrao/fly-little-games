#ifndef FLYNES_SESSION_WIRE_PAIR_HANDSHAKE_HPP
#define FLYNES_SESSION_WIRE_PAIR_HANDSHAKE_HPP

#include "pair_crypto.hpp"
#include "session_codec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::session::wire {

enum class PairRoleV1 : std::uint8_t
{
    Initiator = 1,
    Responder = 2
};

using P256PointValidatorV1 = bool (*)(void* context,
                                      const std::uint8_t point_x963[65]);

struct PairContextV1
{
    std::array<std::uint8_t, 80> bytes{};
    std::array<std::uint8_t, 32> hash{};
};

struct PairContributionV1
{
    PairRoleV1 role{};
    std::array<std::uint8_t, 320> bytes{};
    std::array<std::uint8_t, 32> commitment{};
    std::array<std::uint8_t, 65> identity_public_key{};
    std::array<std::uint8_t, 65> ephemeral_public_key{};
    std::array<std::uint8_t, 32> tls_spki_hash{};
    std::array<std::uint8_t, 32> capability_summary_hash{};
};

struct PairCommitV1
{
    PairRoleV1 sender{};
    PairRoleV1 receiver{};
    std::array<std::uint8_t, 32> pair_context_hash{};
    std::array<std::uint8_t, 32> commitment{};
    std::array<std::uint8_t, 65> ephemeral_public_key{};
};

enum class PairExchangeResultV1 : std::int32_t
{
    Accepted = 1,
    Ready = 2,
    InvalidState = -1,
    ProtocolViolation = -2
};

// Collects the canonical pairing prelude in its single legal wire order:
// PairContext, initiator commit, responder commit, initiator reveal, responder
// reveal. GATT duplicate suppression happens before this reducer; seeing a
// second or reflected phase here is therefore a protocol violation.
class PairExchangeV1 final
{
public:
    PairExchangeV1(P256PointValidatorV1 validate_point,
                   void* validator_context) noexcept;

    PairExchangeResultV1 begin(const std::uint8_t* bytes,
                               std::size_t size) noexcept;
    PairExchangeResultV1 accept_commit(PairRoleV1 sender,
                                       const std::uint8_t* bytes,
                                       std::size_t size) noexcept;
    PairExchangeResultV1 accept_reveal(
        PairRoleV1 sender, const std::uint8_t* bytes, std::size_t size,
        const std::array<std::uint8_t, 32>& logical_hash) noexcept;

    [[nodiscard]] bool ready() const noexcept { return phase_ == Phase::Ready; }
    [[nodiscard]] bool failed() const noexcept { return phase_ == Phase::Failed; }
    [[nodiscard]] bool initiator_commit_received() const noexcept
    {
        return phase_ == Phase::ResponderCommit ||
               phase_ == Phase::InitiatorReveal ||
               phase_ == Phase::ResponderReveal || phase_ == Phase::Ready;
    }
    [[nodiscard]] const PairContextV1& context() const noexcept { return context_; }
    [[nodiscard]] const PairCommitV1& initiator_commit() const noexcept
    {
        return commits_[0];
    }
    [[nodiscard]] const PairCommitV1& responder_commit() const noexcept
    {
        return commits_[1];
    }
    [[nodiscard]] const PairContributionV1& initiator_contribution() const noexcept
    {
        return contributions_[0];
    }
    [[nodiscard]] const PairContributionV1& responder_contribution() const noexcept
    {
        return contributions_[1];
    }
    [[nodiscard]] const std::array<std::uint8_t, 32>&
    initiator_reveal_hash() const noexcept { return reveal_hashes_[0]; }
    [[nodiscard]] const std::array<std::uint8_t, 32>&
    responder_reveal_hash() const noexcept { return reveal_hashes_[1]; }

private:
    enum class Phase : std::uint8_t
    {
        Empty,
        InitiatorCommit,
        ResponderCommit,
        InitiatorReveal,
        ResponderReveal,
        Ready,
        Failed
    };

    PairExchangeResultV1 reject() noexcept;
    static bool nonzero(const std::array<std::uint8_t, 32>& value) noexcept;

    P256PointValidatorV1 validate_point_ = nullptr;
    void* validator_context_ = nullptr;
    Phase phase_ = Phase::Empty;
    PairContextV1 context_{};
    std::array<PairCommitV1, 2> commits_{};
    std::array<PairContributionV1, 2> contributions_{};
    std::array<std::array<std::uint8_t, 32>, 2> reveal_hashes_{};
};

Status decode_pair_context_v1(const std::uint8_t* bytes, std::size_t size,
                              PairContextV1* out) noexcept;

Status encode_pair_contribution_v1(
    const PairContextV1& context, PairRoleV1 role,
    const std::array<std::uint8_t, 65>& identity_public_key,
    const std::array<std::uint8_t, 65>& ephemeral_public_key,
    const std::array<std::uint8_t, 32>& tls_spki_hash,
    const std::array<std::uint8_t, 32>& contribution_nonce,
    const std::array<std::uint8_t, 32>& capability_summary_hash,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, 320>* out) noexcept;

Status encode_pair_commit_v1(
    const PairContextV1& context, const PairContributionV1& contribution,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, 152>* out) noexcept;

Status decode_pair_contribution_v1(
    const std::uint8_t* bytes, std::size_t size,
    const PairContextV1& expected_context, PairRoleV1 expected_role,
    P256PointValidatorV1 validate_point, void* validator_context,
    PairContributionV1* out) noexcept;

Status decode_pair_commit_v1(
    const std::uint8_t* bytes, std::size_t size,
    const PairContextV1& expected_context, PairRoleV1 expected_sender,
    P256PointValidatorV1 validate_point, void* validator_context,
    PairCommitV1* out) noexcept;

Status bind_reveal_to_commit_v1(const PairCommitV1& commit,
                                const PairContributionV1& reveal) noexcept;

Status build_pair_transcript_preimage_v1(
    std::uint8_t entry_mode,
    const std::array<std::uint8_t, 32>& entry_context_hash,
    const std::array<std::uint8_t, 32>& initiator_commitment,
    const std::array<std::uint8_t, 32>& responder_commitment,
    const PairContributionV1& initiator,
    const PairContributionV1& responder,
    std::array<std::uint8_t, 752>* out_preimage,
    std::array<std::uint8_t, 32>* out_hash) noexcept;

inline constexpr std::size_t kPairTranscriptPreimageSizeV1 = 752;
inline constexpr std::size_t kPairTranscriptSizeV1 = 880;
inline constexpr std::uint16_t kPairTranscriptObjectKindV1 = 0x0213;

Status build_pair_transcript_object_v1(
    const std::array<std::uint8_t, kPairTranscriptPreimageSizeV1>& preimage,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    const std::array<std::uint8_t, 64>& initiator_signature,
    const std::array<std::uint8_t, 64>& responder_signature,
    std::array<std::uint8_t, kPairTranscriptSizeV1>* out_transcript,
    std::array<std::uint8_t, 32>* out_object_hash) noexcept;

} // namespace flynes::session::wire

#endif
