// Product projection tests for the approved nearby UI contract
// (design 2026-09-13 §3.1/§3.2/§4/§4.2/§5 and cases C01/C07/C09/C10/C11/C13/C14).
// These run before the platform ports and pin the status/action/reason rules
// that all three platforms must reproduce from the same session facts.

#include "flynes/product/nearby_ui_state.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void check_key(std::string_view actual, std::string_view expected, const char* message)
{
    check(actual == expected, message);
}

using flynes::product::nearby::ActionId;
using flynes::product::nearby::ConnectionStatus;
using flynes::product::nearby::EntryStatus;
using flynes::product::nearby::NearbyLayout;
using flynes::product::nearby::PendingConfigFacts;
using flynes::product::nearby::Permission;
using flynes::product::nearby::ScreenContext;
using flynes::product::nearby::ScreenId;
using flynes::product::nearby::SessionFacts;
using flynes::product::nearby::Stage;
using flynes::product::nearby::StageProjection;
using flynes::product::nearby::StageState;
using flynes::product::nearby::available_actions;
using flynes::product::nearby::config_start_allowed;
using flynes::product::nearby::invalidate_on_config_change;
using flynes::product::nearby::permission_denied_reason;
using flynes::product::nearby::project_entry;
using flynes::product::nearby::project_layout;
using flynes::product::nearby::project_stages;
using flynes::product::nearby::screen_code;
using flynes::product::nearby::stage_failure_key;
using flynes::product::nearby::stage_label_zh;

bool contains(const std::vector<ActionId>& actions, ActionId action)
{
    for (const ActionId candidate : actions)
    {
        if (candidate == action)
        {
            return true;
        }
    }
    return false;
}

SessionFacts verified_session()
{
    SessionFacts facts;
    facts.facts_known = true;
    facts.connection_established = true;
    facts.peer_verified = true;
    facts.channel_bound = true;
    facts.compatibility_verified = true;
    return facts;
}

// --- C01/C09/C14: entry status rule -----------------------------------------

void test_entry_statuses()
{
    SessionFacts facts;
    facts.facts_known = true;

    const EntryStatus disconnected = project_entry(facts);
    check(disconnected.status == ConnectionStatus::Disconnected, "C01: no facts beyond known -> disconnected");
    check_key(disconnected.string_key, "nearby.open", "C01: entry key is nearby.open");
    check(disconnected.text_zh == std::string_view("附近联机"), "C01: entry text is 附近联机");

    facts.peer_verified = true;
    const EntryStatus pairing = project_entry(facts);
    check(pairing.status == ConnectionStatus::Pairing, "C06/C07: partial facts stay in pairing");
    check_key(pairing.string_key, "nearby.open", "pairing still shows 附近联机");

    // Scan success / host acceptance / correct code / single-side SAS can only
    // fill partial facts; none of them may reach Connected.
    facts.channel_bound = true;
    check(project_entry(facts).status == ConnectionStatus::Pairing,
          "C07/C08: bound without full verification never shows 双人联机中");
    facts.compatibility_verified = true;
    check(project_entry(facts).status == ConnectionStatus::Pairing,
          "missing connectionEstablished stays below Connected");

    const EntryStatus connected = project_entry(verified_session());
    check(connected.status == ConnectionStatus::Connected, "C09: all four checks pass -> connected");
    check_key(connected.string_key, "nearby.entry.connected", "C09: connected entry key");
    check(connected.text_zh == std::string_view("双人联机中"), "C09: connected text is 双人联机中");

    SessionFacts interrupted;
    interrupted.facts_known = true;
    interrupted.previous_connection_established = true;
    interrupted.reconnecting = true;
    const EntryStatus interrupted_status = project_entry(interrupted);
    check(interrupted_status.status == ConnectionStatus::Interrupted, "C14: established+reconnecting -> interrupted");
    check_key(interrupted_status.string_key, "nearby.entry.interrupted", "C14: interrupted entry key");
    check(interrupted_status.text_zh == std::string_view("联机中断"), "C14: interrupted text is 联机中断");

    // A connection that used to exist but is cleanly gone is not "interrupted".
    SessionFacts closed;
    closed.facts_known = true;
    closed.previous_connection_established = true;
    check(project_entry(closed).status == ConnectionStatus::Disconnected,
          "explicit disconnect / terminated connection returns to 附近联机");
}

void test_unknown_facts_project_unavailable()
{
    SessionFacts unknown;
    const EntryStatus status = project_entry(unknown);
    check(status.status == ConnectionStatus::Unknown, "unread session must not guess a status");
    check_key(status.string_key, "nearby.entry.unavailable",
              "unread session projects unavailable with reason key");
    check(status.text_zh == std::string_view("暂时无法确认联机状态"),
          "unavailable text explains the state instead of claiming connected");
}

// --- Stage timeline: first failure vs not-started ----------------------------

void test_stage_projection()
{
    using Stages = std::array<StageState, flynes::product::nearby::kStageCount>;
    Stages raw{};
    raw.fill(StageState::NotStarted);

    const StageProjection clean = project_stages(raw);
    check(clean.valid, "all not-started is a valid timeline");
    check(clean.first_failed == -1, "clean timeline has no failure");
    check(clean.states[static_cast<std::size_t>(Stage::Permissions)] == StageState::NotStarted,
          "untouched stages render as not started, never as failed");

    raw[static_cast<std::size_t>(Stage::Permissions)] = StageState::Passed;
    raw[static_cast<std::size_t>(Stage::Discovery)] = StageState::Passed;
    raw[static_cast<std::size_t>(Stage::Authentication)] = StageState::Passed;
    raw[static_cast<std::size_t>(Stage::Wifi)] = StageState::Failed;
    raw[static_cast<std::size_t>(Stage::Quic)] = StageState::NotStarted;
    raw[static_cast<std::size_t>(Stage::Version)] = StageState::NotStarted;
    raw[static_cast<std::size_t>(Stage::Codec)] = StageState::NotStarted;

    const StageProjection failed = project_stages(raw);
    check(failed.valid, "one failure with later stages untouched is valid");
    check(failed.first_failed == static_cast<int>(Stage::Wifi),
          "first failure is the wifi stage (D5 order: 权限→发现→认证→Wi-Fi→QUIC→版本→codec)");
    check(failed.states[static_cast<std::size_t>(Stage::Authentication)] == StageState::Passed,
          "stages before the failure show as passed");
    check(failed.states[static_cast<std::size_t>(Stage::Quic)] == StageState::NotStarted,
          "stages after the failure stay not-started, not red");

    check_key(stage_failure_key(Stage::Wifi), "nearby.stage.wifi.failed", "wifi failure reason key");
    check(stage_label_zh(Stage::Wifi) == std::string_view("Wi-Fi"), "wifi stage label");
    check_key(stage_failure_key(Stage::Permissions), "nearby.stage.permissions.failed",
              "permission failure reason key");

    Stages contradictory = raw;
    contradictory[static_cast<std::size_t>(Stage::Codec)] = StageState::Passed;
    const StageProjection invalid = project_stages(contradictory);
    check(!invalid.valid, "a stage after the failure marked passed contradicts the contract");
    check(!invalid.violation_key.empty(), "contradiction exposes a reason key, never success");

    Stages two_failures = raw;
    two_failures[static_cast<std::size_t>(Stage::Version)] = StageState::Failed;
    check(!project_stages(two_failures).valid,
          "two failures cannot be projected; only the first failure is displayable");
}

// --- Permission reasons ------------------------------------------------------

void test_permission_reasons()
{
    check(permission_denied_reason(Permission::Camera) == std::string_view("nearby.reason.permission.cameraDenied"),
          "camera denial reason key");
    check(permission_denied_reason(Permission::NearbyDiscovery) ==
              std::string_view("nearby.reason.permission.discoveryDenied"),
          "discovery denial reason key");
    check(permission_denied_reason(Permission::Wifi) == std::string_view("nearby.reason.permission.wifiDenied"),
          "wifi denial reason key");
}

// --- C04/C10: action availability --------------------------------------------

void test_n00_actions_without_game()
{
    ScreenContext context;
    const std::vector<ActionId> actions = available_actions(ScreenId::NearbyEntry, context);

    check(contains(actions, ActionId::CreateInvite), "C04: 创建联机 visible on N00");
    check(contains(actions, ActionId::EnterInviteCode), "C04: 输入配对码 visible on N00");
    check(contains(actions, ActionId::ScanQr), "C04: 扫码加入 visible on N00");
    check(contains(actions, ActionId::FindDevices), "N00 keeps 寻找设备");
    check(contains(actions, ActionId::OpenFriends), "N00 keeps 附近设备/好友 tabs entry");
    check(contains(actions, ActionId::BackToGameCenter), "N00 returns to the original lobby");
}

void test_camera_denial_keeps_code_path()
{
    ScreenContext denied;
    denied.camera_usable = false;
    const std::vector<ActionId> actions = available_actions(ScreenId::NearbyEntry, denied);

    check(!contains(actions, ActionId::ScanQr), "C10: camera denial removes the scan action");
    check(contains(actions, ActionId::EnterInviteCode), "C10: code path stays available");
    check(contains(actions, ActionId::FindDevices), "C10: discovery path unaffected by camera denial");

    ScreenContext on_join;
    on_join.camera_usable = false;
    on_join.join_code_complete = true;
    const std::vector<ActionId> join_actions = available_actions(ScreenId::JoinByCode, on_join);
    check(!contains(join_actions, ActionId::SwitchToScan),
          "C10: camera denial also gates the switch-to-scan entry");
    check(contains(join_actions, ActionId::SubmitJoinCode),
          "C10: submitting the code remains possible");
}

void test_join_code_gating()
{
    ScreenContext context;
    std::vector<ActionId> actions = available_actions(ScreenId::JoinByCode, context);
    check(!contains(actions, ActionId::SubmitJoinCode),
          "C05: incomplete code must not be submittable (no request sent)");

    context.join_code_complete = true;
    actions = available_actions(ScreenId::JoinByCode, context);
    check(contains(actions, ActionId::SubmitJoinCode), "complete six-digit code can be submitted");

    context.join_request_in_flight = true;
    actions = available_actions(ScreenId::JoinByCode, context);
    check(!contains(actions, ActionId::SubmitJoinCode),
          "C05/C16: submit is locked while a request for this generation is in flight");
    check(contains(actions, ActionId::CancelRequest), "in-flight request can be cancelled");
}

void test_screen_codes_are_stable()
{
    check_key(screen_code(ScreenId::NearbyEntry), "N00", "N00 code");
    check_key(screen_code(ScreenId::InviteCode), "N01", "N01 code");
    check_key(screen_code(ScreenId::JoinByCode), "N02", "N02 code");
    check_key(screen_code(ScreenId::ScanQr), "N03", "N03 code");
    check_key(screen_code(ScreenId::WaitHost), "N04", "N04 code");
    check_key(screen_code(ScreenId::HostRequest), "N05", "N05 code");
    check_key(screen_code(ScreenId::SasConfirm), "N06", "N06 code");
    check_key(screen_code(ScreenId::Connecting), "N07", "N07 code");
    check_key(screen_code(ScreenId::ConnectedLobby), "N08", "N08 code");
    check_key(screen_code(ScreenId::GameCenter), "G00", "G00 code");
    check_key(screen_code(ScreenId::GameConfig), "N09", "N09 code");
    check_key(screen_code(ScreenId::Failure), "N10", "N10 code");
    check_key(screen_code(ScreenId::ContentTransfer), "N11", "N11 code");
    check_key(screen_code(ScreenId::Friends), "N12", "N12 code");
}

// --- C11/C13: pending config invalidation ------------------------------------

void test_config_invalidation()
{
    PendingConfigFacts facts;
    facts.pending_config_id = "config-g1";
    facts.local_confirmed = true;
    facts.peer_confirmed = true;
    check(config_start_allowed(facts), "both confirmations on the same config allow start");

    const PendingConfigFacts after_host_change =
        invalidate_on_config_change(facts, "config-g2");
    check(after_host_change.pending_config_id == "config-g2", "C11: new fingerprint is bound");
    check(!after_host_change.local_confirmed, "C11: local confirmation invalidated");
    check(!after_host_change.peer_confirmed, "C11: peer confirmation invalidated");
    check(!config_start_allowed(after_host_change), "C11: start blocked until re-confirmed");

    PendingConfigFacts rebound;
    rebound.pending_config_id = "config-g2";
    rebound.local_confirmed = true;
    check(!config_start_allowed(rebound), "C07/C11: one-sided confirmation is not enough");

    const PendingConfigFacts stale = invalidate_on_config_change(rebound, "config-g2");
    check(stale.local_confirmed, "re-binding the identical config keeps existing confirmations");
}

// --- Static action list per remaining screen (contract shape) ----------------

void test_remaining_screen_actions()
{
    ScreenContext plain;
    const std::vector<ActionId> invite = available_actions(ScreenId::InviteCode, plain);
    check(contains(invite, ActionId::RegenerateInvite), "N01 offers 重新生成");
    check(contains(invite, ActionId::CancelInvite), "N01 offers 取消作废邀请");

    const std::vector<ActionId> wait_host = available_actions(ScreenId::WaitHost, plain);
    check(contains(wait_host, ActionId::CancelRequest), "N04 offers 取消请求");

    const std::vector<ActionId> host_request = available_actions(ScreenId::HostRequest, plain);
    check(contains(host_request, ActionId::AcceptRequest), "N05 offers 接受，继续验证");
    check(contains(host_request, ActionId::RejectRequest), "N05 offers 拒绝");

    const std::vector<ActionId> sas = available_actions(ScreenId::SasConfirm, plain);
    check(contains(sas, ActionId::ConfirmSasMatch), "N06 offers 数字一致");
    check(contains(sas, ActionId::ReportSasMismatch), "N06 offers 数字不一致，取消");

    const std::vector<ActionId> config = available_actions(ScreenId::GameConfig, plain);
    check(contains(config, ActionId::ConfirmConfig), "N09 offers 确认入局");
    check(contains(config, ActionId::BackToLobby), "N09 offers 返回大厅换游戏");

    const std::vector<ActionId> connected = available_actions(ScreenId::ConnectedLobby, plain);
    check(contains(connected, ActionId::GoToGameCenter), "N08 offers 去游戏大厅");
    check(contains(connected, ActionId::Disconnect), "N08 offers 断开");
}

void test_responsive_layout_contract()
{
    const NearbyLayout narrow = project_layout(580.0);
    check(!narrow.split, "C17: width 580 remains vertically stacked");
    check(narrow.left_width == 580.0 && narrow.gutter == 0.0
              && narrow.right_width == 580.0,
          "C17: stacked panes each consume the available width");

    const NearbyLayout wide = project_layout(736.0);
    check(wide.split, "C17: width above 580 uses two columns");
    check(wide.left_width == 224.0 && wide.gutter == 18.0
              && wide.right_width == 494.0,
          "C17: wide layout is 224 + 18 + remaining width");

    const NearbyLayout invalid = project_layout(-1.0);
    check(!invalid.valid, "negative available width is rejected");
}

} // namespace

int main()
{
    test_entry_statuses();
    test_unknown_facts_project_unavailable();
    test_stage_projection();
    test_permission_reasons();
    test_n00_actions_without_game();
    test_camera_denial_keeps_code_path();
    test_join_code_gating();
    test_screen_codes_are_stable();
    test_config_invalidation();
    test_remaining_screen_actions();
    test_responsive_layout_contract();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::fprintf(stdout, "test_product_nearby_ui: all checks passed\n");
    return 0;
}
