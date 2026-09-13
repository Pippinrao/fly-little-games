package com.flynes.emu;

import android.os.Bundle;
import android.view.View;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;

/**
 * 好友管理 — rename / delete / block / identity reset (spec §8 A1a-5, design §22.1).
 *
 * <p>Reached from the 好友 tab (a real navigation row) and from the 好友管理 row inside Settings
 * (decision D2). All four actions are present and disabled with the specific reason, because the
 * design requires users to find them and this build has no friend store to act on; a working-looking
 * control over no store is the failure mode the spec's truthful-placeholder contract exists to
 * prevent.
 *
 * <p>Each disabled control repeats its reason to accessibility services, so focusing 拉黑 explains
 * that control rather than pointing at a shared note.
 */
public final class NearbyFriendsManageActivity extends AppCompatActivity {

    private static final int[] DISABLED_CONTROLS = {
            R.id.nearby_manage_rename, R.id.nearby_manage_delete,
            R.id.nearby_manage_block, R.id.nearby_manage_identity_reset,
    };
    private static final int[] DISABLED_REASONS = {
            R.id.nearby_manage_rename_reason, R.id.nearby_manage_delete_reason,
            R.id.nearby_manage_block_reason, R.id.nearby_manage_identity_reset_reason,
    };

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.activity_nearby_friends_manage);

        MaterialToolbar toolbar = findViewById(R.id.nearby_manage_toolbar);
        toolbar.setNavigationOnClickListener(view -> finish());

        View root = findViewById(R.id.nearby_manage_root);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(insets.getSystemWindowInsetLeft(),
                    insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(),
                    insets.getSystemWindowInsetBottom());
            return insets;
        });
        root.requestApplyInsets();

        // The two arrays are parallel and the index is the control; a length mismatch would silently
        // pair a control with another control's reason, so it fails loudly instead.
        if (DISABLED_CONTROLS.length != DISABLED_REASONS.length) {
            throw new IllegalStateException("nearby manage control/reason table mismatch");
        }
        for (int i = 0; i < DISABLED_CONTROLS.length; i++) {
            MaterialButton control = findViewById(DISABLED_CONTROLS[i]);
            TextView reason = findViewById(DISABLED_REASONS[i]);
            control.setContentDescription(control.getText() + ", " + reason.getText());
        }
    }
}
