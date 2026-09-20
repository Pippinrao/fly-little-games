#include "invite_coordinator.hpp"

#include <algorithm>
#include <limits>

namespace flynes::session {
namespace {

template <std::size_t N>
bool nonzero(const std::array<std::uint8_t, N>& bytes) noexcept
{
    return std::any_of(bytes.begin(), bytes.end(), [](std::uint8_t value) {
        return value != 0;
    });
}

bool valid_code(const std::array<std::uint8_t, 6>& code) noexcept
{
    return std::all_of(code.begin(), code.end(), [](std::uint8_t value) {
        return value >= static_cast<std::uint8_t>('0') &&
               value <= static_cast<std::uint8_t>('9');
    });
}

bool valid_child(const InviteChildMaterial& child) noexcept
{
    return nonzero(child.child_id) && nonzero(child.nonce) && nonzero(child.commitment);
}

std::uint64_t add_saturated(std::uint64_t value, std::uint64_t delta) noexcept
{
    if (value > std::numeric_limits<std::uint64_t>::max() - delta)
        return std::numeric_limits<std::uint64_t>::max();
    return value + delta;
}

bool edge_whitespace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

} // namespace

InviteCoordinatorResult invite_code_from_uniform_u32(
    std::uint32_t draw, std::array<std::uint8_t, 6>* out) noexcept
{
    if (out == nullptr)
        return InviteCoordinatorResult::Invalid;
    if (draw >= UINT32_C(4294000000))
        return InviteCoordinatorResult::Retry;
    std::uint32_t value = draw % UINT32_C(1000000);
    for (std::size_t i = 0; i < out->size(); ++i)
    {
        (*out)[out->size() - i - 1] = static_cast<std::uint8_t>('0' + value % 10);
        value /= 10;
    }
    return InviteCoordinatorResult::Ok;
}

InviteCoordinatorResult normalize_invite_code(
    std::string_view input, std::array<std::uint8_t, 6>* out) noexcept
{
    if (out == nullptr)
        return InviteCoordinatorResult::Invalid;
    while (!input.empty() && edge_whitespace(input.front()))
        input.remove_prefix(1);
    while (!input.empty() && edge_whitespace(input.back()))
        input.remove_suffix(1);
    if (input.size() != out->size())
        return InviteCoordinatorResult::Invalid;
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] < '0' || input[i] > '9')
            return InviteCoordinatorResult::Invalid;
        (*out)[i] = static_cast<std::uint8_t>(input[i]);
    }
    return InviteCoordinatorResult::Ok;
}

InviteCoordinatorResult InviteCoordinator::create(
    std::uint64_t generation, std::uint64_t now_ns,
    const std::array<std::uint8_t, 6>& code,
    const InviteChildMaterial& ble, const InviteChildMaterial& qr) noexcept
{
    if (generation == 0 || !valid_code(code) || !valid_child(ble) || !valid_child(qr) ||
        ble.child_id == qr.child_id || ble.nonce == qr.nonce ||
        ble.commitment == qr.commitment)
        return InviteCoordinatorResult::Invalid;
    generation_ = generation;
    deadline_ns_ = add_saturated(now_ns, kInvitationLifetimeNs);
    code_ = code;
    ble_ = ble;
    qr_ = qr;
    active_ = true;
    consumed_ = false;
    inflight_gatt_.clear();
    return InviteCoordinatorResult::Ok;
}

InviteCoordinatorResult InviteCoordinator::cancel(std::uint64_t generation) noexcept
{
    if (generation == 0 || generation != generation_)
        return InviteCoordinatorResult::Stale;
    active_ = false;
    consumed_ = true;
    inflight_gatt_.clear();
    return InviteCoordinatorResult::Ok;
}

InviteCoordinatorResult InviteCoordinator::lookup(
    const std::array<std::uint8_t, 6>& code, std::uint64_t now_ns) noexcept
{
    if (!active_)
        return consumed_ ? InviteCoordinatorResult::Consumed : InviteCoordinatorResult::NotFound;
    if (now_ns >= deadline_ns_)
    {
        active_ = false;
        return InviteCoordinatorResult::Expired;
    }
    return code == code_ ? InviteCoordinatorResult::MatchPendingApproval
                         : InviteCoordinatorResult::NotFound;
}

InviteCoordinatorResult InviteCoordinator::accept(
    std::uint64_t generation, InviteRouteV2 route,
    const std::array<std::uint8_t, 16>& child_id, std::uint64_t now_ns) noexcept
{
    if (generation == 0 || generation != generation_)
        return InviteCoordinatorResult::Stale;
    if (consumed_)
        return InviteCoordinatorResult::Consumed;
    if (!active_ || now_ns >= deadline_ns_)
    {
        active_ = false;
        return InviteCoordinatorResult::Expired;
    }
    const InviteChildMaterial* selected = route == InviteRouteV2::BleAnonymous ? &ble_ :
        route == InviteRouteV2::Qr ? &qr_ : nullptr;
    if (selected == nullptr || child_id != selected->child_id)
        return InviteCoordinatorResult::Stale;
    active_ = false;
    consumed_ = true;
    inflight_gatt_.clear();
    return InviteCoordinatorResult::Accepted;
}

void InviteCoordinator::prune_window(std::vector<std::uint64_t>& timestamps,
                                     std::uint64_t now_ns) noexcept
{
    timestamps.erase(std::remove_if(timestamps.begin(), timestamps.end(),
        [now_ns](std::uint64_t timestamp) {
            return now_ns >= timestamp && now_ns - timestamp >= kInvitationLifetimeNs;
        }), timestamps.end());
}

InviteCoordinatorResult InviteCoordinator::begin_join_submission(std::uint64_t now_ns)
{
    prune_window(join_submissions_, now_ns);
    if (join_submissions_.size() >= 5)
        return InviteCoordinatorResult::RateLimited;
    join_submissions_.push_back(now_ns);
    return InviteCoordinatorResult::Accepted;
}

InviteCoordinatorResult InviteCoordinator::begin_host_lookup(
    std::uint64_t gatt_generation, std::uint64_t now_ns)
{
    if (gatt_generation == 0)
        return InviteCoordinatorResult::Invalid;
    if (std::find(inflight_gatt_.begin(), inflight_gatt_.end(), gatt_generation) !=
        inflight_gatt_.end())
        return InviteCoordinatorResult::Busy;
    prune_window(host_lookups_, now_ns);
    if (host_lookups_.size() >= 20)
        return InviteCoordinatorResult::RateLimited;
    host_lookups_.push_back(now_ns);
    inflight_gatt_.push_back(gatt_generation);
    return InviteCoordinatorResult::Accepted;
}

void InviteCoordinator::end_host_lookup(std::uint64_t gatt_generation) noexcept
{
    inflight_gatt_.erase(std::remove(inflight_gatt_.begin(), inflight_gatt_.end(),
                                     gatt_generation), inflight_gatt_.end());
}

bool InviteCoordinator::route_active(InviteRouteV2 route) const noexcept
{
    return active_ && (route == InviteRouteV2::BleAnonymous || route == InviteRouteV2::Qr);
}

InviteCoordinatorResult JoinLookupRound::start(
    std::uint64_t submission_id, std::uint64_t now_ns,
    std::uint64_t invitation_deadline_ns,
    const std::vector<std::uint64_t>& candidates)
{
    if (submission_id == 0 || candidates.empty() || candidates.size() > 8 ||
        invitation_deadline_ns <= now_ns)
        return InviteCoordinatorResult::Invalid;
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        if (candidates[i] == 0 ||
            std::find(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(i),
                      candidates[i]) != candidates.begin() + static_cast<std::ptrdiff_t>(i))
            return InviteCoordinatorResult::Invalid;
    }
    submission_id_ = submission_id;
    deadline_ns_ = std::min(invitation_deadline_ns,
                            add_saturated(now_ns, kLookupRoundLifetimeNs));
    candidates_ = candidates;
    next_index_ = 0;
    matched_context_.fill(0);
    has_match_ = false;
    active_ = true;
    return InviteCoordinatorResult::Accepted;
}

InviteCoordinatorResult JoinLookupRound::next_candidate(
    std::uint64_t now_ns, std::uint64_t* handle,
    std::uint64_t* candidate_deadline_ns) noexcept
{
    if (handle == nullptr || candidate_deadline_ns == nullptr || submission_id_ == 0)
        return InviteCoordinatorResult::Invalid;
    *handle = 0;
    *candidate_deadline_ns = 0;
    if (!active_ || now_ns >= deadline_ns_)
    {
        active_ = false;
        return InviteCoordinatorResult::Expired;
    }
    if (next_index_ >= candidates_.size())
    {
        active_ = false;
        return has_match_ ? InviteCoordinatorResult::MatchPendingApproval
                          : InviteCoordinatorResult::NotFound;
    }
    *handle = candidates_[next_index_++];
    *candidate_deadline_ns = std::min(deadline_ns_,
                                      add_saturated(now_ns, kLookupCandidateLifetimeNs));
    return InviteCoordinatorResult::Accepted;
}

InviteCoordinatorResult JoinLookupRound::record_match(
    std::uint64_t candidate_handle,
    const std::array<std::uint8_t, 32>& pair_context_hash) noexcept
{
    if (!active_ || candidate_handle == 0 || !nonzero(pair_context_hash) ||
        std::find(candidates_.begin(), candidates_.end(), candidate_handle) == candidates_.end())
        return InviteCoordinatorResult::Invalid;
    if (!has_match_)
    {
        matched_context_ = pair_context_hash;
        has_match_ = true;
        return InviteCoordinatorResult::MatchPendingApproval;
    }
    if (matched_context_ == pair_context_hash)
        return InviteCoordinatorResult::Duplicate;
    active_ = false;
    return InviteCoordinatorResult::Ambiguous;
}

} // namespace flynes::session
