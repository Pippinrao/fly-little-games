#ifndef FLYNES_SESSION_INVITE_COORDINATOR_HPP
#define FLYNES_SESSION_INVITE_COORDINATOR_HPP

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace flynes::session {

inline constexpr std::uint64_t kInvitationLifetimeNs = UINT64_C(60000000000);
inline constexpr std::uint64_t kLookupRoundLifetimeNs = UINT64_C(16000000000);
inline constexpr std::uint64_t kLookupCandidateLifetimeNs = UINT64_C(2000000000);

enum class InviteCoordinatorResult : std::int32_t
{
    Ok = 0,
    Accepted = 1,
    MatchPendingApproval = 2,
    Duplicate = 3,
    Retry = 4,
    NotFound = 5,
    Invalid = -1,
    Stale = -2,
    Expired = -3,
    Consumed = -4,
    RateLimited = -5,
    Busy = -6,
    Ambiguous = -7
};

enum class InviteRouteV2 : std::uint8_t
{
    BleAnonymous = 1,
    Qr = 2
};

struct InviteChildMaterial
{
    std::array<std::uint8_t, 16> child_id{};
    std::array<std::uint8_t, 16> nonce{};
    std::array<std::uint8_t, 32> commitment{};
};

InviteCoordinatorResult invite_code_from_uniform_u32(
    std::uint32_t draw, std::array<std::uint8_t, 6>* out) noexcept;

InviteCoordinatorResult normalize_invite_code(
    std::string_view input, std::array<std::uint8_t, 6>* out) noexcept;

class InviteCoordinator
{
public:
    InviteCoordinatorResult create(std::uint64_t generation,
                                   std::uint64_t now_ns,
                                   const std::array<std::uint8_t, 6>& code,
                                   const InviteChildMaterial& ble,
                                   const InviteChildMaterial& qr) noexcept;
    InviteCoordinatorResult cancel(std::uint64_t generation) noexcept;
    InviteCoordinatorResult lookup(const std::array<std::uint8_t, 6>& code,
                                   std::uint64_t now_ns) noexcept;
    InviteCoordinatorResult accept(std::uint64_t generation,
                                   InviteRouteV2 route,
                                   const std::array<std::uint8_t, 16>& child_id,
                                   std::uint64_t now_ns) noexcept;

    InviteCoordinatorResult begin_join_submission(std::uint64_t now_ns);
    InviteCoordinatorResult begin_host_lookup(std::uint64_t gatt_generation,
                                              std::uint64_t now_ns);
    void end_host_lookup(std::uint64_t gatt_generation) noexcept;

    [[nodiscard]] std::uint64_t deadline_ns() const noexcept { return deadline_ns_; }
    [[nodiscard]] bool route_active(InviteRouteV2 route) const noexcept;
    [[nodiscard]] constexpr bool produces_verified_pair_evidence() const noexcept
    {
        return false;
    }

private:
    static void prune_window(std::vector<std::uint64_t>& timestamps,
                             std::uint64_t now_ns) noexcept;

    std::uint64_t generation_ = 0;
    std::uint64_t deadline_ns_ = 0;
    std::array<std::uint8_t, 6> code_{};
    InviteChildMaterial ble_{};
    InviteChildMaterial qr_{};
    bool active_ = false;
    bool consumed_ = false;
    std::vector<std::uint64_t> join_submissions_;
    std::vector<std::uint64_t> host_lookups_;
    std::vector<std::uint64_t> inflight_gatt_;
};

class JoinLookupRound
{
public:
    InviteCoordinatorResult start(std::uint64_t submission_id,
                                  std::uint64_t now_ns,
                                  std::uint64_t invitation_deadline_ns,
                                  const std::vector<std::uint64_t>& candidates);
    InviteCoordinatorResult next_candidate(std::uint64_t now_ns,
                                           std::uint64_t* handle,
                                           std::uint64_t* candidate_deadline_ns) noexcept;
    InviteCoordinatorResult record_match(
        std::uint64_t candidate_handle,
        const std::array<std::uint8_t, 32>& pair_context_hash) noexcept;

    [[nodiscard]] std::uint64_t deadline_ns() const noexcept { return deadline_ns_; }

private:
    std::uint64_t submission_id_ = 0;
    std::uint64_t deadline_ns_ = 0;
    std::vector<std::uint64_t> candidates_;
    std::size_t next_index_ = 0;
    std::array<std::uint8_t, 32> matched_context_{};
    bool has_match_ = false;
    bool active_ = false;
};

} // namespace flynes::session

#endif
