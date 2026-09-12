import { describe, it, expect } from '@ohos/hypium';
import {
  ControlLayoutWarningCode,
  ControlLayoutWarnings,
  ControlPlacement,
  UndoStack
} from '../../../main/ets/pages/ControlLayoutWarnings';

export default function controlLayoutWarningsTest() {
  describe('ControlLayoutWarnings', () => {
    it('flags_too_small_when_scale_below_067', 0, () => {
      const warnings = ControlLayoutWarnings.inspect([
        new ControlPlacement(0.1, 0.8, 0.66)
      ]);
      expect(warnings.contains(ControlLayoutWarningCode.TOO_SMALL)).assertTrue();
    });

    it('flags_gesture_zone_when_outside_safe_bounds', 0, () => {
      const warnings = ControlLayoutWarnings.inspect([
        new ControlPlacement(0.03, 0.5, 1)
      ]);
      expect(warnings.contains(ControlLayoutWarningCode.GESTURE_ZONE)).assertTrue();
    });

    it('flags_central_protection_when_center_x_in_middle', 0, () => {
      const warnings = ControlLayoutWarnings.inspect([
        new ControlPlacement(0.5, 0.5, 1)
      ]);
      expect(warnings.contains(ControlLayoutWarningCode.CENTRAL_PROTECTION)).assertTrue();
    });

    it('flags_overlap_when_controls_too_close', 0, () => {
      const warnings = ControlLayoutWarnings.inspect([
        new ControlPlacement(0.1, 0.8, 1),
        new ControlPlacement(0.11, 0.8, 1)
      ]);
      expect(warnings.contains(ControlLayoutWarningCode.OVERLAP)).assertTrue();
    });

    it('clean_recommended_layout_has_no_warnings', 0, () => {
      const warnings = ControlLayoutWarnings.inspect([
        new ControlPlacement(0.10, 0.76, 1),
        new ControlPlacement(0.94, 0.64, 1),
        new ControlPlacement(0.87, 0.86, 1),
        new ControlPlacement(0.09, 0.28, 1),
        new ControlPlacement(0.94, 0.28, 1)
      ]);
      expect(warnings.isEmpty()).assertTrue();
    });
  });

  describe('UndoStack', () => {
    it('push_caps_at_20_and_undo_pops_newest', 0, () => {
      const stack = new UndoStack<number>(20);
      for (let index = 0; index < 25; index++) {
        stack.push(index);
      }
      expect(stack.size()).assertEqual(20);
      expect(stack.undo()).assertEqual(24);
      expect(stack.undo()).assertEqual(23);
      expect(stack.size()).assertEqual(18);
    });

    it('undo_on_empty_returns_undefined', 0, () => {
      const stack = new UndoStack<string>(20);
      expect(stack.undo() === undefined).assertTrue();
    });
  });
}
