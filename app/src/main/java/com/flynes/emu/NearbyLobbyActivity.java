package com.flynes.emu;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.content.Intent;

import java.io.IOException;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;

/**
 * Fixed landscape game / host / seat summary with paged technical details.
 * The existing session owner retains the pending-configuration confirmation guard.
 */
public final class NearbyLobbyActivity extends AppCompatActivity {

    private int boundLinkState;
    private NearbySessionOwner owner;
    private NearbyMvpOwner mvpOwner;
    private NearbyMvpSession mvpSession;
    private boolean playStarted;
    private MaterialButton confirm;
    private TextView confirmReason;
    private NearbySessionOwner.Snapshot displayedSnapshot;
    private final Handler refreshHandler = new Handler(Looper.getMainLooper());
    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            bindSnapshot();
            if (owner != null || mvpSession != null) refreshHandler.postDelayed(this, 250L);
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
        toolbar.setNavigationOnClickListener(view -> {
            if (mvpOwner != null) mvpOwner.close();
            finish();
        });

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
        findViewById(R.id.nearby_lobby_details).setOnClickListener(view -> showDetail(0));

        confirm = findViewById(R.id.nearby_lobby_confirm);
        confirmReason = findViewById(R.id.nearby_lobby_confirm_reason);
        confirm.setOnClickListener(view -> {
            if (mvpSession != null) {
                if (!mvpSession.confirm()) {
                    android.widget.Toast.makeText(this, R.string.nearby_not_supported,
                            android.widget.Toast.LENGTH_SHORT).show();
                }
                bindSnapshot();
                return;
            }
            NearbySessionOwner.Snapshot displayed = displayedSnapshot;
            if (owner == null || displayed == null || !displayed.canConfirmGameConfig()) {
                android.widget.Toast.makeText(this, R.string.nearby_not_supported,
                        android.widget.Toast.LENGTH_SHORT).show();
                return;
            }
            owner.confirmGameConfig(displayed.pendingConfigId(), displayed.pendingConfigRevision);
            bindSnapshot();
        });
        FlyNesApplication app = (FlyNesApplication) getApplication();
        mvpOwner = app.nearbyMvpOwner();
        if (mvpOwner != null && mvpOwner.active()) {
            mvpSession = mvpOwner.session();
            configureLocalGameIfNeeded();
            bindSnapshot();
            return;
        }
        NearbyAvailability.Status nearby = app.ensureNearby();
        owner = app.nearbySessionOwner();
        if (!nearby.ready() || owner == null) {
            boundLinkState = NearbySessionOwner.LINK_UNAVAILABLE;
            confirm.setEnabled(true);
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
        if (owner != null || mvpSession != null) refreshHandler.post(refresh);
    }

    @Override protected void onStop() {
        refreshHandler.removeCallbacks(refresh);
        super.onStop();
    }

    private void bindSnapshot() {
        if (mvpSession != null) {
            int[] snapshot = mvpSession.snapshot();
            if (snapshot == null || snapshot.length < 9) return;
            configureLocalGameIfNeeded();
            boundLinkState = snapshot[0];
            LinearLayout gameRow = findViewById(R.id.nearby_lobby_row_rom_identity);
            boolean localHost = snapshot[4] == 1;
            String gameTitle = mvpOwner.gameTitle();
            ((TextView) gameRow.getChildAt(1)).setText(localHost
                    ? gameTitle + " · " + getString(R.string.nearby_choose_game)
                    : gameTitle);
            gameRow.setContentDescription(localHost
                    ? gameTitle + ", " + getString(R.string.nearby_choose_game)
                    : gameTitle);
            if (localHost) {
                gameRow.setOnClickListener(view -> {
                    if (mvpSession.returnLobby()) startActivity(new Intent(this, HomeActivity.class)
                            .putExtra("nearby_choose_game", true));
                });
            } else {
                gameRow.setOnClickListener(null);
                gameRow.setClickable(false);
            }
            LinearLayout hostRow = findViewById(R.id.nearby_lobby_row_network_owner);
            ((TextView) hostRow.getChildAt(1)).setText(localHost ? "Android · P1" : "P1");
            LinearLayout seatRow = findViewById(R.id.nearby_lobby_row_seat);
            ((TextView) seatRow.getChildAt(1)).setText(localHost ? "Android · P1" : "Android · P2");
            confirm.setEnabled(snapshot[0] == NearbyMvpSession.CONFIGURING && snapshot[5] != 0 && snapshot[6] != 0 && snapshot[7] == 0);
            confirm.setText(snapshot[7] != 0
                    ? R.string.nearby_config_confirmed : R.string.nearby_lobby_confirm);
            if (snapshot[0] == NearbyMvpSession.ENDED) {
                confirmReason.setText(R.string.nearby_mvp_connection_failed);
            } else if (snapshot[7] != 0 && snapshot[8] == 0) {
                confirmReason.setText(R.string.nearby_config_waitingConfirm);
            } else if (snapshot[5] != 0 && snapshot[6] != 0) {
                confirmReason.setText(R.string.nearby_config_waitingConfirm);
            } else {
                confirmReason.setText(R.string.nearby_screen_connecting);
            }
            confirmReason.setVisibility(View.VISIBLE);
            confirm.setContentDescription(confirm.getText() + ", " + confirmReason.getText());
            if (snapshot[0] == NearbyMvpSession.RUNNING && !playStarted) {
                playStarted = true;
                startActivity(new Intent(this, MainActivity.class).putExtra("nearby_mvp", true));
                finish();
            }
            return;
        }
        if (owner == null) return;
        try {
            displayedSnapshot = owner.snapshot();
            boundLinkState = displayedSnapshot.linkState;
            confirm.setEnabled(displayedSnapshot.pendingConfigLocalConfirmed == 0);
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
            boolean hasStatus = displayedSnapshot.canConfirmGameConfig()
                    || displayedSnapshot.pendingConfigLocalConfirmed != 0 || !reasonKey.isEmpty();
            confirmReason.setVisibility(hasStatus ? View.VISIBLE : View.GONE);
            confirm.setText(displayedSnapshot.pendingConfigLocalConfirmed != 0
                    ? R.string.nearby_config_confirmed : R.string.nearby_lobby_confirm);
        } catch (IllegalStateException unavailable) {
            displayedSnapshot = null;
            boundLinkState = NearbySessionOwner.LINK_UNAVAILABLE;
            confirm.setEnabled(true);
            confirmReason.setText(R.string.nearby_blocked_session_read);
            confirmReason.setVisibility(View.VISIBLE);
        }
        confirm.setContentDescription(confirm.getText() + ", " + confirmReason.getText());
    }

    private void configureLocalGameIfNeeded() {
        if (mvpSession == null) return;
        int[] snapshot = mvpSession.snapshot();
        if (snapshot == null || snapshot.length < 9 || snapshot[5] != 0) return;
        try {
            boolean localHost = snapshot[4] == 1;
            NearbyMvpGame.Selection selection;
            boolean selected;
            if (localHost && snapshot[0] == NearbyMvpSession.LOBBY) {
                // A retained title means the host deliberately returned to the existing
                // game picker; do not race that choice by auto-selecting the default.
                if (!mvpOwner.gameTitle().isEmpty()) return;
                selection = NearbyMvpGame.load(this);
                selected = mvpSession.selectGame(selection.rom, selection.entry.canonicalId);
            } else if (!localHost && snapshot[0] == NearbyMvpSession.CONFIGURING) {
                String key = mvpSession.peerGameKey();
                if (key.isEmpty()) return;
                selection = NearbyMvpGame.load(this, key);
                selected = mvpSession.selectRom(selection.rom);
            } else {
                return;
            }
            if (!selected) throw new IOException("ROM rejected");
            mvpOwner.gameTitle(java.util.Locale.getDefault().getLanguage().equals("zh")
                    ? selection.entry.titleZhHans : selection.entry.titleEn);
        } catch (IOException failure) {
            confirm.setEnabled(false);
            confirmReason.setText(R.string.nearby_blocked_rom_transfer);
            confirmReason.setVisibility(View.VISIBLE);
        }
    }

    private void showDetail(int index) {
        int[] field = DETAILS[index];
        com.google.android.material.dialog.MaterialAlertDialogBuilder dialog =
                new com.google.android.material.dialog.MaterialAlertDialogBuilder(this)
                .setTitle(getString(field[1]) + "  " + (index + 1) + "/" + DETAILS.length)
                .setMessage(field[2])
                .setNeutralButton(R.string.nearby_action_cancel, null);
        if (index > 0) dialog.setNegativeButton(R.string.nearby_details_previous,
                (which, action) -> showDetail(index - 1));
        if (index + 1 < DETAILS.length) dialog.setPositiveButton(R.string.nearby_details_next,
                (which, action) -> showDetail(index + 1));
        dialog.show();
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
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
        params.setMarginStart(parent.getChildCount() == 0 ? 0 : Math.round(
                16 * getResources().getDisplayMetrics().density));
        row.setLayoutParams(params);
        row.setGravity(android.view.Gravity.CENTER_VERTICAL);

        TextView label = (TextView) row.getChildAt(0);
        TextView reason = (TextView) row.getChildAt(1);
        label.setText(field[1]);
        reason.setText(R.string.nearby_not_supported);

        // Label first, then the reason it is unavailable: one stop per field, and the reason can
        // never be announced before the field it belongs to (spec §2.3, `color-not-only`).
        row.setContentDescription(label.getText() + ", " + reason.getText());
        return row;
    }
}
