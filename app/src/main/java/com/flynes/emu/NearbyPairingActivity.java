package com.flynes.emu;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.textfield.TextInputEditText;

/**
 * 配对 — the three entry-mode flows of the approved design (N01/N02/N03):
 * create (invite lifecycle), join-by-code (six-digit input), scan.
 *
 * <p>Everything the user can do here follows the shared route contract: the
 * six-digit input keeps leading zeros, never sends a request on incomplete
 * input, and locks submit while a request for the current generation is in
 * flight (C05/C16). The invite lifecycle kills the previous generation and its
 * QR on regenerate/cancel and never revives it. No part of this page may
 * announce a pairing: the stage pipeline below stays the single source for
 * which stage blocks, and it is wired to the session ABI as that track lands.
 *
 * <p>Field errors render directly under the input and appear only after the
 * user submits or completes the field - never per keystroke.
 */
public final class NearbyPairingActivity extends AppCompatActivity {

    public static final String MODE_CREATE = "create";
    public static final String MODE_JOIN_CODE = "join_code";
    public static final String MODE_SCAN = "scan";
    private static final String EXTRA_MODE = "nearby_mode";

    private NearbyInviteHostState invite;
    private NearbySession nearbySession;
    private final Handler ticker = new Handler(Looper.getMainLooper());
    private String mode = "";
    private final NearbyJoinSubmitState joinSubmit = new NearbyJoinSubmitState();
    private long requestGeneration;

    public static void start(@NonNull Context context, @NonNull String mode) {
        context.startActivity(intentFor(context, mode));
    }

    public static Intent scanIntent(@NonNull Context context) {
        return intentFor(context, MODE_SCAN);
    }

    private static Intent intentFor(@NonNull Context context, @NonNull String mode) {
        Intent intent = new Intent(context, NearbyPairingActivity.class);
        intent.putExtra(EXTRA_MODE, mode);
        return intent;
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.activity_nearby_pairing);
        nearbySession = ((FlyNesApplication) getApplication()).nearbySession();
        invite = new NearbyInviteHostState(nearbySession);

        MaterialToolbar toolbar = findViewById(R.id.nearby_pairing_toolbar);
        toolbar.setNavigationOnClickListener(view -> finish());

        View root = findViewById(R.id.nearby_pairing_root);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(insets.getSystemWindowInsetLeft(),
                    insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(),
                    insets.getSystemWindowInsetBottom());
            return insets;
        });
        root.requestApplyInsets();

        mode = getIntent() == null || getIntent().getStringExtra(EXTRA_MODE) == null
                ? "" : getIntent().getStringExtra(EXTRA_MODE);
        if (MODE_CREATE.equals(mode)) {
            showCreateBlock();
        } else if (MODE_JOIN_CODE.equals(mode)) {
            showJoinBlock();
        } else if (MODE_SCAN.equals(mode)) {
            showScanBlock();
        }
        applyResponsiveColumns();
    }

    private void applyResponsiveColumns() {
        View root = findViewById(R.id.nearby_pairing_root);
        root.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight,
                                        oldBottom) -> {
            if (right - left != oldRight - oldLeft) {
                layoutPairingColumns();
            }
        });
        root.post(this::layoutPairingColumns);
    }

    private void layoutPairingColumns() {
        View root = findViewById(R.id.nearby_pairing_root);
        LinearLayout columns = findViewById(R.id.nearby_pairing_columns);
        if (root.getWidth() == 0 || columns == null) return;
        float density = getResources().getDisplayMetrics().density;
        float contentAfterInsets = (root.getWidth() - root.getPaddingLeft()
                - root.getPaddingRight()) / density;
        boolean split = contentAfterInsets > 580f;
        columns.setOrientation(split ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);

        int leftWidth = split ? Math.round(224f * density) : ViewGroup.LayoutParams.MATCH_PARENT;
        setBlockWidth(R.id.nearby_create_block, leftWidth);
        setBlockWidth(R.id.nearby_join_block, leftWidth);
        setBlockWidth(R.id.nearby_scan_block, leftWidth);

        LinearLayout right = findViewById(R.id.nearby_pairing_right);
        LinearLayout.LayoutParams rightParams = (LinearLayout.LayoutParams) right.getLayoutParams();
        rightParams.width = split ? 0 : ViewGroup.LayoutParams.MATCH_PARENT;
        rightParams.weight = split ? 1f : 0f;
        rightParams.leftMargin = split ? Math.round(18f * density) : 0;
        rightParams.topMargin = split ? 0 : Math.round(18f * density);
        right.setLayoutParams(rightParams);
        layoutPairingFooter(split);
    }

    private void setBlockWidth(int id, int width) {
        View block = findViewById(id);
        LinearLayout.LayoutParams params = (LinearLayout.LayoutParams) block.getLayoutParams();
        params.width = width;
        block.setLayoutParams(params);
    }

    private void layoutPairingFooter(boolean split) {
        LinearLayout footer = findViewById(R.id.nearby_pairing_footer);
        footer.setOrientation(split ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
        TextView copy = findViewById(R.id.nearby_pairing_footer_copy);
        LinearLayout.LayoutParams copyParams = (LinearLayout.LayoutParams) copy.getLayoutParams();
        copyParams.width = split ? 0 : ViewGroup.LayoutParams.MATCH_PARENT;
        copyParams.weight = split ? 1f : 0f;
        copy.setLayoutParams(copyParams);
        int[] buttons = {
                R.id.nearby_invite_cancel,
                R.id.nearby_join_submit,
                R.id.nearby_join_cancel,
                R.id.nearby_scan_cancel
        };
        for (int id : buttons) {
            View button = findViewById(id);
            LinearLayout.LayoutParams params = (LinearLayout.LayoutParams) button.getLayoutParams();
            params.width = split ? ViewGroup.LayoutParams.WRAP_CONTENT
                    : ViewGroup.LayoutParams.MATCH_PARENT;
            params.weight = 0f;
            button.setLayoutParams(params);
        }
    }

    @Override protected void onDestroy() {
        ticker.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    private void showCreateBlock() {
        toolbar().setTitle(R.string.nearby_screen_invite);
        findViewById(R.id.nearby_create_block).setVisibility(View.VISIBLE);
        findViewById(R.id.nearby_invite_qr_wrap).setVisibility(View.VISIBLE);
        findViewById(R.id.nearby_invite_cancel).setVisibility(View.VISIBLE);
        findViewById(R.id.nearby_join_submit).setVisibility(View.GONE);
        findViewById(R.id.nearby_join_cancel).setVisibility(View.GONE);
        findViewById(R.id.nearby_scan_cancel).setVisibility(View.GONE);
        TextView footer = findViewById(R.id.nearby_pairing_footer_copy);
        footer.setText(R.string.nearby_invite_footer);
        // The session is process-scoped while this display state belongs to
        // one Activity. If the prior display owner disappeared, its code is no
        // longer recoverable; cancel that generation before publishing a new
        // one so reopening the page cannot be blocked by invisible stale UI.
        nearbySession.cancelActiveHost();
        if (!invite.active()) {
            invite.create(android.os.SystemClock.elapsedRealtime());
        }
        renderInvite();
        findViewById(R.id.nearby_invite_regenerate).setOnClickListener(view -> {
            invite.regenerate(android.os.SystemClock.elapsedRealtime());
            renderInvite();
        });
        findViewById(R.id.nearby_invite_cancel).setOnClickListener(view -> {
            invite.cancel(invite.generation());
            finish();
        });
        startTicker();
    }

    private void startTicker() {
        ticker.postDelayed(new Runnable() {
            @Override public void run() {
                NearbyInviteTicker.tick(invite, android.os.SystemClock.elapsedRealtime());
                if (invite.active()) {
                    renderValidity();
                    ticker.postDelayed(this, 250L);
                } else {
                    renderInvite();
                }
            }
        }, 250L);
    }

    private void renderInvite() {
        TextView code = findViewById(R.id.nearby_invite_code_value);
        code.setText(invite.code());
        renderValidity();
        MaterialButton regenerate = findViewById(R.id.nearby_invite_regenerate);
        MaterialButton cancel = findViewById(R.id.nearby_invite_cancel);
        regenerate.setEnabled(invite.active());
        cancel.setEnabled(invite.active());
    }

    private void renderValidity() {
        TextView validity = findViewById(R.id.nearby_invite_valid_for);
        long seconds = (invite.remainingMs(android.os.SystemClock.elapsedRealtime()) + 999) / 1000;
        validity.setText(getString(R.string.nearby_invite_validFor) + ": " + seconds + "s");
    }

    private void showJoinBlock() {
        toolbar().setTitle(R.string.nearby_screen_joinCode);
        findViewById(R.id.nearby_join_block).setVisibility(View.VISIBLE);
        findViewById(R.id.nearby_join_right).setVisibility(View.VISIBLE);
        findViewById(R.id.nearby_invite_cancel).setVisibility(View.GONE);
        findViewById(R.id.nearby_join_submit).setVisibility(View.VISIBLE);
        findViewById(R.id.nearby_join_cancel).setVisibility(View.GONE);
        findViewById(R.id.nearby_scan_cancel).setVisibility(View.GONE);
        TextView footer = findViewById(R.id.nearby_pairing_footer_copy);
        footer.setText(R.string.nearby_join_footer);
        TextInputEditText input = findViewById(R.id.nearby_join_code_input);
        MaterialButton submit = findViewById(R.id.nearby_join_submit);
        TextView error = findViewById(R.id.nearby_join_code_error);
        MaterialButton switchToScan = findViewById(R.id.nearby_join_switch_to_scan);
        switchToScan.setOnClickListener(view -> {
            start(this, MODE_SCAN);
            finish();
        });
        input.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                if (joinSubmit.inputLocked()) return;
                error.setVisibility(View.GONE);
                renderJoinControls();
            }
            @Override public void afterTextChanged(Editable s) {}
        });
        submit.setOnClickListener(view -> onSubmit());
        findViewById(R.id.nearby_join_cancel).setOnClickListener(view -> {
            if (requestGeneration != 0L) nearbySession.cancelCode(requestGeneration);
            requestGeneration = 0L;
            joinSubmit.cancel();
            renderJoinControls();
            error.setVisibility(View.GONE);
            finish();
        });
        renderJoinControls();
    }

    private void renderJoinControls() {
        TextInputEditText input = findViewById(R.id.nearby_join_code_input);
        MaterialButton submit = findViewById(R.id.nearby_join_submit);
        View cancel = findViewById(R.id.nearby_join_cancel);
        String raw = input.getText() == null ? "" : input.getText().toString();
        submit.setEnabled(joinSubmit.submitEnabled(raw));
        cancel.setVisibility(joinSubmit.cancelVisible() ? View.VISIBLE : View.GONE);
        input.setEnabled(!joinSubmit.inputLocked());
    }

    private void onSubmit() {
        TextInputEditText input = findViewById(R.id.nearby_join_code_input);
        TextView error = findViewById(R.id.nearby_join_code_error);
        MaterialButton submit = findViewById(R.id.nearby_join_submit);
        String raw = input.getText() == null ? "" : input.getText().toString();
        String code = NearbyInviteCode.normalize(raw);
        if (code == null) {
            // Never a request: invalid input only surfaces the field error
            // after a submit attempt (C05).
            error.setText(R.string.nearby_reason_code_invalidFormat);
            error.setVisibility(View.VISIBLE);
            return;
        }
        if (!joinSubmit.submit(raw)) {
            renderJoinControls();
            return;
        }
        long generation = nearbySession.nextJoinAttemptId();
        requestGeneration = generation;
        renderJoinControls();
        // There is no discovery bearer in this build, so the lookup request
        // cannot go out: the honest outcome is the discovery-blocked reason,
        // never a synthetic host approval. Restore after this frame so a
        // synchronous double-click is still one attempt, then the same page
        // can edit, retry, or cancel.
        nearbySession.submitCode(generation, code, android.os.SystemClock.elapsedRealtime());
        error.setText(R.string.nearby_stage_discovery_reason);
        error.setVisibility(View.VISIBLE);
        submit.post(() -> {
            joinSubmit.onFailure();
            renderJoinControls();
        });
    }

    private void showScanBlock() {
        toolbar().setTitle(R.string.nearby_screen_scan);
        findViewById(R.id.nearby_scan_block).setVisibility(View.VISIBLE);
        findViewById(R.id.nearby_scan_right).setVisibility(View.VISIBLE);
        findViewById(R.id.nearby_invite_cancel).setVisibility(View.GONE);
        findViewById(R.id.nearby_join_submit).setVisibility(View.GONE);
        findViewById(R.id.nearby_join_cancel).setVisibility(View.GONE);
        findViewById(R.id.nearby_scan_cancel).setVisibility(View.VISIBLE);
        TextView footer = findViewById(R.id.nearby_pairing_footer_copy);
        footer.setText(R.string.nearby_scan_footer);
        findViewById(R.id.nearby_scan_switch_to_code).setOnClickListener(view -> {
            start(this, MODE_JOIN_CODE);
            finish();
        });
        findViewById(R.id.nearby_scan_cancel).setOnClickListener(view -> finish());
    }

    private MaterialToolbar toolbar() {
        return findViewById(R.id.nearby_pairing_toolbar);
    }
}
