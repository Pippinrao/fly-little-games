package com.flynes.emu;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
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
    private boolean requestInFlight;
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

        NearbyStagePipeline.render(this, firstFailingStage());

        // A disabled control still has to say why it cannot act: the confirm
        // button repeats its reason to accessibility services.
        MaterialButton confirm = findViewById(R.id.nearby_code_confirm);
        TextView confirmReason = findViewById(R.id.nearby_code_confirm_reason);
        confirm.setContentDescription(confirm.getText() + ", " + confirmReason.getText());

        mode = getIntent() == null || getIntent().getStringExtra(EXTRA_MODE) == null
                ? "" : getIntent().getStringExtra(EXTRA_MODE);
        if (MODE_CREATE.equals(mode)) {
            showCreateBlock();
        } else if (MODE_JOIN_CODE.equals(mode)) {
            showJoinBlock();
        }
        // MODE_SCAN needs a live camera stream; until that adapter lands the
        // page shows the same blocked-stage treatment as before plus the
        // switch-to-code entry the design requires (N03).
        if (MODE_SCAN.equals(mode)) {
            findViewById(R.id.nearby_join_block).setVisibility(View.VISIBLE);
            findViewById(R.id.nearby_create_block).setVisibility(View.GONE);
        }
    }

    @Override protected void onDestroy() {
        ticker.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    private void showCreateBlock() {
        findViewById(R.id.nearby_create_block).setVisibility(View.VISIBLE);
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
            renderInvite();
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
        findViewById(R.id.nearby_join_block).setVisibility(View.VISIBLE);
        TextInputEditText input = findViewById(R.id.nearby_join_code_input);
        MaterialButton submit = findViewById(R.id.nearby_join_submit);
        TextView error = findViewById(R.id.nearby_join_code_error);
        MaterialButton switchToScan = findViewById(R.id.nearby_join_switch_to_scan);
        switchToScan.setOnClickListener(view -> {
            // The camera is requested on use by the friends page scan entry;
            // this switch only moves between entry forms (N03).
            finish();
        });
        input.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                if (requestInFlight) return;
                submit.setEnabled(isComplete(s.toString()));
                error.setVisibility(View.GONE);
            }
            @Override public void afterTextChanged(Editable s) {}
        });
        submit.setOnClickListener(view -> onSubmit());
        findViewById(R.id.nearby_join_cancel).setOnClickListener(view -> {
            requestInFlight = false;
            if (requestGeneration != 0L) nearbySession.cancelCode(requestGeneration);
            requestGeneration = 0L;
            submit.setEnabled(false);
            error.setVisibility(View.GONE);
        });
    }

    private void onSubmit() {
        TextInputEditText input = findViewById(R.id.nearby_join_code_input);
        TextView error = findViewById(R.id.nearby_join_code_error);
        MaterialButton submit = findViewById(R.id.nearby_join_submit);
        String code = NearbyInviteCode.normalize(input.getText() == null
                ? "" : input.getText().toString());
        if (code == null) {
            // Never a request: invalid input only surfaces the field error
            // after a submit attempt (C05).
            error.setVisibility(View.VISIBLE);
            return;
        }
        if (requestInFlight) return;
        long generation = nearbySession.nextJoinAttemptId();
        requestInFlight = true;
        requestGeneration = generation;
        submit.setEnabled(false);
        // There is no discovery bearer in this build, so the lookup request
        // cannot go out: the honest outcome is the discovery-blocked reason,
        // never a synthetic host approval. The backend track replaces this
        // with the real route command through the session ABI.
        nearbySession.submitCode(generation, code, android.os.SystemClock.elapsedRealtime());
        error.setText(R.string.nearby_stage_discovery_reason);
        error.setVisibility(View.VISIBLE);
        requestInFlight = false;
        requestGeneration = 0L;
    }

    private static boolean isComplete(String raw) {
        return NearbyInviteCode.normalize(raw) != null;
    }

    /**
     * Index in §2.1's order of the first failing pairing stage. This build declares no nearby
     * permission, so the 权限 stage is truthfully the first failure. Once the session ABI reports
     * the current stage this method must read it rather than return a constant — it is the single
     * source for every row's status, so nothing else needs to change.
     */
    private int firstFailingStage() {
        return NearbyStagePipeline.PERMISSION;
    }
}
