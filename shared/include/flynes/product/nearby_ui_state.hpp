#pragma once

// Pure product projection for the approved nearby multiplayer UX
// (design docs/superpowers/specs/2026-09-13-nearby-ui-parity-design.md).
//
// This layer owns no platform API, no I/O, and no network result of its own:
// it only projects shared session facts into the connection status, screen
// actions, stage timeline, permission reasons, and per-game config
// confirmation state that the three platform UIs must render identically.
// The normative screen/reason/string/layout tables live in
// shared/schema/nearby_ui_v1.json; the identifiers below must stay in sync
// with that contract (tools/quality/check_nearby_ui_contract.py enforces the
// platform string coverage).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace flynes::product::nearby {

inline constexpr double kNearbySplitThreshold = 580.0;
inline constexpr double kNearbyLeftColumnWidth = 224.0;
inline constexpr double kNearbyColumnGutter = 18.0;

struct NearbyLayout
{
    bool valid = false;
    bool split = false;
    double left_width = 0.0;
    double gutter = 0.0;
    double right_width = 0.0;
};

[[nodiscard]] NearbyLayout project_layout(double available_width) noexcept;

// ---------------------------------------------------------------------------
// Screen identifiers (design §4). These are semantic test/adaptation IDs, not
// a requirement to create one native page per value.
// ---------------------------------------------------------------------------
enum class ScreenId : uint8_t
{
    NearbyEntry = 0,   // N00
    InviteCode,        // N01
    JoinByCode,        // N02
    ScanQr,            // N03
    WaitHost,          // N04
    HostRequest,       // N05
    SasConfirm,        // N06
    Connecting,        // N07
    ConnectedLobby,    // N08
    GameCenter,        // G00
    GameConfig,        // N09
    Failure,           // N10
    ContentTransfer,   // N11
    Friends,           // N12
};

[[nodiscard]] std::string_view screen_code(ScreenId screen);

// ---------------------------------------------------------------------------
// Connection status (design §3.1).
// ---------------------------------------------------------------------------
enum class ConnectionStatus : uint8_t
{
    Unknown = 0,     // session facts not read yet; never guesses "connected"
    Disconnected,    // 无认证连接
    Pairing,         // 请求/认证/建网中
    Connected,       // 身份、通道及兼容性检查全部完成
    Interrupted,     // 已建立的连接中断
};

// Only real shared-session state may enter this struct. Nothing here may be
// derived from elapsed time, page paths, test flags, or persisted booleans.
struct SessionFacts
{
    bool facts_known = false;               // false: session state not read yet
    bool peer_verified = false;             // 双方身份完成（SAS 或签名邀请握手）
    bool channel_bound = false;             // ChannelBind ACK milestone reached
    bool compatibility_verified = false;    // 版本/codec 协商完成
    bool connection_established = false;    // 建网完成
    bool previous_connection_established = false;
    bool reconnecting = false;
};

struct EntryStatus
{
    ConnectionStatus status = ConnectionStatus::Unknown;
    std::string_view string_key;   // canonical UI contract key
    std::string_view text_zh;      // contract Chinese text
};

// Entry-status projection (design §3.1):
//   all of established+verified+bound+compatibility -> "双人联机中"
//   previously established and reconnecting         -> "联机中断"
//   otherwise                                        -> "附近联机"
// Unknown facts project as unavailable with a reason instead of guessing.
// Scan success, host acceptance, a correct invite code, or a single-side SAS
// confirmation can never produce Connected on their own; they only ever set
// the partial facts above, which stay below the Connected bar.
[[nodiscard]] EntryStatus project_entry(const SessionFacts& facts);

// ---------------------------------------------------------------------------
// Stage timeline (design §4.2): 权限 → 发现 → 认证 → Wi-Fi → QUIC → 版本 → codec
// ---------------------------------------------------------------------------
enum class Stage : uint8_t
{
    Permissions = 0,
    Discovery,
    Authentication,
    Wifi,
    Quic,
    Version,
    Codec,
    Count,
};

enum class StageState : uint8_t
{
    NotStarted = 0,
    InProgress,
    Passed,
    Failed,
};

inline constexpr std::size_t kStageCount = static_cast<std::size_t>(Stage::Count);

struct StageProjection
{
    std::array<StageState, kStageCount> states{};
    int first_failed = -1;   // index of the first failed stage, -1 when none
    bool valid = false;      // false: input contradicts the ordering contract
    std::string_view violation_key;  // reason key when !valid, else empty
};

// Validates and normalizes a raw per-stage timeline for display:
//   - at most one failed stage (the first failure);
//   - stages after the first failure must be NotStarted and are projected as
//     such ("不要把未开始的步骤全标红");
//   - stages before the failure must be Passed/InProgress.
// A timeline that marks a later stage Passed/InProgress/Failed after an
// earlier failure is invalid: the caller must render the unavailable reason
// instead of a success-looking timeline.
[[nodiscard]] StageProjection project_stages(
    const std::array<StageState, kStageCount>& raw);

[[nodiscard]] std::string_view stage_code(Stage stage);
[[nodiscard]] std::string_view stage_label_zh(Stage stage);
[[nodiscard]] std::string_view stage_state_label_zh(StageState state);
[[nodiscard]] std::string_view stage_failure_key(Stage stage);

// ---------------------------------------------------------------------------
// Permissions (design §4.2). Denials map to stable reason keys; a camera
// denial must never disable the code/discovery paths, and denials must not
// loop the system prompt (that policy is enforced per platform).
// ---------------------------------------------------------------------------
enum class Permission : uint8_t
{
    Camera = 0,
    NearbyDiscovery,
    Wifi,
};

enum class PermissionOutcome : uint8_t
{
    NotRequested = 0,
    Requesting,
    Granted,
    Denied,
};

// Reason key for a denied permission; empty when not denied.
[[nodiscard]] std::string_view permission_denied_reason(Permission permission);

// ---------------------------------------------------------------------------
// Actions per screen (design §4 table + C04/C10 gating rules).
// ---------------------------------------------------------------------------
enum class ActionId : uint8_t
{
    CreateInvite = 0,
    EnterInviteCode,
    ScanQr,
    FindDevices,
    OpenFriends,
    BackToGameCenter,
    RegenerateInvite,
    CancelInvite,
    SubmitJoinCode,
    SwitchToScan,
    SwitchToJoinCode,
    Cancel,
    CancelRequest,
    AcceptRequest,
    RejectRequest,
    ConfirmSasMatch,
    ReportSasMismatch,
    CancelConnecting,
    ViewDetails,
    GoToGameCenter,
    Disconnect,
    OpenNearby,
    SelectGame,
    DisableMultiplayerFilter,
    ConfirmConfig,
    BackToLobby,
    Retry,
    NewInvite,
    BackToEntry,
    GrantSend,
    GrantReceive,
    ConfirmImport,
    CancelTransfer,
    RenameFriend,
    DeleteFriend,
    BlockFriend,
    ResetIdentity,
    Count,
};

inline constexpr std::size_t kActionCount = static_cast<std::size_t>(ActionId::Count);

// Runtime context that gates action availability on top of the static
// per-screen action list declared in the UI contract.
struct ScreenContext
{
    bool camera_usable = true;            // camera permission granted
    bool discovery_usable = true;         // nearby discovery usable
    bool join_code_complete = false;      // six digits entered (leading zeros kept)
    bool join_request_in_flight = false;  // a request for the current generation is submitted
    bool multiplayer_zero_result = false; // current category empty under the filter
};

// Available actions for a screen under the given context. Actions whose
// preconditions are unmet are omitted entirely (a disabled control still
// shows its disabled reason; this list is what must be clickable).
[[nodiscard]] std::vector<ActionId> available_actions(ScreenId screen,
                                                      const ScreenContext& context);

[[nodiscard]] std::string_view action_code(ActionId action);

// ---------------------------------------------------------------------------
// Per-game pending config confirmation (design §5, cases C11/C13).
// ---------------------------------------------------------------------------
struct PendingConfigFacts
{
    std::string pending_config_id;  // fingerprint of the full pending config
    bool local_confirmed = false;
    bool peer_confirmed = false;
};

// Any change of the bound config (game identity, host, seat, mode, capability
// plan) immediately invalidates BOTH confirmations and rebinds to the new
// fingerprint. The UI short code is display-only and never a substitute for
// the fingerprint.
[[nodiscard]] PendingConfigFacts invalidate_on_config_change(
    const PendingConfigFacts& current, std::string new_config_id);

// Both sides confirmed the same pending config; only then may the game start.
[[nodiscard]] bool config_start_allowed(const PendingConfigFacts& facts);

} // namespace flynes::product::nearby
