#ifndef FLYNES_SESSION_LINK_PAIR_PIPELINE_HPP
#define FLYNES_SESSION_LINK_PAIR_PIPELINE_HPP

#include "../pair_auth_reducer.hpp"
#include "../ports/provider_operation_journal.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

class PairPipelineCryptoProvider
{
public:
    virtual ~PairPipelineCryptoProvider() = default;
    virtual bool verify_signature(
        const fly_session_op_token_v2& token, PairRole role,
        const std::array<std::uint8_t, 65>& public_key,
        const PlanHash& digest,
        const std::array<std::uint8_t, 64>& signature) = 0;
    virtual bool derive_sas(const PlanHash& transcript,
                            std::array<std::uint8_t, 6>& sas) = 0;
    virtual bool verify_key_confirmation(
        const fly_session_op_token_v2& token, PairRole role,
        const PlanHash& transcript, const PlanHash& confirmation) = 0;
};

// The provider is the cryptographic trust boundary. Callers submit raw signatures and
// confirmations; only successful provider verification is converted into the reducer's
// sealed internal receipt. There is no public authenticated=true input.
class PairPipeline final
{
public:
    explicit PairPipeline(PairPipelineCryptoProvider& provider) noexcept;

    bool begin(const PairAuthStartV1& start);
    bool submit_signature(PairRole role,
                          const std::array<std::uint8_t, 64>& signature);
    std::optional<std::array<std::uint8_t, 6>> sas();
    bool approve_local(std::uint32_t approval_kind,
                       const std::array<std::uint8_t, 6>& displayed_sas);
    bool approve_local(std::uint32_t approval_kind);
    bool submit_key_confirmation(PairRole role, const PlanHash& confirmation);
    bool accept_capability(PairRole sender, const CapabilitySummary& summary,
                           const PlanHash& logical_hash);
    std::optional<VerifiedPairEvidence> take_verified_evidence();

    [[nodiscard]] const PlanHash& transcript_hash() const noexcept
    {
        return reducer_.transcript_hash();
    }
    [[nodiscard]] bool failed() const noexcept
    {
        return failed_ || reducer_.failed();
    }

private:
    bool reject() noexcept;
    bool record_provider_terminal(const fly_session_op_token_v2& token,
                                  std::uint32_t payload_kind,
                                  fly_session_result_v2 result);
    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;
    static std::size_t role_index(PairRole role) noexcept;
    static bool canonical_low_s(
        const std::array<std::uint8_t, 64>& signature) noexcept;

    PairPipelineCryptoProvider& provider_;
    ProviderOperationJournal operations_{4};
    PairAuthenticationReducer reducer_{};
    PairAuthStartV1 start_{};
    std::optional<std::array<std::uint8_t, 6>> sas_{};
    bool begun_ = false;
    bool failed_ = false;
    bool approved_ = false;
    std::uint8_t signature_mask_ = 0;
    std::uint8_t confirmation_mask_ = 0;
};

struct PairSignatureVerificationEffect final
{
    fly_session_op_token_v2 token{};
    PairRole role{};
    std::array<std::uint8_t, 65> public_key{};
    PlanHash digest{};
    std::array<std::uint8_t, 64> signature{};
};

struct PairMacVerificationEffect final
{
    fly_session_op_token_v2 token{};
    PairRole role{};
    fly_session_resource_handle_v2 key = 0;
    std::vector<std::uint8_t> exact_input;
    PlanHash expected_mac{};
};

// Public-provider bridge for the signature phase. Raw peer signatures produce a
// fenced Crypto.verify_prehashed effect. Only the exact public provider terminal
// is translated into the reducer's private sealed receipt.
class PairVerificationScheduler final
{
public:
    PairVerificationScheduler();

    bool begin(const PairAuthStartV1& start);
    fly_session_result_v2 queue_signature(
        PairRole role, const std::array<std::uint8_t, 64>& signature);
    [[nodiscard]] std::optional<PairSignatureVerificationEffect>
    poll_signature_effect() const noexcept;
    fly_session_result_v2 complete_signature(
        const fly_session_port_event_v2& event);
    bool approve_local(std::uint32_t approval_kind);
    fly_session_result_v2 queue_key_confirmation(
        PairRole role, fly_session_resource_handle_v2 key,
        const std::vector<std::uint8_t>& exact_input,
        const PlanHash& expected_mac);
    [[nodiscard]] std::optional<PairMacVerificationEffect>
    poll_key_confirmation_effect() const;
    fly_session_result_v2 complete_key_confirmation(
        const fly_session_port_event_v2& event);
    bool accept_capability(PairRole sender, const CapabilitySummary& summary,
                           const PlanHash& logical_hash);
    std::optional<VerifiedPairEvidence> take_verified_evidence();

    [[nodiscard]] const PlanHash& transcript_hash() const noexcept
    {
        return reducer_.transcript_hash();
    }
    [[nodiscard]] bool signature_verified(PairRole role) const noexcept;
    [[nodiscard]] bool signatures_verified() const noexcept
    {
        return verified_mask_ == 3;
    }
    [[nodiscard]] bool key_confirmations_verified() const noexcept
    {
        return key_confirm_verified_mask_ == 3;
    }
    [[nodiscard]] bool failed() const noexcept
    {
        return failed_ || reducer_.failed();
    }

private:
    static bool valid_role(PairRole role) noexcept;
    static std::size_t role_index(PairRole role) noexcept;
    static std::uint8_t role_bit(PairRole role) noexcept;
    static bool canonical_low_s(
        const std::array<std::uint8_t, 64>& signature) noexcept;
    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;

    ProviderOperationJournal operations_{1};
    PairAuthenticationReducer reducer_{};
    PairAuthStartV1 start_{};
    std::optional<PairSignatureVerificationEffect> pending_{};
    std::optional<PairMacVerificationEffect> pending_key_confirm_{};
    std::uint8_t queued_mask_ = 0;
    std::uint8_t verified_mask_ = 0;
    std::uint8_t key_confirm_queued_mask_ = 0;
    std::uint8_t key_confirm_verified_mask_ = 0;
    bool begun_ = false;
    bool failed_ = false;
    bool approved_ = false;
};

} // namespace flynes::session

#endif
