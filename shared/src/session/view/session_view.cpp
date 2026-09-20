#include "session_view.hpp"

#include <algorithm>
#include <cstring>
#include <new>

namespace {

std::atomic<std::uint64_t> next_engine_instance{1};

void copy_key(char (&destination)[64], const char* source) noexcept
{
    const auto length = (std::min)(std::strlen(source), sizeof(destination) - 1);
    std::memcpy(destination, source, length);
    destination[length] = '\0';
}

} // namespace

namespace flynes::session {

std::shared_ptr<AuthorizationState> make_authorization_state()
{
    auto state = std::make_shared<AuthorizationState>();
    const auto value = next_engine_instance.fetch_add(1, std::memory_order_relaxed);
    for (std::size_t index = 0; index < sizeof(value); ++index)
    {
        state->engine_instance_id[index] =
            static_cast<std::uint8_t>(value >> (index * 8));
    }
    return state;
}

fly_session_view_v2_t* make_session_view(
    const std::shared_ptr<AuthorizationState>& authorization,
    std::uint64_t revision,
    std::uint32_t engine_state,
    bool include_cancel_action)
{
    const std::vector<SessionActionSpec> actions = include_cancel_action
        ? std::vector<SessionActionSpec>{{1, FLY_SESSION_ACTION_CANCEL_LOADING_V2,
                                         true, "nearby.action.cancel_loading", ""}}
        : std::vector<SessionActionSpec>{};
    return make_session_view(
        authorization, revision, engine_state, FLY_SESSION_LINK_UNAVAILABLE_V2,
        FLY_SESSION_GAME_NOT_STARTED_V2,
        include_cancel_action ? "" : "nearby.state.shutdown_complete", actions);
}

fly_session_view_v2_t* make_session_view(
    const std::shared_ptr<AuthorizationState>& authorization,
    std::uint64_t revision, std::uint32_t engine_state,
    std::uint32_t link_state, std::uint32_t game_state,
    const char* primary_reason_key,
    const std::vector<SessionActionSpec>& actions)
{
    auto view = std::make_unique<fly_session_view_v2_t>();
    view->snapshot.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE;
    view->snapshot.abi_version = FLY_SESSION_ABI_VERSION_2;
    view->snapshot.view_revision = revision;
    view->snapshot.engine_state = engine_state;
    view->snapshot.link_state = link_state;
    view->snapshot.game_state = game_state;
    view->snapshot.scope.struct_size = FLY_SESSION_SCOPE_V2_SIZE;
    view->snapshot.scope.abi_version = FLY_SESSION_ABI_VERSION_2;
    view->snapshot.scope.kind = FLY_SESSION_SCOPE_ENGINE_V2;

    copy_key(view->snapshot.primary_reason_key,
             primary_reason_key ? primary_reason_key : "");
    view->actions.reserve(actions.size());
    for (const auto& spec : actions)
    {
        auto token = std::make_unique<fly_session_approval_token_v2_t>();
        token->authorization = authorization;
        token->bound_generation =
            authorization->generation.load(std::memory_order_acquire);
        token->descriptor_id = spec.descriptor_id;
        token->action_kind = spec.action_kind;
        fly_session_action_descriptor_v2 descriptor{};
        descriptor.struct_size = FLY_SESSION_ACTION_DESCRIPTOR_V2_SIZE;
        descriptor.abi_version = FLY_SESSION_ABI_VERSION_2;
        descriptor.descriptor_id = spec.descriptor_id;
        descriptor.action_kind = spec.action_kind;
        descriptor.enabled = spec.enabled ? 1U : 0U;
        copy_key(descriptor.text_key, spec.text_key ? spec.text_key : "");
        copy_key(descriptor.reason_key, spec.reason_key ? spec.reason_key : "");
        descriptor.approval_token = spec.enabled ? token.release() : nullptr;
        view->actions.push_back(descriptor);
    }
    view->snapshot.action_count = static_cast<std::uint32_t>(view->actions.size());
    view->snapshot.candidate_count =
        static_cast<std::uint32_t>(view->candidates.size());
    view->snapshot.friend_count =
        static_cast<std::uint32_t>(view->friends.size());
    view->snapshot.game_choice_count =
        static_cast<std::uint32_t>(view->game_choices.size());
    return view.release();
}

void session_view_retain(fly_session_view_v2_t* view) noexcept
{
    if (view)
    {
        view->references.fetch_add(1, std::memory_order_relaxed);
    }
}

bool approval_token_matches(
    const fly_session_approval_token_v2_t* token,
    const std::shared_ptr<AuthorizationState>& authorization) noexcept
{
    return token && token->authorization.get() == authorization.get() &&
           token->bound_generation ==
               authorization->generation.load(std::memory_order_acquire);
}

} // namespace flynes::session

extern "C" fly_session_result_v2 fly_session_view_read_v2(
    const fly_session_view_v2_t* view,
    fly_session_snapshot_v2* out_snapshot)
{
    if (!view || !out_snapshot)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    /*
     * Tail-append compatibility: the pre-DUAL prefix is the required size, and
     * the copy is bounded by what the caller declared, so an older reader is
     * served the older fields and never has its buffer overrun by the appended
     * DUAL block. Requiring only the prefix is what keeps the append additive.
     */
    if (out_snapshot->struct_size < FLY_SESSION_SNAPSHOT_V2_R0_SIZE ||
        out_snapshot->abi_version != FLY_SESSION_ABI_VERSION_2)
    {
        return FLY_SESSION_V2_ABI_MISMATCH;
    }
    const auto declared = out_snapshot->struct_size;
    const auto available = FLY_SESSION_SNAPSHOT_V2_SIZE;
    const auto copied = declared < available ? declared : available;
    std::memcpy(out_snapshot, &view->snapshot, copied);
    return FLY_SESSION_V2_OK;
}

extern "C" fly_session_result_v2 fly_session_view_read_pairing_v2(
    const fly_session_view_v2_t* view,
    fly_session_pairing_v2* out_pairing)
{
    if (!view || !out_pairing)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (out_pairing->struct_size < FLY_SESSION_PAIRING_V2_SIZE ||
        out_pairing->abi_version != FLY_SESSION_ABI_VERSION_2)
        return FLY_SESSION_V2_ABI_MISMATCH;
    if (!view->has_pairing)
        return FLY_SESSION_V2_EMPTY;
    *out_pairing = view->pairing;
    return FLY_SESSION_V2_OK;
}

extern "C" fly_session_result_v2 fly_session_view_copy_actions_v2(
    const fly_session_view_v2_t* view,
    uint32_t offset,
    fly_session_action_descriptor_v2* out_actions,
    uint32_t capacity,
    uint32_t* written)
{
    if (!view || !written || (capacity != 0 && !out_actions))
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    *written = 0;
    const std::uint32_t total = static_cast<std::uint32_t>(view->actions.size());
    if (offset > total)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    if (offset == total || capacity == 0)
    {
        return FLY_SESSION_V2_OK;
    }
    const auto count = (std::min)(capacity, total - offset);
    std::copy_n(view->actions.begin() + offset, count, out_actions);
    *written = count;
    return FLY_SESSION_V2_OK;
}

namespace {

template <typename Item>
fly_session_result_v2 copy_view_page(const std::vector<Item>& source,
                                     uint32_t offset, Item* output,
                                     uint32_t capacity, uint32_t* written)
{
    if (!written || (capacity != 0 && !output))
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    *written = 0;
    const auto total = static_cast<std::uint32_t>(source.size());
    if (offset > total)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (offset == total || capacity == 0)
        return FLY_SESSION_V2_OK;
    const auto count = (std::min)(capacity, total - offset);
    std::copy_n(source.begin() + offset, count, output);
    *written = count;
    return FLY_SESSION_V2_OK;
}

} // namespace

extern "C" fly_session_result_v2 fly_session_view_copy_candidates_v2(
    const fly_session_view_v2_t* view, uint32_t offset,
    fly_session_candidate_v2* out_candidates, uint32_t capacity,
    uint32_t* written)
{
    return view ? copy_view_page(view->candidates, offset, out_candidates,
                                 capacity, written)
                : FLY_SESSION_V2_INVALID_ARGUMENT;
}

extern "C" fly_session_result_v2 fly_session_view_copy_friends_v2(
    const fly_session_view_v2_t* view, uint32_t offset,
    fly_session_friend_v2* out_friends, uint32_t capacity,
    uint32_t* written)
{
    return view ? copy_view_page(view->friends, offset, out_friends,
                                 capacity, written)
                : FLY_SESSION_V2_INVALID_ARGUMENT;
}

extern "C" fly_session_result_v2 fly_session_view_copy_game_choices_v2(
    const fly_session_view_v2_t* view, uint32_t offset,
    fly_session_game_choice_v2* out_choices, uint32_t capacity,
    uint32_t* written)
{
    if (!view || !written || (capacity != 0 && !out_choices))
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    *written = 0;
    const auto total = static_cast<std::uint32_t>(view->game_choices.size());
    if (offset > total)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (offset == total || capacity == 0)
        return FLY_SESSION_V2_OK;

    const auto declared_in = out_choices->struct_size;
    const auto abi_in = out_choices->abi_version;
    const auto declared =
        (declared_in == 0u && abi_in == 0u)
            ? FLY_SESSION_GAME_CHOICE_V2_R0_SIZE
            : declared_in;
    const auto abi =
        (declared_in == 0u && abi_in == 0u) ? FLY_SESSION_ABI_VERSION_2 : abi_in;
    if (declared < FLY_SESSION_GAME_CHOICE_V2_R0_SIZE ||
        declared > FLY_SESSION_GAME_CHOICE_V2_SIZE ||
        abi != FLY_SESSION_ABI_VERSION_2)
    {
        return FLY_SESSION_V2_ABI_MISMATCH;
    }

    const auto count = (std::min)(capacity, total - offset);
    const auto copied = declared;
    auto* bytes = reinterpret_cast<std::uint8_t*>(out_choices);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        auto* dest = bytes + static_cast<std::size_t>(index) * declared;
        std::memcpy(dest, &view->game_choices[offset + index], copied);
        std::memcpy(dest, &declared, sizeof(declared));
        std::memcpy(dest + sizeof(declared), &abi, sizeof(abi));
    }
    *written = count;
    return FLY_SESSION_V2_OK;
}

extern "C" void fly_session_approval_token_retain_v2(
    fly_session_approval_token_v2_t* token)
{
    if (token)
    {
        token->references.fetch_add(1, std::memory_order_relaxed);
    }
}

extern "C" void fly_session_approval_token_release_v2(
    fly_session_approval_token_v2_t* token)
{
    if (token && token->references.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        delete token;
    }
}

extern "C" void fly_session_view_release_v2(fly_session_view_v2_t* view)
{
    if (view && view->references.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        for (const auto& action : view->actions)
        {
            fly_session_approval_token_release_v2(action.approval_token);
        }
        delete view;
    }
}
