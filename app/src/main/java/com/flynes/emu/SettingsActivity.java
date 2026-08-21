package com.flynes.emu;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.Switch;
import android.widget.TextView;

import com.flynes.emu.input.HapticLevel;

/** Small, persistent control-settings screen used by the playable alpha. */
public final class SettingsActivity extends Activity {
    static final String PREFS = "flynes_settings";
    static final String KEY_HAPTIC_LEVEL = "controls.haptic_level";
    static final String KEY_DISTINCT_AB = "controls.distinct_ab";

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        SharedPreferences preferences = getSharedPreferences(PREFS, MODE_PRIVATE);
        int padding = dp(24);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(padding, padding, padding, padding);
        root.setGravity(Gravity.CENTER_VERTICAL);
        root.setBackgroundColor(0xFF151A23);

        TextView title = new TextView(this);
        title.setText(R.string.control_feedback);
        title.setTextColor(Color.WHITE);
        title.setTextSize(24f);
        root.addView(title, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        RadioGroup levels = new RadioGroup(this);
        levels.setOrientation(RadioGroup.HORIZONTAL);
        addLevel(levels, R.id.haptic_off, R.string.haptic_off, HapticLevel.OFF);
        addLevel(levels, R.id.haptic_light, R.string.haptic_light, HapticLevel.LIGHT);
        addLevel(levels, R.id.haptic_standard, R.string.haptic_standard, HapticLevel.STANDARD);
        addLevel(levels, R.id.haptic_strong, R.string.haptic_strong, HapticLevel.STRONG);
        HapticLevel current = loadHapticLevel(this);
        levels.check(idFor(current));
        levels.setOnCheckedChangeListener((group, checkedId) -> preferences.edit()
                .putString(KEY_HAPTIC_LEVEL, levelFor(checkedId).name()).apply());
        root.addView(levels, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(64)));

        Switch distinct = new Switch(this);
        distinct.setId(R.id.haptic_distinct);
        distinct.setText(R.string.distinct_ab_haptics);
        distinct.setTextColor(Color.WHITE);
        distinct.setTextSize(18f);
        distinct.setChecked(loadDistinctAB(this));
        distinct.setOnCheckedChangeListener((button, checked) -> preferences.edit()
                .putBoolean(KEY_DISTINCT_AB, checked).apply());
        root.addView(distinct, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(56)));
        setContentView(root);
    }

    static HapticLevel loadHapticLevel(Context context) {
        String value = context.getSharedPreferences(PREFS, MODE_PRIVATE)
                .getString(KEY_HAPTIC_LEVEL, HapticLevel.LIGHT.name());
        try {
            return HapticLevel.valueOf(value);
        } catch (IllegalArgumentException ignored) {
            return HapticLevel.LIGHT;
        }
    }

    static boolean loadDistinctAB(Context context) {
        return context.getSharedPreferences(PREFS, MODE_PRIVATE)
                .getBoolean(KEY_DISTINCT_AB, true);
    }

    private void addLevel(RadioGroup group, int id, int label, HapticLevel level) {
        RadioButton button = new RadioButton(this);
        button.setId(id);
        button.setText(label);
        button.setTextColor(Color.WHITE);
        button.setTag(level);
        group.addView(button, new RadioGroup.LayoutParams(0,
                ViewGroup.LayoutParams.MATCH_PARENT, 1f));
    }

    private static HapticLevel levelFor(int id) {
        if (id == R.id.haptic_off) return HapticLevel.OFF;
        if (id == R.id.haptic_standard) return HapticLevel.STANDARD;
        if (id == R.id.haptic_strong) return HapticLevel.STRONG;
        return HapticLevel.LIGHT;
    }

    private static int idFor(HapticLevel level) {
        switch (level) {
            case OFF: return R.id.haptic_off;
            case STANDARD: return R.id.haptic_standard;
            case STRONG: return R.id.haptic_strong;
            default: return R.id.haptic_light;
        }
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
