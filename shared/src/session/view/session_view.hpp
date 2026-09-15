#ifndef FLYNES_SESSION_VIEW_SESSION_VIEW_HPP
#define FLYNES_SESSION_VIEW_SESSION_VIEW_HPP

#include <flynes/flynes_session.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace flynes::session {

struct AuthorizationState final
{
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::atomic<std::uint64_t> generation{1};
};

std::shared_ptr<AuthorizationState> make_authorization_state();
fly_session_view_v2_t* make_session_view(
    const std::shared_ptr<AuthorizationState>& authorization,
    std::uint64_t revision,
    std::uint32_t engine_state,
    bool include_cancel_action);
struct SessionActionSpec final
{
    std::uint64_t descriptor_id = 0;
    std::uint32_t action_kind = 0;
    bool enabled = false;
    const char* text_key = "";
    const char* reason_key = "";
};
fly_session_view_v2_t* make_session_view(
    const std::shared_ptr<AuthorizationState>& authorization,
    std::uint64_t revision, std::uint32_t engine_state,
    std::uint32_t link_state, std::uint32_t game_state,
    const char* primary_reason_key,
    const std::vector<SessionActionSpec>& actions);
void session_view_retain(fly_session_view_v2_t* view) noexcept;
bool approval_token_matches(
    const fly_session_approval_token_v2_t* token,
    const std::shared_ptr<AuthorizationState>& authorization) noexcept;

} // namespace flynes::session

struct fly_session_approval_token_v2_handle
{
    std::atomic<std::uint32_t> references{1};
    std::shared_ptr<flynes::session::AuthorizationState> authorization;
    std::uint64_t bound_generation = 0;
    std::uint64_t descriptor_id = 0;
    std::uint32_t action_kind = 0;
};

struct fly_session_view_v2_handle
{
    std::atomic<std::uint32_t> references{1};
    fly_session_snapshot_v2 snapshot{};
    bool has_pairing = false;
    fly_session_pairing_v2 pairing{};
    std::vector<fly_session_candidate_v2> candidates;
    std::vector<fly_session_friend_v2> friends;
    std::vector<fly_session_action_descriptor_v2> actions;
    std::vector<fly_session_game_choice_v2> game_choices;
};

#endif
