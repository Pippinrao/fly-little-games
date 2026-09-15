#ifndef FLYNES_SESSION_PAIR_AUTH_REDUCER_HPP
#define FLYNES_SESSION_PAIR_AUTH_REDUCER_HPP

#include "initial_plan_lock.hpp"
#include "wire/pair_handshake.hpp"

#include <flynes/flynes_session.h>

#include <array>
#include <cstdint>
#include <optional>

namespace flynes::session {

class PairPipeline;
class PairVerificationScheduler;

struct PairAuthStartV1
{
    std::uint64_t generation = 0;
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::array<std::uint64_t, 2> signature_operation_ids{};
    std::array<std::uint64_t, 2> key_confirm_operation_ids{};
    PairRole local_role{};
    std::uint8_t entry_mode = 0;
    bool known_path = false;
    PlanHash entry_context_hash{};
    PlanHash pair_context_hash{};
    wire::PairCommitV1 initiator_commit{};
    wire::PairCommitV1 responder_commit{};
    wire::PairContributionV1 initiator_contribution{};
    wire::PairContributionV1 responder_contribution{};
    PlanHash initiator_reveal_logical_hash{};
    PlanHash responder_reveal_logical_hash{};
    CapabilitySummary initiator_summary{};
    CapabilitySummary responder_summary{};
};

// Consumes only fenced terminal provider receipts. It never accepts a caller
// supplied "verified=true" flag. The engine must deliver these receipts only
// from the configured Crypto/Key providers for the exact outstanding request.
class PairAuthenticationReducer
{
public:
    bool begin(const PairAuthStartV1& start);
    bool approve_local(std::uint32_t approval_kind);
    bool accept_capability_reveal(PairRole sender,
                                  const CapabilitySummary& summary,
                                  const PlanHash& logical_hash);
    std::optional<VerifiedPairEvidence> take_verified_evidence();

    [[nodiscard]] const PlanHash& transcript_hash() const noexcept
    {
        return transcript_hash_;
    }
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    bool reject() noexcept;
    bool accept_provider_verification(std::uint64_t operation_id,
                                      std::uint32_t payload_kind,
                                      PairRole role);
    bool seen_operation(std::uint64_t operation_id) const noexcept;
    friend class PairPipeline;
    friend class PairVerificationScheduler;

    bool begun_ = false;
    bool failed_ = false;
    bool consumed_ = false;
    std::uint64_t generation_ = 0;
    std::array<std::uint8_t, 16> engine_instance_id_{};
    std::array<std::uint8_t, 16> link_id_{};
    std::array<std::uint64_t, 2> signature_operation_ids_{};
    std::array<std::uint64_t, 2> key_confirm_operation_ids_{};
    PairRole local_role_{};
    std::uint8_t entry_mode_ = 0;
    bool known_path_ = false;
    std::uint32_t approval_kind_ = 0;
    PlanHash transcript_hash_{};
    PlanHash initiator_reveal_{};
    PlanHash responder_reveal_{};
    PlanHash initiator_capability_{};
    PlanHash responder_capability_{};
    PlanHash expected_initiator_summary_hash_{};
    PlanHash expected_responder_summary_hash_{};
    CapabilitySummary initiator_summary_{};
    CapabilitySummary responder_summary_{};
    std::array<std::uint64_t, 4> operation_ids_{};
    std::size_t operation_count_ = 0;
    std::uint8_t signature_mask_ = 0;
    std::uint8_t key_confirm_mask_ = 0;
    std::uint8_t capability_mask_ = 0;
};

} // namespace flynes::session

#endif
