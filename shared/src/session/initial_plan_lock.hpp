#ifndef FLYNES_SESSION_INITIAL_PLAN_LOCK_HPP
#define FLYNES_SESSION_INITIAL_PLAN_LOCK_HPP

#include "wire/pair_capability.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace flynes::session {

using PlanHash = std::array<std::uint8_t, 32>;
using CapabilitySummary = std::array<std::uint8_t, 512>;
enum class PairRole : std::uint8_t { Initiator = 1, Responder = 2 };

// INTERNAL TRUST BOUNDARY: caller has verified the full PairTranscript's two
// signatures, current route/KNOWN policy, both human approvals and KEY_CONFIRM,
// both reveal envelopes, contribution summary commitments, and certification
// eligibility of the exact records. Raw network input must NEVER construct this
// evidence directly. This reducer implements neither crypto nor those gates.
struct VerifiedPairEvidence {
    PairRole local_role{};
    std::uint64_t generation = 0;
    PlanHash transcript{};
    PlanHash initiator_reveal{};
    PlanHash responder_reveal{};
    CapabilitySummary initiator_summary{};
    CapabilitySummary responder_summary{};
};

// Each logical hash is an immutable content-addressed reference to the complete
// exact GATTLogicalMessage (including its already-created secure envelope), NOT
// an inner/summary/ciphertext hash. The trusted adapter owns these bounded bytes,
// verifies reference resolution against the full message hash and all fields,
// and retains them for this attempt. Persist commands must durably persist those
// exact bytes and the indicated lock atomically before reporting success; storing
// a hash alone does not satisfy a persistence command. Send commands resolve and
// reuse those same envelope bytes; no re-encryption or new logical message.
// Locally prepared evidence has the same verification requirements as received
// evidence. Plan/ACK/FINAL are supplied incrementally; future hashes stay zero.
struct VerifiedPlanEvidence {
    std::uint64_t generation = 0;
    PairRole sender{};
    PairRole receiver{};
    PlanHash transcript{};
    PlanHash initiator_reveal{};
    PlanHash responder_reveal{};
    wire::BearerPlanBytes selected_plan{};
    PlanHash selected_plan_hash{};
    PlanHash plan_logical_hash{};
    PlanHash ack_logical_hash{};
    PlanHash final_logical_hash{};
};

struct VerifiedCredentialEvidence {
    // Creator supplies locally prepared bytes only after CreateBearer success;
    // noncreator supplies verified received bytes only after mutual lock.
    VerifiedPlanEvidence binding{};
    PlanHash credential_logical_hash{};
};

enum class InitialPlanCommandKind {
    PersistPlan, PersistPlanAndLock, SendPlan,
    PersistAck, PersistAckAndLock, SendAck,
    PersistFinal, SendFinal, PersistMutualLock,
    PersistPromptConsumed, CreateBearer, PublishCredentials,
    PersistCredentials, JoinBearer
};

struct InitialPlanCommand {
    // Local executor idempotency token only: NEVER a wire transition_id.
    std::uint64_t id = 0;
    std::uint64_t generation = 0;
    InitialPlanCommandKind kind{};
    VerifiedPlanEvidence evidence{};
    PlanHash credential_logical_hash{};
    // Only CreateBearer/JoinBearer may carry a consumed prompt allowance. Every
    // other operation MUST avoid a system prompt. After CreateBearer success,
    // the creator may prepare exact encrypted type8 bytes and submit evidence.
    // PublishCredentials sends only the resulting persisted credential reference;
    // its bearer/creator/listener/codec must exactly match evidence.selected_plan.
    bool may_prompt = false;
};

// One initial pairing attempt, fixed-size state, no I/O, no command queue.
// Polling repeats the same command; executor must deduplicate by (instance,id).
// Send success means acceptance by this generation's ordered GATT send path,
// NEVER receipt of a physical ACK. Physical ACKs have no reducer entry point.
// Any malformed, stale, out-of-order or failed operation invalidates the attempt.
// GATT reconnect/cancel is terminal; authenticated game recovery is out of scope.
class InitialPlanLock {
public:
    InitialPlanLock() = default;
    InitialPlanLock(const InitialPlanLock&) = delete;
    InitialPlanLock& operator=(const InitialPlanLock&) = delete;
    bool begin(const VerifiedPairEvidence& evidence);
    bool accept_plan(const VerifiedPlanEvidence& evidence);
    bool accept_ack(const VerifiedPlanEvidence& evidence);
    bool accept_final(const VerifiedPlanEvidence& evidence);
    bool accept_credentials(const VerifiedCredentialEvidence& evidence);
    std::optional<InitialPlanCommand> poll() const;
    bool complete(std::uint64_t id, std::uint64_t generation, bool success);
    void invalidate() noexcept;
    bool failed() const noexcept { return failed_; }
    bool not_supported() const noexcept { return not_supported_; }
    // Historical durable facts survive invalidation; only a live outstanding
    // command authorizes an adapter effect. These getters are not authorization.
    bool locked() const noexcept { return locked_; }
    bool mutually_locked() const noexcept { return mutually_locked_; }
    bool prompt_consumed() const noexcept { return prompt_consumed_; }
    const wire::BearerPlanBytes& selected_plan() const noexcept { return selected_; }

private:
    enum class Phase { Idle, Plan, Ack, Final, Credentials, Pending, Done };
    bool reject() noexcept;
    bool valid_binding(const VerifiedPlanEvidence& evidence, PairRole sender) const;
    bool issue(InitialPlanCommandKind kind, bool may_prompt = false);
    bool authorize_bearer();
    bool issue_bearer();
    Phase phase_ = Phase::Idle;
    bool failed_ = false;
    bool not_supported_ = false;
    bool locked_ = false;
    bool mutually_locked_ = false;
    bool prompt_consumed_ = false;
    std::uint64_t next_id_ = 1;
    VerifiedPairEvidence pair_{};
    VerifiedPlanEvidence evidence_{};
    PlanHash credential_hash_{};
    wire::BearerPlanBytes selected_{};
    std::optional<InitialPlanCommand> pending_{};
};

} // namespace flynes::session
#endif
