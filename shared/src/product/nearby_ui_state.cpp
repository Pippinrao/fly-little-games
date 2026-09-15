// Implementation of the nearby UI product projection. Pure functions over
// session facts; see the header for the design references and the contract
// JSON for the normative strings and layout constants.

#include "flynes/product/nearby_ui_state.hpp"

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

} // namespace flynes::product::nearby
