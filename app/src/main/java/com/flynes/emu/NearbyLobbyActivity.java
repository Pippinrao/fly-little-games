package com.flynes.emu;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
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
 * <p>The mockup first screen is game / host / seat plus the footer confirm. Technical §22.3
 * fields stay behind the details control. {@link #FIRST_SCREEN} and
 * {@link #DETAILS} are the tables of "field label → why this field is unavailable", and
 * {@link #buildRow} turns one entry into one accessibility node.
 *
 * <p>Why the reasons differ per field instead of all saying "no session": the spec's §3 table maps
 * each lobby field to the capability it actually waits on, so 座位 and 预计模式 wait on the mode gate
 * (no {@code FLY_SESSION_MODE_*} values exist), the ROM rows wait on ROM identity and transfer,
 * MultiplayerProfile waits on profile verification, and the rest wait on the session snapshot. A user
 * reading one generic reason could not tell which part of the design is missing.
 *
 * <p>D8 binds the one primary confirmation action to the displayed pending configuration. The
 * session engine owns both confirmation flags; this page never starts a run or confirms a peer.
 */
public final class NearbyLobbyActivity extends AppCompatActivity {

    private int boundLinkState;
    private NearbySessionOwner owner;
    private MaterialButton confirm;
    private TextView confirmReason;
    private NearbySessionOwner.Snapshot displayedSnapshot;
    private final Handler refreshHandler = new Handler(Looper.getMainLooper());
    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            bindSnapshot();
            if (owner != null) refreshHandler.postDelayed(this, 250L);
        }
    };

    private static final int[][] FIRST_SCREEN = {
            {R.id.nearby_lobby_row_rom_identity, R.string.nearby_lobby_rom_identity,
                    R.string.nearby_blocked_rom_transfer},
            {R.id.nearby_lobby_row_network_owner, R.string.nearby_lobby_network_owner,
                    R.string.nearby_blocked_session_read},
            {R.id.nearby_lobby_row_seat, R.string.nearby_lobby_seat,
                    R.string.nearby_blocked_mode_gate},
    };

    /** Diagnostic fields stay in details, not on the mockup first screen. */
    private static final int[][] DETAILS = {
            {R.id.nearby_lobby_row_friend_name, R.string.nearby_lobby_friend_name,
                    R.string.nearby_blocked_session_read},
            {R.id.nearby_lobby_row_identity_fingerprint, R.string.nearby_lobby_identity_fingerprint,
                    R.string.nearby_blocked_session_read},
            {R.id.nearby_lobby_row_authority_capability, R.string.nearby_lobby_authority_capability,
                    R.string.nearby_blocked_session_read},
            {R.id.nearby_lobby_row_resource_risk, R.string.nearby_lobby_resource_risk,
                    R.string.nearby_blocked_session_read},
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
        for (int[] field : FIRST_SCREEN) {
            rows.addView(buildRow(field, rows));
        }
        MaterialButton details = new MaterialButton(this, null,
                com.google.android.material.R.attr.materialButtonOutlinedStyle);
        details.setId(R.id.nearby_lobby_details);
        details.setText(R.string.nearby_diagnostics_title);
        details.setMinHeight(getResources().getDimensionPixelSize(R.dimen.fly_touch_min));
        LinearLayout detailsRows = new LinearLayout(this);
        detailsRows.setId(R.id.nearby_lobby_details_rows);
        detailsRows.setOrientation(LinearLayout.VERTICAL);
        detailsRows.setVisibility(View.GONE);
        for (int[] field : DETAILS) {
            detailsRows.addView(buildRow(field, detailsRows));
        }
        details.setOnClickListener(view -> detailsRows.setVisibility(
                detailsRows.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE));
        rows.addView(details);
        rows.addView(detailsRows);

        confirm = findViewById(R.id.nearby_lobby_confirm);
        confirmReason = findViewById(R.id.nearby_lobby_confirm_reason);
        confirm.setOnClickListener(view -> {
            NearbySessionOwner.Snapshot displayed = displayedSnapshot;
            if (owner == null || displayed == null || !displayed.canConfirmGameConfig()) return;
            owner.confirmGameConfig(displayed.pendingConfigId(), displayed.pendingConfigRevision);
            bindSnapshot();
        });
        FlyNesApplication app = (FlyNesApplication) getApplication();
        NearbyAvailability.Status nearby = app.ensureNearby();
        owner = app.nearbySessionOwner();
        if (!nearby.ready() || owner == null) {
            boundLinkState = NearbySessionOwner.LINK_UNAVAILABLE;
            confirm.setEnabled(false);
            if (nearby.reasonKey() != null) {
                int reasonId = getResources().getIdentifier(
                        nearby.reasonKey(), "string", getPackageName());
                if (reasonId != 0) confirmReason.setText(reasonId);
            }
            owner = null;
            confirm.setContentDescription(confirm.getText() + ", " + confirmReason.getText());
            return;
        }
        bindSnapshot();
    }

    @Override protected void onStart() {
        super.onStart();
        refreshHandler.removeCallbacks(refresh);
        if (owner != null) refreshHandler.post(refresh);
    }

    @Override protected void onStop() {
        refreshHandler.removeCallbacks(refresh);
        super.onStop();
    }

    private void bindSnapshot() {
        if (owner == null) return;
        try {
            displayedSnapshot = owner.snapshot();
            boundLinkState = displayedSnapshot.linkState;
            confirm.setEnabled(displayedSnapshot.canConfirmGameConfig());
            String reasonKey = displayedSnapshot.primaryReasonKey;
            int reasonId = reasonKey.isEmpty() ? 0 : getResources().getIdentifier(
                    reasonKey.replace('.', '_'), "string", getPackageName());
            if (reasonId != 0) confirmReason.setText(reasonId);
            else if (!reasonKey.isEmpty()) confirmReason.setText(reasonKey);
            else if (displayedSnapshot.pendingConfigLocalConfirmed != 0)
                confirmReason.setText(R.string.nearby_config_confirmed);
            else if (displayedSnapshot.canConfirmGameConfig())
                confirmReason.setText(R.string.nearby_config_waitingConfirm);
            else confirmReason.setText(R.string.nearby_blocked_session_read);
        } catch (IllegalStateException unavailable) {
            displayedSnapshot = null;
            boundLinkState = NearbySessionOwner.LINK_UNAVAILABLE;
            confirm.setEnabled(false);
            confirmReason.setText(R.string.nearby_blocked_session_read);
        }
        confirm.setContentDescription(confirm.getText() + ", " + confirmReason.getText());
    }

    public int boundLinkState() {
        return boundLinkState;
    }

    /** First-screen field count: game / host / seat. Details are behind a separate control. */
    public static int fieldCount() {
        return FIRST_SCREEN.length;
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
