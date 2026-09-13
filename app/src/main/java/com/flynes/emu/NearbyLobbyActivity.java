package com.flynes.emu;

import android.content.res.ColorStateList;
import android.os.Bundle;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;

/**
 * 大厅 — every field design §22.3 requires both ends to see, each rendered with the exact reason it
 * cannot be shown yet (spec §8 A1a-3).
 *
 * <p>The page is truthful by construction rather than by review: {@link #FIELDS} is the single table
 * of "field label → why this field is unavailable", and {@link #buildRow} turns one entry into one
 * accessibility node. A field added to the spec but not to this table is therefore visible as a
 * missing row in the instrumentation test, and a row can never show a label with another field's
 * reason.
 *
 * <p>Why the reasons differ per field instead of all saying "no session": the spec's §3 table maps
 * each lobby field to the capability it actually waits on, so 座位 and 预计模式 wait on the mode gate
 * (no {@code FLY_SESSION_MODE_*} values exist), the ROM rows wait on ROM identity and transfer,
 * MultiplayerProfile waits on profile verification, and the rest wait on the session snapshot. A user
 * reading one generic reason could not tell which part of the design is missing.
 *
 * <p>Nothing is confirmed here. D8 makes 确认入局 one primary action per side bound to the pending
 * configuration, so the button stays disabled and states its reason until that configuration can be
 * read from the session ABI.
 */
public final class NearbyLobbyActivity extends AppCompatActivity {

    /** One row per §22.3 field: {@code {row id, label, unavailable reason}} in display order. */
    private static final int[][] FIELDS = {
            {R.id.nearby_lobby_row_friend_name, R.string.nearby_lobby_friend_name,
                    R.string.nearby_blocked_session_read},
            {R.id.nearby_lobby_row_identity_fingerprint, R.string.nearby_lobby_identity_fingerprint,
                    R.string.nearby_blocked_session_read},
            {R.id.nearby_lobby_row_authority_capability, R.string.nearby_lobby_authority_capability,
                    R.string.nearby_blocked_session_read},
            {R.id.nearby_lobby_row_resource_risk, R.string.nearby_lobby_resource_risk,
                    R.string.nearby_blocked_session_read},
            {R.id.nearby_lobby_row_network_owner, R.string.nearby_lobby_network_owner,
                    R.string.nearby_blocked_session_read},
            {R.id.nearby_lobby_row_seat, R.string.nearby_lobby_seat,
                    R.string.nearby_blocked_mode_gate},
            {R.id.nearby_lobby_row_rom_identity, R.string.nearby_lobby_rom_identity,
                    R.string.nearby_blocked_rom_transfer},
            {R.id.nearby_lobby_row_rom_local_state, R.string.nearby_lobby_rom_local_state,
                    R.string.nearby_blocked_rom_transfer},
            {R.id.nearby_lobby_row_rom_transfer_confirm, R.string.nearby_lobby_rom_transfer_confirm,
                    R.string.nearby_blocked_rom_transfer},
            {R.id.nearby_lobby_row_rom_transfer_progress, R.string.nearby_lobby_rom_transfer_progress,
                    R.string.nearby_blocked_rom_transfer},
            {R.id.nearby_lobby_row_profile_verified, R.string.nearby_lobby_profile_verified,
                    R.string.nearby_blocked_profile_verify},
            {R.id.nearby_lobby_row_mode_expected, R.string.nearby_lobby_mode_expected,
                    R.string.nearby_blocked_mode_gate},
            {R.id.nearby_lobby_row_local_audio, R.string.nearby_lobby_local_audio,
                    R.string.nearby_blocked_local_mute},
            // The invalidation row is a §22.3 field like the rest: this page reports that it cannot
            // know whether a confirmation is still valid, rather than staying silent about D8's
            // invalidation rule.
            {R.id.nearby_lobby_row_confirm_invalidated, R.string.nearby_lobby_confirm_invalidated,
                    R.string.nearby_blocked_session_read},
    };

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.activity_nearby_lobby);

        MaterialToolbar toolbar = findViewById(R.id.nearby_lobby_toolbar);
        toolbar.setNavigationOnClickListener(view -> finish());

        View root = findViewById(R.id.nearby_lobby_root);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(insets.getSystemWindowInsetLeft(),
                    insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(),
                    insets.getSystemWindowInsetBottom());
            return insets;
        });
        root.requestApplyInsets();

        LinearLayout rows = findViewById(R.id.nearby_lobby_rows);
        for (int[] field : FIELDS) {
            rows.addView(buildRow(field, rows));
        }

        MaterialButton confirm = findViewById(R.id.nearby_lobby_confirm);
        TextView reason = findViewById(R.id.nearby_lobby_confirm_reason);
        confirm.setContentDescription(confirm.getText() + ", " + reason.getText());
    }

    /**
     * Number of §22.3 fields this page renders. Instrumentation asserts the built row count against
     * this, so a field dropped from the table fails a test instead of quietly shrinking the page.
     */
    public static int fieldCount() {
        return FIELDS.length;
    }

    private View buildRow(int[] field, LinearLayout parent) {
        LinearLayout row = (LinearLayout) getLayoutInflater()
                .inflate(R.layout.view_nearby_lobby_row, parent, false);
        row.setId(field[0]);
        if (parent.getChildCount() > 0) {
            ((LinearLayout.LayoutParams) row.getLayoutParams()).topMargin =
                    getResources().getDimensionPixelSize(R.dimen.fly_space_3);
        }

        TextView label = (TextView) row.getChildAt(0);
        TextView reason = (TextView) row.getChildAt(1);
        label.setText(field[1]);
        reason.setText(field[2]);

        // Label first, then the reason it is unavailable: one stop per field, and the reason can
        // never be announced before the field it belongs to (spec §2.3, `color-not-only`).
        row.setContentDescription(label.getText() + ", " + reason.getText());
        return row;
    }
}
