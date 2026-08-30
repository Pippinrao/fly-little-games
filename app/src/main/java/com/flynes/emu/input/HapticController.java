package com.flynes.emu.input;

import android.content.Context;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.view.HapticFeedbackConstants;
import android.view.View;

public final class HapticController {
    private final View view;
    private HapticLevel level = HapticLevel.LIGHT;
    private boolean distinguishAB = true;

    public HapticController(View view) {
        this.view = view;
    }

    public void configure(HapticLevel level, boolean distinguishAB) {
        this.level = level;
        this.distinguishAB = distinguishAB;
    }

    public void feedback(GamepadHitMap.Control control) {
        HapticPattern pattern = HapticPattern.forControl(control, level, distinguishAB);
        if (pattern.isNone()) return;
        Vibrator vibrator = vibrator();
        if (vibrator != null && vibrator.hasVibrator()) {
            try {
                long[] timings = pattern.timings();
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    vibrator.vibrate(VibrationEffect.createWaveform(
                            timings, pattern.amplitudes(), -1));
                } else {
                    vibrator.vibrate(timings, -1);
                }
                return;
            } catch (RuntimeException ignored) {
                // Vendor implementations occasionally reject custom waveforms; use system feedback.
            }
        }
        view.performHapticFeedback(feedbackConstant(control));
    }

    private int feedbackConstant(GamepadHitMap.Control control) {
        if (control == GamepadHitMap.Control.B && distinguishAB) {
            return HapticFeedbackConstants.LONG_PRESS;
        }
        if (control == GamepadHitMap.Control.START) {
            return HapticFeedbackConstants.CONTEXT_CLICK;
        }
        return HapticFeedbackConstants.KEYBOARD_TAP;
    }

    @SuppressWarnings("deprecation")
    private Vibrator vibrator() {
        Context context = view.getContext();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            VibratorManager manager = context.getSystemService(VibratorManager.class);
            return manager == null ? null : manager.getDefaultVibrator();
        }
        return (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
    }
}
