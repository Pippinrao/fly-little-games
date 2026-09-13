import { describe, it, expect } from '@ohos/hypium';
import {
  NEARBY_BANNER_AUTHORITY_TIMEOUT,
  NEARBY_BANNER_FROZEN,
  NEARBY_BANNER_NONE,
  NEARBY_BANNER_RECONNECT,
  NEARBY_FRIEND_STORE_BLOCKED,
  NEARBY_INGAME_NO_AUTO_MERGE,
  NEARBY_STAGE_ORDER,
  NEARBY_STAGE_STATUS_CURRENT,
  NEARBY_STAGE_STATUS_NOT_REACHED,
  NEARBY_STAGE_STATUS_PASSED,
  NEARBY_WIFI_PATH_ORDER,
  nearbyAuthorityTimeoutOptions,
  nearbyInGameBanner,
  nearbyInGameRows,
  nearbyLobbyRows,
  nearbyManageActions,
  nearbyPairingCurrentStage,
  nearbyPairingStages,
  NearbyBanner,
  NearbyInGameRow,
  NearbyLobbyRow,
  NearbyStageRow,
  NearbyEntryFacts,
  normalizeInviteCode,
  projectNearbyEntry
} from '../../../main/ets/service/NearbyService';

export default function nearbyServiceTest() {
    // 2026-09-13 UI parity: entry status rule (C01/C09/C14) and code input rule (C05).
    it('projects_entry_status_from_real_facts_only', 0, () => {
      const clean: NearbyEntryFacts = {
        factsKnown: true, peerVerified: false, channelBound: false,
        compatibilityVerified: false, connectionEstablished: false,
        previousConnectionEstablished: false, reconnecting: false
      };
      expect(projectNearbyEntry(clean).status).assertEqual('disconnected');
      expect(projectNearbyEntry(clean).stringKey).assertEqual('nearby.open');
      const partial: NearbyEntryFacts = {
        factsKnown: true, peerVerified: true, channelBound: true,
        compatibilityVerified: false, connectionEstablished: false,
        previousConnectionEstablished: false, reconnecting: false
      };
      expect(projectNearbyEntry(partial).status).assertEqual('pairing');
      const connected: NearbyEntryFacts = {
        factsKnown: true, peerVerified: true, channelBound: true,
        compatibilityVerified: true, connectionEstablished: true,
        previousConnectionEstablished: false, reconnecting: false
      };
      expect(projectNearbyEntry(connected).status).assertEqual('connected');
      expect(projectNearbyEntry(connected).stringKey).assertEqual('nearby.entry.connected');
      const interrupted: NearbyEntryFacts = {
        factsKnown: true, peerVerified: false, channelBound: false,
        compatibilityVerified: false, connectionEstablished: false,
        previousConnectionEstablished: true, reconnecting: true
      };
      expect(projectNearbyEntry(interrupted).status).assertEqual('interrupted');
      const unknown: NearbyEntryFacts = {
        factsKnown: false, peerVerified: false, channelBound: false,
        compatibilityVerified: false, connectionEstablished: false,
        previousConnectionEstablished: false, reconnecting: false
      };
      expect(projectNearbyEntry(unknown).status).assertEqual('unknown');
    });

    it('invite_code_input_keeps_leading_zeros_and_rejects_the_rest', 0, () => {
      expect(normalizeInviteCode('012345')).assertEqual('012345');
      expect(normalizeInviteCode('  012345  ')).assertEqual('012345');
      expect(normalizeInviteCode('12345')).assertEqual('');
      expect(normalizeInviteCode('1234567')).assertEqual('');
      expect(normalizeInviteCode('12a456')).assertEqual('');
      expect(normalizeInviteCode('')).assertEqual('');
    });

  describe('NearbyService', () => {
    it('renders_the_seven_pipeline_stages_in_canonical_order', 0, () => {
      const rows: NearbyStageRow[] = nearbyPairingStages('nearby_stage_permission', NEARBY_STAGE_ORDER);
      expect(rows.length).assertEqual(7);
      expect(rows[0].label).assertEqual('nearby_stage_permission');
      expect(rows[1].label).assertEqual('nearby_stage_discovery');
      expect(rows[2].label).assertEqual('nearby_stage_auth');
      expect(rows[3].label).assertEqual('nearby_stage_wifi');
      expect(rows[4].label).assertEqual('nearby_stage_quic');
      expect(rows[5].label).assertEqual('nearby_stage_version');
      expect(rows[6].label).assertEqual('nearby_stage_codec');
    });

    it('marks_only_the_first_failing_stage_and_never_annotates_later_rows', 0, () => {
      const rows: NearbyStageRow[] = nearbyPairingStages('nearby_stage_permission', NEARBY_STAGE_ORDER);
      expect(rows[0].status).assertEqual(NEARBY_STAGE_STATUS_CURRENT);
      expect(rows[0].reason).assertEqual('nearby_stage_permission_reason');
      for (let index = 1; index < rows.length; index++) {
        expect(rows[index].status).assertEqual(NEARBY_STAGE_STATUS_NOT_REACHED);
        // Empty reason means neither a reason key nor a blocked key is rendered.
        expect(rows[index].reason).assertEqual('');
      }
    });

    it('shows_earlier_stages_as_passed_when_a_later_stage_fails', 0, () => {
      const rows: NearbyStageRow[] = nearbyPairingStages('nearby_stage_quic', NEARBY_STAGE_ORDER);
      expect(rows[0].status).assertEqual(NEARBY_STAGE_STATUS_PASSED);
      expect(rows[1].status).assertEqual(NEARBY_STAGE_STATUS_PASSED);
      expect(rows[2].status).assertEqual(NEARBY_STAGE_STATUS_PASSED);
      expect(rows[3].status).assertEqual(NEARBY_STAGE_STATUS_PASSED);
      expect(rows[4].status).assertEqual(NEARBY_STAGE_STATUS_CURRENT);
      expect(rows[4].reason).assertEqual('nearby_stage_quic_reason');
      expect(rows[5].status).assertEqual(NEARBY_STAGE_STATUS_NOT_REACHED);
      expect(rows[6].reason).assertEqual('');
    });

    it('falls_back_to_permission_because_no_session_stage_exists_yet', 0, () => {
      expect(nearbyPairingCurrentStage('')).assertEqual('nearby_stage_permission');
      expect(nearbyPairingCurrentStage('nearby_stage_wifi')).assertEqual('nearby_stage_wifi');
    });

    it('renders_every_lobby_field_with_its_blocked_key_and_one_confirm_control', 0, () => {
      const rows: NearbyLobbyRow[] = nearbyLobbyRows();
      expect(rows.length).assertEqual(15);
      let confirmRows = 0;
      for (let index = 0; index < rows.length; index++) {
        expect(rows[index].blocked.startsWith('nearby_blocked_')).assertTrue();
        if (rows[index].label === 'nearby_lobby_confirm') {
          confirmRows++;
        }
      }
      // D8: exactly one primary 确认入局 control, and it is blocked today.
      expect(confirmRows).assertEqual(1);
      expect(rows[13].label).assertEqual('nearby_lobby_confirm');
      expect(rows[13].blocked).assertEqual('nearby_blocked_session_read');
    });

    it('covers_every_in_game_field_and_keeps_the_no_auto_merge_invariant_unblocked', 0, () => {
      const rows: NearbyInGameRow[] = nearbyInGameRows();
      expect(rows.length).assertEqual(10);
      expect(rows[0].label).assertEqual('nearby_ingame_mode');
      expect(rows[4].label).assertEqual('nearby_ingame_frozen');
      expect(rows[6].label).assertEqual('nearby_ingame_authority_timeout');
      // D10: 明确不会自动合并 is always known, so it carries no blocked key.
      expect(rows[8].label).assertEqual(NEARBY_INGAME_NO_AUTO_MERGE);
      expect(rows[8].blocked).assertEqual('');
    });

    it('offers_the_three_authority_timeout_options_with_a_reason_each', 0, () => {
      const options = nearbyAuthorityTimeoutOptions();
      expect(options.length).assertEqual(3);
      expect(options[0].label).assertEqual('nearby_ingame_takeover');
      expect(options[1].label).assertEqual('nearby_ingame_continue_solo');
      expect(options[2].label).assertEqual('nearby_ingame_save_and_end');
      expect(options[0].reason).assertEqual('nearby_ingame_option_unavailable_reason');
    });

    it('pins_no_banner_in_local_single_player_and_no_banner_when_nothing_demands_action', 0, () => {
      // D7: no session means no nearby chrome at all.
      expect(nearbyInGameBanner(false, 2) === undefined).assertTrue();
      expect(nearbyInGameBanner(true, 0) === undefined).assertTrue();
    });

    it('pins_a_non_dismissible_banner_only_for_the_action_demanding_states', 0, () => {
      // `kind` is the state identity and `label` the resource key the page resolves, so both are
      // asserted: comparing label against a NEARBY_BANNER_* discriminator conflated the two, which
      // is how this emulator run found the bug.
      const frozen: NearbyBanner | undefined = nearbyInGameBanner(true, 2);
      expect(frozen?.kind).assertEqual(NEARBY_BANNER_FROZEN);
      expect(frozen?.label).assertEqual('nearby_ingame_frozen');
      expect(frozen?.actions).assertFalse();
      const reconnect: NearbyBanner | undefined = nearbyInGameBanner(true, 3);
      expect(reconnect?.kind).assertEqual(NEARBY_BANNER_RECONNECT);
      expect(reconnect?.label).assertEqual('nearby_ingame_reconnect_countdown');
      expect(reconnect?.actions).assertFalse();
      const timeout: NearbyBanner | undefined = nearbyInGameBanner(true, 4);
      expect(timeout?.kind).assertEqual(NEARBY_BANNER_AUTHORITY_TIMEOUT);
      expect(timeout?.label).assertEqual('nearby_ingame_authority_timeout');
      // Only the authority-timeout state offers 接管 / 继续单人 / 保存结束 (D6, D9).
      expect(timeout?.actions).assertTrue();
      // Every action-demanding state is one of the four discriminators, none of which is "no banner".
      expect(NEARBY_BANNER_NONE).assertEqual('none');
    });

    it('keeps_every_friends_management_action_disabled_on_the_friend_store', 0, () => {
      const actions = nearbyManageActions();
      expect(actions.length).assertEqual(4);
      for (let index = 0; index < actions.length; index++) {
        expect(actions[index].blocked).assertEqual(NEARBY_FRIEND_STORE_BLOCKED);
      }
      expect(actions[3].label).assertEqual('nearby_friends_identity_reset');
    });
  });
}
