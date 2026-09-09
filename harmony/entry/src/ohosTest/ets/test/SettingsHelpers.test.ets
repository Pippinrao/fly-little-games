import { describe, it, expect } from '@ohos/hypium';
import {
  aspectModeLabelKey,
  hapticPreviewGapMs,
  hapticPreviewSequence,
  normalizeAspectMode,
  runtimeLanguageFor
} from '../../../main/ets/pages/SettingsHelpers';
import { computeViewport } from '../../../main/ets/pages/ViewportGeometry';
import {
  debugSideloadUrls,
  isDebugSideloadFixture,
  shouldDownloadSideloadFile
} from '../../../main/ets/platform/DebugSideload';

export default function settingsHelpersTest() {
  describe('SettingsHelpers', () => {
    it('maps_aspect_mode_values', 0, () => {
      expect(aspectModeLabelKey(1)).assertEqual('aspect_four_three');
      expect(aspectModeLabelKey(2)).assertEqual('aspect_square_pixels');
      expect(aspectModeLabelKey(3)).assertEqual('aspect_integer');
      expect(normalizeAspectMode(9)).assertEqual(1);
    });

    it('haptic_preview_plays_a_then_b_with_260ms_gap', 0, () => {
      expect(hapticPreviewGapMs()).assertEqual(260);
      const sequence = hapticPreviewSequence();
      expect(sequence.length).assertEqual(2);
      expect(sequence[0]).assertEqual('A');
      expect(sequence[1]).assertEqual('B');
    });

    it('computes_physical_4_3_square_and_integer_viewports', 0, () => {
      const fourThree = computeViewport(2844, 1260, 1, 256, 240);
      expect(fourThree.width).assertEqual(1680);
      expect(fourThree.height).assertEqual(1260);
      const square = computeViewport(2844, 1260, 2, 256, 240);
      expect(square.width).assertEqual(1344);
      expect(square.height).assertEqual(1260);
      const integer = computeViewport(2844, 1260, 3, 256, 240);
      expect(integer.width).assertEqual(1280);
      expect(integer.height).assertEqual(1200);
    });

    it('maps_saved_app_language_to_runtime_language', 0, () => {
      expect(runtimeLanguageFor('en', 'zh-Hans')).assertEqual('en');
      expect(runtimeLanguageFor('zh-Hans', 'en-US')).assertEqual('zh-Hans');
      expect(runtimeLanguageFor('system', 'en-US')).assertEqual('en-US');
    });

    it('uses_only_explicit_loopback_debug_sideload_endpoints', 0, () => {
      const urls: string[] = debugSideloadUrls();
      expect(urls.length).assertEqual(1);
      expect(urls[0]).assertEqual('http://127.0.0.1:18080');
      expect(shouldDownloadSideloadFile(false)).assertTrue();
      expect(shouldDownloadSideloadFile(true)).assertFalse();
      expect(isDebugSideloadFixture('flynes-tdd-slow-01.zip')).assertTrue();
      expect(isDebugSideloadFixture('slow-2048-roms.zip')).assertTrue();
      expect(isDebugSideloadFixture('my-favorite-game.zip')).assertFalse();
      expect(isDebugSideloadFixture('flynes-tdd-slow-01.nes')).assertFalse();
    });
  });
}
