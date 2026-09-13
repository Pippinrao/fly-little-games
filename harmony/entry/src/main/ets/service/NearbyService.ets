// Decision logic for the A1b nearby pages (pairing / lobby / in-game status /
// friends management). Everything here is a pure function over the canonical
// vocabulary of docs/superpowers/plans/2026-09-11-nearby-ui-spine-slice-spec.md
// §2, so the pages stay thin and the rules are unit-testable without a device.
//
// The pages keep literal `$r('app.string.…')` calls; this module only names the
// resources so a test can assert which label/status a row must render.

/** Resource name without the `app.string.` prefix, e.g. `nearby_stage_wifi`. */
export type NearbyStringKey = string;

/**
 * Resolves a key named by this module to the resource the page renders. Kept in
 * one place so every user-visible string still goes through `$r('app.string.…')`
 * while the decision functions stay free of UI types.
 *
 * Two of the keys it resolves are page titles that spec §2 does not list and A1
 * adds: `nearby_pairing_title` (Pairing / 配对) and `nearby_lobby_title`
 * (Lobby / 大厅). Each child page — including 好友管理, which uses §2's
 * `nearby_friends_manage` — is titled with its own name; `nearby_title` names the
 * whole feature and is only the entry/page-group name. The comment recording that
 * belongs here because the resource compiler rejects comments inside
 * `resources/**\/element/string.json`.
 */
export function nearbyString(name: NearbyStringKey): Resource {
  if (name === 'nearby_lobby_title') {
    return $r('app.string.nearby_lobby_title');
  }
  if (name === 'nearby_pairing_title') {
    return $r('app.string.nearby_pairing_title');
  }
  if (name === 'nearby_title') {
    return $r('app.string.nearby_title');
  }
  if (name === 'nearby_blocked_session_read') {
    return $r('app.string.nearby_blocked_session_read');
  }
  if (name === 'nearby_blocked_friend_store') {
    return $r('app.string.nearby_blocked_friend_store');
  }
  if (name === 'nearby_blocked_auth') {
    return $r('app.string.nearby_blocked_auth');
  }
  if (name === 'nearby_blocked_authority_recovery') {
    return $r('app.string.nearby_blocked_authority_recovery');
  }
  if (name === 'nearby_blocked_branch_merge') {
    return $r('app.string.nearby_blocked_branch_merge');
  }
  if (name === 'nearby_blocked_codec') {
    return $r('app.string.nearby_blocked_codec');
  }
  if (name === 'nearby_blocked_connection_quality') {
    return $r('app.string.nearby_blocked_connection_quality');
  }
  if (name === 'nearby_blocked_discovery') {
    return $r('app.string.nearby_blocked_discovery');
  }
  if (name === 'nearby_blocked_local_mute') {
    return $r('app.string.nearby_blocked_local_mute');
  }
  if (name === 'nearby_blocked_mode_gate') {
    return $r('app.string.nearby_blocked_mode_gate');
  }
  if (name === 'nearby_blocked_permissions') {
    return $r('app.string.nearby_blocked_permissions');
  }
  if (name === 'nearby_blocked_profile_verify') {
    return $r('app.string.nearby_blocked_profile_verify');
  }
  if (name === 'nearby_blocked_quic') {
    return $r('app.string.nearby_blocked_quic');
  }
  if (name === 'nearby_blocked_rom_transfer') {
    return $r('app.string.nearby_blocked_rom_transfer');
  }
  if (name === 'nearby_blocked_version') {
    return $r('app.string.nearby_blocked_version');
  }
  if (name === 'nearby_blocked_wifi') {
    return $r('app.string.nearby_blocked_wifi');
  }
  if (name === 'nearby_code_confirm') {
    return $r('app.string.nearby_code_confirm');
  }
  if (name === 'nearby_code_label') {
    return $r('app.string.nearby_code_label');
  }
  if (name === 'nearby_devices_empty') {
    return $r('app.string.nearby_devices_empty');
  }
  if (name === 'nearby_friends_block') {
    return $r('app.string.nearby_friends_block');
  }
  if (name === 'nearby_friends_delete') {
    return $r('app.string.nearby_friends_delete');
  }
  if (name === 'nearby_friends_empty') {
    return $r('app.string.nearby_friends_empty');
  }
  if (name === 'nearby_friends_identity_reset') {
    return $r('app.string.nearby_friends_identity_reset');
  }
  if (name === 'nearby_friends_manage') {
    return $r('app.string.nearby_friends_manage');
  }
  if (name === 'nearby_friends_rename') {
    return $r('app.string.nearby_friends_rename');
  }
  if (name === 'nearby_friends_saved_after_auth') {
    return $r('app.string.nearby_friends_saved_after_auth');
  }
  if (name === 'nearby_friends_section') {
    return $r('app.string.nearby_friends_section');
  }
  if (name === 'nearby_ingame_authority_timeout') {
    return $r('app.string.nearby_ingame_authority_timeout');
  }
  if (name === 'nearby_ingame_branch_no_auto_merge') {
    return $r('app.string.nearby_ingame_branch_no_auto_merge');
  }
  if (name === 'nearby_ingame_branch_reunion') {
    return $r('app.string.nearby_ingame_branch_reunion');
  }
  if (name === 'nearby_ingame_connection_quality') {
    return $r('app.string.nearby_ingame_connection_quality');
  }
  if (name === 'nearby_ingame_continue_solo') {
    return $r('app.string.nearby_ingame_continue_solo');
  }
  if (name === 'nearby_ingame_frozen') {
    return $r('app.string.nearby_ingame_frozen');
  }
  if (name === 'nearby_ingame_frozen_reason') {
    return $r('app.string.nearby_ingame_frozen_reason');
  }
  if (name === 'nearby_ingame_reconnect_countdown_reason') {
    return $r('app.string.nearby_ingame_reconnect_countdown_reason');
  }
  if (name === 'nearby_find_devices') {
    return $r('app.string.nearby_find_devices');
  }
  if (name === 'nearby_open') {
    return $r('app.string.nearby_open');
  }
  if (name === 'nearby_scan_host_qr') {
    return $r('app.string.nearby_scan_host_qr');
  }
  if (name === 'nearby_ingame_local_mute') {
    return $r('app.string.nearby_ingame_local_mute');
  }
  if (name === 'nearby_ingame_mode') {
    return $r('app.string.nearby_ingame_mode');
  }
  if (name === 'nearby_ingame_option_unavailable_reason') {
    return $r('app.string.nearby_ingame_option_unavailable_reason');
  }
  if (name === 'nearby_ingame_pause_state') {
    return $r('app.string.nearby_ingame_pause_state');
  }
  if (name === 'nearby_ingame_reconnect_countdown') {
    return $r('app.string.nearby_ingame_reconnect_countdown');
  }
  if (name === 'nearby_ingame_save_and_end') {
    return $r('app.string.nearby_ingame_save_and_end');
  }
  if (name === 'nearby_ingame_seat') {
    return $r('app.string.nearby_ingame_seat');
  }
  if (name === 'nearby_ingame_takeover') {
    return $r('app.string.nearby_ingame_takeover');
  }
  if (name === 'nearby_lobby_authority_capability') {
    return $r('app.string.nearby_lobby_authority_capability');
  }
  if (name === 'nearby_lobby_confirm') {
    return $r('app.string.nearby_lobby_confirm');
  }
  if (name === 'nearby_lobby_confirm_invalidated') {
    return $r('app.string.nearby_lobby_confirm_invalidated');
  }
  if (name === 'nearby_lobby_friend_name') {
    return $r('app.string.nearby_lobby_friend_name');
  }
  if (name === 'nearby_lobby_identity_fingerprint') {
    return $r('app.string.nearby_lobby_identity_fingerprint');
  }
  if (name === 'nearby_lobby_local_audio') {
    return $r('app.string.nearby_lobby_local_audio');
  }
  if (name === 'nearby_lobby_mode_expected') {
    return $r('app.string.nearby_lobby_mode_expected');
  }
  if (name === 'nearby_lobby_network_owner') {
    return $r('app.string.nearby_lobby_network_owner');
  }
  if (name === 'nearby_lobby_profile_verified') {
    return $r('app.string.nearby_lobby_profile_verified');
  }
  if (name === 'nearby_lobby_resource_risk') {
    return $r('app.string.nearby_lobby_resource_risk');
  }
  if (name === 'nearby_lobby_rom_identity') {
    return $r('app.string.nearby_lobby_rom_identity');
  }
  if (name === 'nearby_lobby_rom_local_state') {
    return $r('app.string.nearby_lobby_rom_local_state');
  }
  if (name === 'nearby_lobby_rom_transfer_confirm') {
    return $r('app.string.nearby_lobby_rom_transfer_confirm');
  }
  if (name === 'nearby_lobby_rom_transfer_progress') {
    return $r('app.string.nearby_lobby_rom_transfer_progress');
  }
  if (name === 'nearby_lobby_seat') {
    return $r('app.string.nearby_lobby_seat');
  }
  if (name === 'nearby_stage_auth') {
    return $r('app.string.nearby_stage_auth');
  }
  if (name === 'nearby_stage_auth_reason') {
    return $r('app.string.nearby_stage_auth_reason');
  }
  if (name === 'nearby_stage_codec') {
    return $r('app.string.nearby_stage_codec');
  }
  if (name === 'nearby_stage_codec_reason') {
    return $r('app.string.nearby_stage_codec_reason');
  }
  if (name === 'nearby_stage_discovery') {
    return $r('app.string.nearby_stage_discovery');
  }
  if (name === 'nearby_stage_discovery_reason') {
    return $r('app.string.nearby_stage_discovery_reason');
  }
  if (name === 'nearby_stage_permission') {
    return $r('app.string.nearby_stage_permission');
  }
  if (name === 'nearby_stage_permission_reason') {
    return $r('app.string.nearby_stage_permission_reason');
  }
  if (name === 'nearby_stage_quic') {
    return $r('app.string.nearby_stage_quic');
  }
  if (name === 'nearby_stage_quic_reason') {
    return $r('app.string.nearby_stage_quic_reason');
  }
  if (name === 'nearby_stage_status_current') {
    return $r('app.string.nearby_stage_status_current');
  }
  if (name === 'nearby_stage_status_not_reached') {
    return $r('app.string.nearby_stage_status_not_reached');
  }
  if (name === 'nearby_stage_status_passed') {
    return $r('app.string.nearby_stage_status_passed');
  }
  if (name === 'nearby_stage_version') {
    return $r('app.string.nearby_stage_version');
  }
  if (name === 'nearby_stage_version_reason') {
    return $r('app.string.nearby_stage_version_reason');
  }
  if (name === 'nearby_stage_wifi') {
    return $r('app.string.nearby_stage_wifi');
  }
  if (name === 'nearby_stage_wifi_reason') {
    return $r('app.string.nearby_stage_wifi_reason');
  }
  if (name === 'nearby_tab_devices') {
    return $r('app.string.nearby_tab_devices');
  }
  if (name === 'nearby_tab_friends') {
    return $r('app.string.nearby_tab_friends');
  }
  if (name === 'nearby_wifi_path_building') {
    return $r('app.string.nearby_wifi_path_building');
  }
  if (name === 'nearby_wifi_system_confirm_once') {
    return $r('app.string.nearby_wifi_system_confirm_once');
  }
  return $r('app.string.ok');
}

/** The single source of truth for stage order: pipeline order = display order (D5). */
export const NEARBY_STAGE_ORDER: string[] = [
  'nearby_stage_permission',
  'nearby_stage_discovery',
  'nearby_stage_auth',
  'nearby_stage_wifi',
  'nearby_stage_quic',
  'nearby_stage_version',
  'nearby_stage_codec'
];

// §2.1: every stage keeps both keys — a static name and a reported reason (D11).
const STAGE_NAME: NearbyStringKey[] = [
  'nearby_stage_permission',
  'nearby_stage_discovery',
  'nearby_stage_auth',
  'nearby_stage_wifi',
  'nearby_stage_quic',
  'nearby_stage_version',
  'nearby_stage_codec'
];
const STAGE_REASON: NearbyStringKey[] = [
  'nearby_stage_permission_reason',
  'nearby_stage_discovery_reason',
  'nearby_stage_auth_reason',
  'nearby_stage_wifi_reason',
  'nearby_stage_quic_reason',
  'nearby_stage_version_reason',
  'nearby_stage_codec_reason'
];

export const NEARBY_STAGE_STATUS_PASSED: NearbyStringKey = 'nearby_stage_status_passed';
export const NEARBY_STAGE_STATUS_CURRENT: NearbyStringKey = 'nearby_stage_status_current';
export const NEARBY_STAGE_STATUS_NOT_REACHED: NearbyStringKey = 'nearby_stage_status_not_reached';

/**
 * One row of the §2.1 pipeline list. `reason` is present on the first failing
 * stage only: earlier stages are passed, later stages are neutral not-yet-reached
 * rows with no blocked key and no reason (D5, §2 resolved reading).
 */
export interface NearbyStageRow {
  stage: NearbyStringKey;
  label: NearbyStringKey;
  status: NearbyStringKey;
  /** Empty unless this is the first failing stage. */
  reason: NearbyStringKey;
}

// Android/Harmony can truthfully report 权限 as DENIED from an undeclared query;
// the session ABI cannot report a stage yet, so 权限 stays the first failure (D1).
const STAGE_PERMISSION_FALLBACK: NearbyStringKey = STAGE_NAME[0];

/**
 * Wi-Fi path block: §4 requires all five stages `permission…codec` rendered in
 * pipeline order, so the path reuses the canonical order rather than a subset.
 */
export const NEARBY_WIFI_PATH_ORDER: string[] = NEARBY_STAGE_ORDER;

/** Index of `stage` in `order`, or `order.length` when unknown. */
export function nearbyStageIndex(stage: string, order: string[]): number {
  for (let index = 0; index < order.length; index++) {
    if (order[index] === stage) {
      return index;
    }
  }
  return order.length;
}

/** §2.1 display order restricted to `order`; unknown stages are dropped. */
export function nearbyStageNames(order: string[]): NearbyStringKey[] {
  const names: NearbyStringKey[] = [];
  for (let stage = 0; stage < STAGE_NAME.length; stage++) {
    for (let index = 0; index < order.length; index++) {
      if (order[index] === STAGE_NAME[stage]) {
        names.push(STAGE_NAME[stage]);
        break;
      }
    }
  }
  return names;
}

/**
 * Marks exactly one stage as current (the first failing one) and never annotates
 * the unmarked rows with a reason or a blocked key (D5).
 */
export function nearbyPairingStages(currentStage: string, order: string[]): NearbyStageRow[] {
  const stages = nearbyStageNames(order);
  const current = nearbyStageIndex(currentStage, stages);
  const rows: NearbyStageRow[] = [];
  for (let index = 0; index < stages.length; index++) {
    const stage = stages[index];
    const labelIndex = STAGE_NAME.indexOf(stage);
    let status: NearbyStringKey = NEARBY_STAGE_STATUS_NOT_REACHED;
    if (index < current) {
      status = NEARBY_STAGE_STATUS_PASSED;
    } else if (index === current) {
      status = NEARBY_STAGE_STATUS_CURRENT;
    }
    rows.push({
      stage: stage,
      label: STAGE_NAME[labelIndex],
      status: status,
      reason: index === current ? STAGE_REASON[labelIndex] : ''
    });
  }
  return rows;
}

/** §4: the pairing page marks 权限 until the session ABI reports the real stage. */
export function nearbyPairingCurrentStage(sessionStage: string): NearbyStringKey {
  return sessionStage.length > 0 ? sessionStage : STAGE_PERMISSION_FALLBACK;
}

/** §2.2 lobby rows, in the design's order. Every value is session-owned. */
export interface NearbyLobbyRow {
  label: NearbyStringKey;
  /** Blocked key actually rendered by the row. */
  blocked: NearbyStringKey;
}

const LOBBY_LABELS: NearbyStringKey[] = [
  'nearby_lobby_friend_name',
  'nearby_lobby_identity_fingerprint',
  'nearby_lobby_authority_capability',
  'nearby_lobby_resource_risk',
  'nearby_lobby_network_owner',
  'nearby_lobby_seat',
  'nearby_lobby_rom_identity',
  'nearby_lobby_rom_local_state',
  'nearby_lobby_rom_transfer_confirm',
  'nearby_lobby_rom_transfer_progress',
  'nearby_lobby_profile_verified',
  'nearby_lobby_mode_expected',
  'nearby_lobby_local_audio',
  'nearby_lobby_confirm',
  'nearby_lobby_confirm_invalidated'
];

// §3: a field with a specific blocked key uses it; everything else falls back to
// the generic session-read key, which is never used on the pipeline list.
const LOBBY_BLOCKED: NearbyStringKey[] = [
  'nearby_blocked_session_read',
  'nearby_blocked_session_read',
  'nearby_blocked_session_read',
  'nearby_blocked_session_read',
  'nearby_blocked_session_read',
  'nearby_blocked_session_read',
  'nearby_blocked_rom_transfer',
  'nearby_blocked_rom_transfer',
  'nearby_blocked_rom_transfer',
  'nearby_blocked_rom_transfer',
  'nearby_blocked_profile_verify',
  'nearby_blocked_mode_gate',
  'nearby_blocked_local_mute',
  'nearby_blocked_session_read',
  'nearby_blocked_session_read'
];

export function nearbyLobbyRows(): NearbyLobbyRow[] {
  const rows: NearbyLobbyRow[] = [];
  for (let index = 0; index < LOBBY_LABELS.length; index++) {
    rows.push({ label: LOBBY_LABELS[index], blocked: LOBBY_BLOCKED[index] });
  }
  return rows;
}

/** §2.2 in-game field rows; every value needs the session surface (§3). */
export interface NearbyInGameRow {
  label: NearbyStringKey;
  blocked: NearbyStringKey;
}

const IN_GAME_LABELS: NearbyStringKey[] = [
  'nearby_ingame_mode',
  'nearby_ingame_connection_quality',
  'nearby_ingame_seat',
  'nearby_ingame_pause_state',
  'nearby_ingame_frozen',
  'nearby_ingame_reconnect_countdown',
  'nearby_ingame_authority_timeout',
  'nearby_ingame_branch_reunion',
  'nearby_ingame_branch_no_auto_merge',
  'nearby_ingame_local_mute'
];

const IN_GAME_BLOCKED: NearbyStringKey[] = [
  // §3: the mode shares the lobby's `nearby.lobby.mode_expected` source, so it uses
  // that gate; every other §22.4 field reads the session and uses the generic key.
  'nearby_blocked_mode_gate',
  'nearby_blocked_session_read',
  'nearby_blocked_session_read',
  'nearby_blocked_session_read',
  'nearby_blocked_session_read',
  'nearby_blocked_session_read',
  'nearby_blocked_session_read',
  'nearby_blocked_session_read',
  // D10: 明确不会自动合并 is an always-known invariant, not a blocked row.
  '',
  'nearby_blocked_session_read'
];

export function nearbyInGameRows(): NearbyInGameRow[] {
  const rows: NearbyInGameRow[] = [];
  for (let index = 0; index < IN_GAME_LABELS.length; index++) {
    rows.push({ label: IN_GAME_LABELS[index], blocked: IN_GAME_BLOCKED[index] });
  }
  return rows;
}

/** D10: the one always-known in-game line, at zero ABI cost. */
export const NEARBY_INGAME_NO_AUTO_MERGE: NearbyStringKey = 'nearby_ingame_branch_no_auto_merge';

// §2.3 authoritative-timeout three-action list (D9). Unavailable options are
// shown disabled with a specific reason; no wire text ever reaches the UI.
export interface NearbyActionOption {
  label: NearbyStringKey;
  reason: NearbyStringKey;
}

export function nearbyAuthorityTimeoutOptions(): NearbyActionOption[] {
  return [
    { label: 'nearby_ingame_takeover', reason: 'nearby_ingame_option_unavailable_reason' },
    { label: 'nearby_ingame_continue_solo', reason: 'nearby_ingame_option_unavailable_reason' },
    { label: 'nearby_ingame_save_and_end', reason: 'nearby_ingame_option_unavailable_reason' }
  ];
}

// ---------------------------------------------------------------- in-game banner
// D6: no persistent HUD. Only the action-demanding states pin a non-dismissible
// banner to the top of the run surface; a modal is explicitly wrong.
export const NEARBY_BANNER_NONE: string = 'none';
export const NEARBY_BANNER_FROZEN: string = 'frozen';
export const NEARBY_BANNER_RECONNECT: string = 'reconnect';
export const NEARBY_BANNER_AUTHORITY_TIMEOUT: string = 'authority_timeout';

export interface NearbyBanner {
  /**
   * Which action-demanding state this is, one of the `NEARBY_BANNER_*` discriminators. It is the
   * machine-readable identity; `label` and `reason` are resource keys the page resolves through
   * `nearbyString()`. Both exist because a caller has to be able to branch on the state without
   * comparing translated text, and the page must never render a raw key.
   */
  kind: string;
  label: NearbyStringKey;
  reason: NearbyStringKey;
  /** True only for the authority-timeout state, which offers 接管/继续单人/保存结束. */
  actions: boolean;
}

// Reasons for the two banner states that have a specific one in the vocabulary.
// Only the authority-timeout state fell back to a blocked key because `frozen`
// and `reconnect_countdown` are states, not blocked capabilities.
export const NEARBY_INGAME_FROZEN_REASON: NearbyStringKey = 'nearby_ingame_frozen_reason';
export const NEARBY_INGAME_RECONNECT_REASON: NearbyStringKey = 'nearby_ingame_reconnect_countdown_reason';

/**
 * D7: with no session there is no nearby block at all — not even a blocked row.
 * `uiState` is `fly_session_snapshot.ui_state`; this build has no session surface,
 * so the caller passes `FLY_SESSION_UI_UNSPECIFIED` (0) and gets no banner.
 */
export function nearbyInGameBanner(sessionExists: boolean, uiState: number): NearbyBanner | undefined {
  if (!sessionExists) {
    return undefined;
  }
  if (uiState === 2) {
    return {
      kind: NEARBY_BANNER_FROZEN,
      label: 'nearby_ingame_frozen',
      reason: NEARBY_INGAME_FROZEN_REASON,
      actions: false
    };
  }
  if (uiState === 3) {
    return {
      kind: NEARBY_BANNER_RECONNECT,
      label: 'nearby_ingame_reconnect_countdown',
      reason: NEARBY_INGAME_RECONNECT_REASON,
      actions: false
    };
  }
  if (uiState === 4) {
    return {
      kind: NEARBY_BANNER_AUTHORITY_TIMEOUT,
      label: 'nearby_ingame_authority_timeout',
      reason: 'nearby_blocked_authority_recovery',
      actions: true
    };
  }
  return undefined;
}

// ------------------------------------------------------------ friends management
/** §4 / §10 D2: 好友管理 — present, disabled, `nearby.blocked.friend_store`. */
export interface NearbyManageAction {
  label: NearbyStringKey;
  blocked: NearbyStringKey;
}

export const NEARBY_FRIEND_STORE_BLOCKED: NearbyStringKey = 'nearby_blocked_friend_store';

export function nearbyManageActions(): NearbyManageAction[] {
  return [
    { label: 'nearby_friends_rename', blocked: NEARBY_FRIEND_STORE_BLOCKED },
    { label: 'nearby_friends_delete', blocked: NEARBY_FRIEND_STORE_BLOCKED },
    { label: 'nearby_friends_block', blocked: NEARBY_FRIEND_STORE_BLOCKED },
    { label: 'nearby_friends_identity_reset', blocked: NEARBY_FRIEND_STORE_BLOCKED }
  ];
}
