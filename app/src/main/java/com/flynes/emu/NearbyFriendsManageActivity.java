package com.flynes.emu;

import android.os.Bundle;
import android.view.View;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;

/** Fixed landscape friend actions. Unimplemented actions explain availability on tap. */
public final class NearbyFriendsManageActivity extends AppCompatActivity {

    private static final int[] UNSUPPORTED_CONTROLS = {
            R.id.nearby_manage_rename, R.id.nearby_manage_delete,
            R.id.nearby_manage_block, R.id.nearby_manage_identity_reset,
    };
    private static final int[] UNSUPPORTED_REASONS = {
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
        if (UNSUPPORTED_CONTROLS.length != UNSUPPORTED_REASONS.length) {
            throw new IllegalStateException("nearby manage control/reason table mismatch");
        }
        for (int i = 0; i < UNSUPPORTED_CONTROLS.length; i++) {
            MaterialButton control = findViewById(UNSUPPORTED_CONTROLS[i]);
            TextView reason = findViewById(UNSUPPORTED_REASONS[i]);
            control.setEnabled(true);
            control.setContentDescription(control.getText() + ", " + getString(R.string.nearby_not_supported));
            control.setOnClickListener(view -> android.widget.Toast.makeText(
                    this, R.string.nearby_not_supported, android.widget.Toast.LENGTH_SHORT).show());
        }
    }
}
