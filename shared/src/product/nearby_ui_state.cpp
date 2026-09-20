// Implementation of the nearby UI product projection. Pure functions over
// session facts; see the header for the design references and the contract
// JSON for the normative strings and layout constants.

#include "flynes/product/nearby_ui_state.hpp"

#include <flynes/flynes_session.h>

#include <cmath>
#include <stdexcept>

namespace flynes::product::nearby {

NearbyLayout project_layout(double available_width) noexcept
{
    if (!std::isfinite(available_width) || available_width < 0.0)
    {
        return {};
    }
    NearbyLayout result;
    result.valid = true;
    result.split = available_width > kNearbySplitThreshold;
    if (!result.split)
    {
        result.left_width = available_width;
        result.right_width = available_width;
        return result;
    }
    result.left_width = kNearbyLeftColumnWidth;
    result.gutter = kNearbyColumnGutter;
    result.right_width = available_width - result.left_width - result.gutter;
    return result;
}

std::array<std::string_view, 6> n00_left_column_keys() noexcept
{
    return {
        "nearby.entry.kicker",
        "nearby.entry.headline",
        "nearby.entry.subtitle",
        "nearby.action.create",
        "nearby.action.enterCode",
        "nearby.action.scanQr",
    };
}

bool n00_shows_stage_pipeline() noexcept
{
    return false;
}

bool n00_shows_scan_host_qr() noexcept
{
    return false;
}

bool n00_shows_pairing_shortcut() noexcept
{
    return false;
}

bool n00_tabs_on_right_column() noexcept
{
    return true;
}

bool n00_find_devices_on_devices_tab() noexcept
{
    return true;
}

namespace {

constexpr std::array<std::string_view, 14> kScreenCodes{
    "N00", "N01", "N02", "N03", "N04", "N05", "N06",
    "N07", "N08", "G00", "N09", "N10", "N11", "N12",
};

constexpr std::array<std::string_view, kStageCount> kStageCodes{
    "permissions", "discovery", "authentication",
    "wifi", "quic", "version", "codec",
};

constexpr std::array<std::string_view, kStageCount> kStageLabelsZh{
    "权限", "发现", "认证", "Wi-Fi", "QUIC", "版本", "codec",
};

constexpr std::array<std::string_view, 4> kStageStateLabelsZh{
    "未开始", "进行中", "已通过", "失败",
};

constexpr std::array<std::string_view, kActionCount> kActionCodes{
    "createInvite",        "enterInviteCode",  "scanQr",
    "findDevices",         "openFriends",      "backToGameCenter",
    "regenerateInvite",    "cancelInvite",     "submitJoinCode",
    "switchToScan",        "switchToJoinCode", "cancel",
    "cancelRequest",       "acceptRequest",    "rejectRequest",
    "confirmSasMatch",     "reportSasMismatch", "cancelConnecting",
    "viewDetails",         "goToGameCenter",   "disconnect",
    "openNearby",          "selectGame",       "disableMultiplayerFilter",
    "confirmConfig",       "backToLobby",      "retry",
    "newInvite",           "backToEntry",      "grantSend",
    "grantReceive",        "confirmImport",    "cancelTransfer",
    "renameFriend",        "deleteFriend",     "blockFriend",
    "resetIdentity",
};

constexpr std::string_view kTimelineViolationKey = "nearby.stage.timeline.invalid";

constexpr std::array<std::string_view, kStageCount> kStageFailureKeys{
    "nearby.stage.permissions.failed", "nearby.stage.discovery.failed",
    "nearby.stage.authentication.failed", "nearby.stage.wifi.failed",
    "nearby.stage.quic.failed", "nearby.stage.version.failed",
    "nearby.stage.codec.failed",
};

} // namespace

std::string_view screen_code(ScreenId screen)
{
    const auto index = static_cast<std::size_t>(screen);
    if (index >= kScreenCodes.size())
    {
        throw std::invalid_argument("screen id out of range");
    }
    return kScreenCodes[index];
}

EntryStatus project_entry(const SessionFacts& facts)
{
    if (!facts.facts_known)
    {
        return EntryStatus{ConnectionStatus::Unknown, "nearby.entry.unavailable",
                           "暂时无法确认联机状态"};
    }

    if (facts.connection_established && facts.peer_verified && facts.channel_bound
        && facts.compatibility_verified)
    {
        return EntryStatus{ConnectionStatus::Connected, "nearby.entry.connected", "双人联机中"};
    }

    if (facts.previous_connection_established && facts.reconnecting)
    {
        return EntryStatus{ConnectionStatus::Interrupted, "nearby.entry.interrupted", "联机中断"};
    }

    // Partial progress (request sent, code matched, host accepted, one-sided
    // SAS, network building) all stay under the plain 附近联机 entry.
    const bool pairing = facts.peer_verified || facts.channel_bound
        || facts.compatibility_verified || facts.reconnecting;
    const ConnectionStatus status =
        pairing ? ConnectionStatus::Pairing : ConnectionStatus::Disconnected;
    return EntryStatus{status, "nearby.open", "附近联机"};
}

StageProjection project_stages(const std::array<StageState, kStageCount>& raw)
{
    StageProjection projection;
    projection.states = raw;
    projection.first_failed = -1;
    projection.violation_key = kTimelineViolationKey;

    for (std::size_t i = 0; i < kStageCount; ++i)
    {
        if (raw[i] == StageState::Failed)
        {
            if (projection.first_failed >= 0)
            {
                return projection;  // second failure: not displayable
            }
            projection.first_failed = static_cast<int>(i);
        }
    }

    if (projection.first_failed < 0)
    {
        // No failure: a Passed/InProgress prefix followed by NotStarted.
        bool seen_active = false;
        for (std::size_t i = 0; i < kStageCount; ++i)
        {
            const StageState state = raw[i];
            if (state == StageState::NotStarted)
            {
                seen_active = true;
                continue;
            }
            if (seen_active)
            {
                return projection;  // activity resumed after a not-started stage
            }
            if (state == StageState::InProgress)
            {
                seen_active = true;
            }
            else if (state != StageState::Passed)
            {
                return projection;
            }
        }
        projection.valid = true;
        projection.violation_key = {};
        return projection;
    }

    // One failure: everything before must be Passed, everything after NotStarted.
    for (std::size_t i = 0; i < kStageCount; ++i)
    {
        const StageState state = raw[i];
        if (i < static_cast<std::size_t>(projection.first_failed))
        {
            if (state != StageState::Passed)
            {
                return projection;
            }
        }
        else if (i == static_cast<std::size_t>(projection.first_failed))
        {
            continue;  // the failure itself
        }
        else if (state != StageState::NotStarted)
        {
            return projection;
        }
    }

    projection.valid = true;
    projection.violation_key = {};
    return projection;
}

std::string_view stage_code(Stage stage)
{
    const auto index = static_cast<std::size_t>(stage);
    if (index >= kStageCount)
    {
        throw std::invalid_argument("stage out of range");
    }
    return kStageCodes[index];
}

std::string_view stage_label_zh(Stage stage)
{
    const auto index = static_cast<std::size_t>(stage);
    if (index >= kStageCount)
    {
        throw std::invalid_argument("stage out of range");
    }
    return kStageLabelsZh[index];
}

std::string_view stage_state_label_zh(StageState state)
{
    const auto index = static_cast<std::size_t>(state);
    if (index >= kStageStateLabelsZh.size())
    {
        throw std::invalid_argument("stage state out of range");
    }
    return kStageStateLabelsZh[index];
}

std::string_view stage_failure_key(Stage stage)
{
    const auto index = static_cast<std::size_t>(stage);
    if (index >= kStageCount)
    {
        throw std::invalid_argument("stage out of range");
    }
    return kStageFailureKeys[index];
}

std::string_view permission_denied_reason(Permission permission)
{
    switch (permission)
    {
    case Permission::Camera:
        return "nearby.reason.permission.cameraDenied";
    case Permission::NearbyDiscovery:
        return "nearby.reason.permission.discoveryDenied";
    case Permission::Wifi:
        return "nearby.reason.permission.wifiDenied";
    }
    return "nearby.reason.permission.cameraDenied";
}

std::vector<ActionId> available_actions(ScreenId screen, const ScreenContext& context)
{
    std::vector<ActionId> actions;
    const auto push = [&actions](ActionId action) { actions.push_back(action); };

    switch (screen)
    {
    case ScreenId::NearbyEntry:
        push(ActionId::CreateInvite);
        push(ActionId::EnterInviteCode);
        if (context.camera_usable)
        {
            push(ActionId::ScanQr);
        }
        if (context.discovery_usable)
        {
            push(ActionId::FindDevices);
        }
        push(ActionId::OpenFriends);
        push(ActionId::BackToGameCenter);
        break;

    case ScreenId::InviteCode:
        push(ActionId::RegenerateInvite);
        push(ActionId::CancelInvite);
        break;

    case ScreenId::JoinByCode:
        if (context.join_code_complete && !context.join_request_in_flight)
        {
            push(ActionId::SubmitJoinCode);
        }
        if (context.camera_usable)
        {
            push(ActionId::SwitchToScan);
        }
        push(ActionId::CancelRequest);
        break;

    case ScreenId::ScanQr:
        push(ActionId::SwitchToJoinCode);
        push(ActionId::Cancel);
        break;

    case ScreenId::WaitHost:
        push(ActionId::CancelRequest);
        break;

    case ScreenId::HostRequest:
        push(ActionId::AcceptRequest);
        push(ActionId::RejectRequest);
        break;

    case ScreenId::SasConfirm:
        push(ActionId::ConfirmSasMatch);
        push(ActionId::ReportSasMismatch);
        break;

    case ScreenId::Connecting:
        push(ActionId::CancelConnecting);
        push(ActionId::ViewDetails);
        break;

    case ScreenId::ConnectedLobby:
        push(ActionId::GoToGameCenter);
        push(ActionId::Disconnect);
        break;

    case ScreenId::GameCenter:
        push(ActionId::OpenNearby);
        push(ActionId::SelectGame);
        if (context.multiplayer_zero_result)
        {
            push(ActionId::DisableMultiplayerFilter);
        }
        break;

    case ScreenId::GameConfig:
        push(ActionId::ConfirmConfig);
        push(ActionId::BackToLobby);
        break;

    case ScreenId::Failure:
        push(ActionId::Retry);
        push(ActionId::NewInvite);
        push(ActionId::BackToEntry);
        break;

    case ScreenId::ContentTransfer:
        push(ActionId::GrantSend);
        push(ActionId::GrantReceive);
        push(ActionId::ConfirmImport);
        push(ActionId::CancelTransfer);
        break;

    case ScreenId::Friends:
        push(ActionId::RenameFriend);
        push(ActionId::DeleteFriend);
        push(ActionId::BlockFriend);
        push(ActionId::ResetIdentity);
        break;
    }

    return actions;
}

std::string_view action_code(ActionId action)
{
    const auto index = static_cast<std::size_t>(action);
    if (index >= kActionCount)
    {
        throw std::invalid_argument("action out of range");
    }
    return kActionCodes[index];
}

PendingConfigFacts invalidate_on_config_change(const PendingConfigFacts& current,
                                               std::string new_config_id)
{
    if (new_config_id == current.pending_config_id)
    {
        return current;
    }
    PendingConfigFacts rebound;
    rebound.pending_config_id = std::move(new_config_id);
    rebound.local_confirmed = false;
    rebound.peer_confirmed = false;
    return rebound;
}

bool config_start_allowed(const PendingConfigFacts& facts)
{
    return facts.local_confirmed && facts.peer_confirmed && !facts.pending_config_id.empty();
}

NearbyContainer screen_container(ScreenId screen) noexcept
{
    switch (screen)
    {
    case ScreenId::NearbyEntry:
        return NearbyContainer::Entry;
    case ScreenId::InviteCode:
    case ScreenId::JoinByCode:
    case ScreenId::ScanQr:
    case ScreenId::WaitHost:
    case ScreenId::HostRequest:
    case ScreenId::SasConfirm:
    case ScreenId::Connecting:
        return NearbyContainer::Pair;
    case ScreenId::ConnectedLobby:
        return NearbyContainer::Connected;
    case ScreenId::GameCenter:
        return NearbyContainer::Home;
    case ScreenId::GameConfig:
        return NearbyContainer::Config;
    case ScreenId::Failure:
        return NearbyContainer::Failure;
    case ScreenId::ContentTransfer:
        return NearbyContainer::Content;
    case ScreenId::Friends:
        return NearbyContainer::Friends;
    }
    return NearbyContainer::Entry;
}

namespace {

NearbyNavigation stay(const NearbyNavigationFacts& facts)
{
    NearbyNavigation out;
    out.screen = facts.screen;
    out.details_open = facts.details_open;
    out.failure_reason_key = facts.failure_reason_key;
    return out;
}

NearbyNavigation go(const NearbyNavigationFacts& facts, ScreenId screen)
{
    NearbyNavigation out = stay(facts);
    out.screen = screen;
    out.details_open = false;
    return out;
}

} // namespace

NearbyNavigation apply_nearby_action(const NearbyNavigationFacts& facts, ActionId action)
{
    if (facts.details_open && action == ActionId::ViewDetails)
    {
        NearbyNavigation closed = stay(facts);
        closed.details_open = false;
        closed.rebuild_invite = false;
        closed.rebuild_connection = false;
        return closed;
    }

    switch (action)
    {
    case ActionId::CreateInvite:
        if (facts.screen == ScreenId::NearbyEntry)
        {
            return go(facts, ScreenId::InviteCode);
        }
        break;
    case ActionId::EnterInviteCode:
        if (facts.screen == ScreenId::NearbyEntry)
        {
            return go(facts, ScreenId::JoinByCode);
        }
        break;
    case ActionId::ScanQr:
        if (facts.screen == ScreenId::NearbyEntry)
        {
            return go(facts, ScreenId::ScanQr);
        }
        break;
    case ActionId::SwitchToScan:
        if (facts.screen == ScreenId::JoinByCode)
        {
            return go(facts, ScreenId::ScanQr);
        }
        break;
    case ActionId::SwitchToJoinCode:
        if (facts.screen == ScreenId::ScanQr)
        {
            return go(facts, ScreenId::JoinByCode);
        }
        break;
    case ActionId::SubmitJoinCode:
        if (facts.screen == ScreenId::JoinByCode)
        {
            if (!facts.join_accepted)
            {
                return stay(facts);
            }
            return go(facts, facts.local_is_host ? ScreenId::HostRequest : ScreenId::WaitHost);
        }
        break;
    case ActionId::AcceptRequest:
        if (facts.screen == ScreenId::HostRequest)
        {
            return go(facts, facts.qr_signed_path ? ScreenId::Connecting : ScreenId::SasConfirm);
        }
        break;
    case ActionId::ConfirmSasMatch:
        if (facts.screen == ScreenId::SasConfirm)
        {
            return stay(facts);
        }
        break;
    case ActionId::GoToGameCenter:
        if (facts.screen == ScreenId::ConnectedLobby)
        {
            return go(facts, ScreenId::GameCenter);
        }
        break;
    case ActionId::SelectGame:
        if (facts.screen == ScreenId::GameCenter && facts.session_connected)
        {
            return go(facts, ScreenId::GameConfig);
        }
        break;
    case ActionId::BackToLobby:
        if (facts.screen == ScreenId::GameConfig)
        {
            NearbyNavigation back = go(facts, ScreenId::GameCenter);
            back.clear_config_confirm = true;
            back.terminate_attempt = false;
            back.rebuild_connection = false;
            return back;
        }
        break;
    case ActionId::ViewDetails:
        if (facts.screen == ScreenId::Connecting)
        {
            NearbyNavigation open = stay(facts);
            open.details_open = true;
            return open;
        }
        break;
    case ActionId::RejectRequest:
    case ActionId::CancelInvite:
    case ActionId::CancelRequest:
    case ActionId::ReportSasMismatch:
    case ActionId::CancelConnecting:
    case ActionId::Disconnect:
    {
        NearbyNavigation ended = go(facts, ScreenId::NearbyEntry);
        ended.terminate_attempt = true;
        return ended;
    }
    case ActionId::Cancel:
        if (facts.screen == ScreenId::ScanQr)
        {
            NearbyNavigation back = go(facts, ScreenId::NearbyEntry);
            back.terminate_attempt = false;
            back.rebuild_invite = false;
            return back;
        }
        break;
    case ActionId::BackToGameCenter:
        if (facts.screen == ScreenId::NearbyEntry)
        {
            NearbyNavigation home = go(facts, ScreenId::GameCenter);
            home.terminate_attempt = false;
            return home;
        }
        break;
    default:
        break;
    }
    return stay(facts);
}

NearbyNavigation apply_nearby_session(const NearbyNavigationFacts& facts)
{
    if (facts.first_stage_failed)
    {
        NearbyNavigation failed = go(facts, ScreenId::Failure);
        failed.failure_reason_key = facts.failure_reason_key;
        return failed;
    }
    if (facts.screen == ScreenId::ScanQr && !facts.qr_capability_ready)
    {
        return stay(facts);
    }
    if (facts.link_ready
        && (facts.screen == ScreenId::SasConfirm || facts.screen == ScreenId::Connecting
            || facts.screen == ScreenId::WaitHost || facts.screen == ScreenId::HostRequest))
    {
        return go(facts, ScreenId::ConnectedLobby);
    }
    return stay(facts);
}

uint32_t session_action_kind(ActionId action) noexcept
{
    switch (action)
    {
    case ActionId::CreateInvite:
        return FLY_SESSION_ACTION_CREATE_INVITE_V2;
    case ActionId::FindDevices:
        return FLY_SESSION_ACTION_START_DISCOVERY_V2;
    case ActionId::RegenerateInvite:
        return FLY_SESSION_ACTION_REGENERATE_INVITE_V2;
    case ActionId::CancelInvite:
        return FLY_SESSION_ACTION_CANCEL_INVITE_V2;
    case ActionId::CancelRequest:
        return FLY_SESSION_ACTION_CANCEL_JOIN_V2;
    case ActionId::SubmitJoinCode:
        return FLY_SESSION_ACTION_JOIN_CODE_V2;
    case ActionId::ScanQr:
        return FLY_SESSION_ACTION_BEGIN_SCAN_V2;
    case ActionId::AcceptRequest:
        return FLY_SESSION_ACTION_ACCEPT_REQUEST_V2;
    case ActionId::RejectRequest:
        return FLY_SESSION_ACTION_REJECT_REQUEST_V2;
    case ActionId::ConfirmSasMatch:
        return FLY_SESSION_ACTION_CONFIRM_SAS_V2;
    case ActionId::ReportSasMismatch:
        return FLY_SESSION_ACTION_REJECT_SAS_V2;
    case ActionId::CancelConnecting:
        return FLY_SESSION_ACTION_CANCEL_CONNECT_V2;
    case ActionId::Disconnect:
        return FLY_SESSION_ACTION_DISCONNECT_LINK_V2;
    case ActionId::SelectGame:
        return FLY_SESSION_ACTION_SELECT_CONTENT_V2;
    case ActionId::ConfirmConfig:
        return FLY_SESSION_ACTION_CONFIRM_GAME_CONFIG_V2;
    case ActionId::BackToLobby:
        return FLY_SESSION_ACTION_RETURN_TO_LOBBY_V2;
    case ActionId::GrantSend:
        return FLY_SESSION_ACTION_APPROVE_SEND_V2;
    case ActionId::GrantReceive:
        return FLY_SESSION_ACTION_APPROVE_RECEIVE_V2;
    case ActionId::CancelTransfer:
        return FLY_SESSION_ACTION_CANCEL_CONTENT_V2;
    case ActionId::ConfirmImport:
        return FLY_SESSION_ACTION_APPROVE_IMPORT_V2;
    case ActionId::Retry:
        return FLY_SESSION_ACTION_RETRY_FAILURE_V2;
    case ActionId::RenameFriend:
        return FLY_SESSION_ACTION_RENAME_FRIEND_V2;
    case ActionId::DeleteFriend:
        return FLY_SESSION_ACTION_DELETE_FRIEND_V2;
    case ActionId::BlockFriend:
        return FLY_SESSION_ACTION_BLOCK_FRIEND_V2;
    case ActionId::ResetIdentity:
        return FLY_SESSION_ACTION_RESET_LOCAL_IDENTITY_V2;
    case ActionId::EnterInviteCode:
    case ActionId::OpenFriends:
    case ActionId::BackToGameCenter:
    case ActionId::SwitchToScan:
    case ActionId::SwitchToJoinCode:
    case ActionId::Cancel:
    case ActionId::ViewDetails:
    case ActionId::GoToGameCenter:
    case ActionId::OpenNearby:
    case ActionId::DisableMultiplayerFilter:
    case ActionId::NewInvite:
    case ActionId::BackToEntry:
    case ActionId::Count:
        return 0u;
    }
    return 0u;
}

} // namespace flynes::product::nearby
