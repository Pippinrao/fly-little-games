package com.flynes.emu;

import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.textfield.TextInputEditText;
import com.google.zxing.BarcodeFormat;
import com.google.zxing.WriterException;
import com.google.zxing.common.BitMatrix;
import com.google.zxing.qrcode.QRCodeWriter;

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
    static final String EXTRA_SCANNED_INVITE = "nearby_mvp_scanned_invite";

    private NearbyInviteHostState invite;
    private NearbySession nearbySession;
    private NearbyMvpSession mvpSession;
    private NearbyMvpOwner mvpOwner;
    private boolean handoffStarted;
    private String shownMvpQr;
    private int loggedMvpState = -1;
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
        mvpOwner = ((FlyNesApplication) getApplication()).nearbyMvpOwner();
        if (MODE_CREATE.equals(mode)) {
            showCreateBlock();
        } else if (MODE_JOIN_CODE.equals(mode)) {
            nearbySession = ((FlyNesApplication) getApplication()).nearbySession();
            invite = new NearbyInviteHostState(nearbySession);
            showJoinBlock();
        } else if (MODE_SCAN.equals(mode)) {
            nearbySession = ((FlyNesApplication) getApplication()).nearbySession();
            invite = new NearbyInviteHostState(nearbySession);
            showScanBlock();
            String scannedInvite = getIntent().getStringExtra(EXTRA_SCANNED_INVITE);
            if (scannedInvite != null) consumeScannedInvite(scannedInvite);
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
        // Body and action rail are independently measured; the body never scrolls.
        int[] blocks = {R.id.nearby_create_block, R.id.nearby_join_block, R.id.nearby_scan_block};
        for (int id : blocks) {
            findViewById(id).setLayoutParams(new LinearLayout.LayoutParams(
                    0, ViewGroup.LayoutParams.MATCH_PARENT, 0.45f));
        }
        LinearLayout.LayoutParams right = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.MATCH_PARENT, 0.55f);
        right.setMarginStart(Math.round(18 * getResources().getDisplayMetrics().density));
        findViewById(R.id.nearby_pairing_right).setLayoutParams(right);
        findViewById(R.id.nearby_invite_regenerate).setVisibility(
                MODE_CREATE.equals(mode) ? View.VISIBLE : View.GONE);
    }

    @Override protected void onResume() {
        super.onResume();
        Log.i("FlyNesNearby", "event=onResume");
    }

    @Override protected void onPause() {
        Log.i("FlyNesNearby", "event=onPause");
        super.onPause();
    }

    @Override protected void onStop() {
        Log.i("FlyNesNearby", "event=onStop");
        super.onStop();
    }

    @Override protected void onDestroy() {
        Log.i("FlyNesNearby", "event=onDestroy");
        ticker.removeCallbacksAndMessages(null);
        if (!handoffStarted && mvpOwner != null) mvpOwner.close();
        mvpSession = null;
        shownMvpQr = null;
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
        ((TextView) findViewById(R.id.nearby_invite_code_label)).setText(R.string.nearby_mvp_status_label);
        findViewById(R.id.nearby_invite_regenerate).setOnClickListener(view -> startMvpHost());
        findViewById(R.id.nearby_invite_cancel).setOnClickListener(view -> finish());
        startMvpHost();
        ticker.postDelayed(new Runnable() {
            @Override public void run() {
                if (isFinishing() || isDestroyed()) return;
                renderMvpHost();
                ticker.postDelayed(this, 250L);
            }
        }, 250L);
    }

    private void startMvpHost() {
        if (mvpOwner != null) mvpOwner.close();
        mvpSession = null;
        shownMvpQr = null;
        ((FrameLayout) findViewById(R.id.nearby_invite_qr)).removeAllViews();
        String ipv4 = NearbyMvpLanAddress.current();
        if (ipv4 == null) {
            ((TextView) findViewById(R.id.nearby_invite_code_value))
                    .setText(R.string.nearby_mvp_no_lan);
            return;
        }
        try {
            if (mvpOwner == null || !mvpOwner.startHost(ipv4)) {
                ((TextView) findViewById(R.id.nearby_invite_code_value))
                        .setText(R.string.nearby_mvp_connection_failed);
                return;
            }
            mvpSession = mvpOwner.session();
            ((TextView) findViewById(R.id.nearby_invite_code_value))
                    .setText(R.string.nearby_screen_connecting);
        } catch (IllegalStateException error) {
            ((TextView) findViewById(R.id.nearby_invite_code_value))
                    .setText(R.string.nearby_mvp_connection_failed);
        }
    }

    private void renderMvpHost() {
        if (mvpSession == null) return;
        int[] snapshot = mvpSession.snapshot();
        if (snapshot == null || snapshot.length < 2) return;
        if (snapshot[0] != loggedMvpState) {
            loggedMvpState = snapshot[0];
            Log.i("FlyNesNearby", "LAN state=" + snapshot[0] + " reason=" + snapshot[1]
                    + " transportResult=" + (snapshot.length > 2 ? snapshot[2] : 0)
                    + " transportOperation=" + (snapshot.length > 3 ? snapshot[3] : 0));
        }
        TextView status = findViewById(R.id.nearby_invite_code_value);
        if (snapshot[0] == NearbyMvpSession.LOBBY) {
            ((FrameLayout) findViewById(R.id.nearby_invite_qr)).removeAllViews();
            shownMvpQr = null;
            status.setText(R.string.nearby_mvp_connected);
            if (!handoffStarted) {
                handoffStarted = true;
                ticker.postDelayed(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    startActivity(new Intent(this, NearbyLobbyActivity.class));
                    finish();
                }, 750L);
            }
            return;
        }
        if (snapshot[0] == NearbyMvpSession.ENDED) {
            ((FrameLayout) findViewById(R.id.nearby_invite_qr)).removeAllViews();
            shownMvpQr = null;
            status.setText(snapshot[1] == 4 ? R.string.nearby_mvp_expired :
                    R.string.nearby_mvp_connection_failed);
            return;
        }
        String qr = mvpSession.invite();
        if (qr == null || qr.equals(shownMvpQr)) return;
        try {
            BitMatrix matrix = new QRCodeWriter().encode(qr, BarcodeFormat.QR_CODE, 512, 512);
            Bitmap bitmap = Bitmap.createBitmap(512, 512, Bitmap.Config.ARGB_8888);
            for (int y = 0; y < 512; y++) {
                for (int x = 0; x < 512; x++) {
                    bitmap.setPixel(x, y, matrix.get(x, y) ? 0xff000000 : 0xffffffff);
                }
            }
            ImageView image = new ImageView(this);
            image.setImageBitmap(bitmap);
            image.setContentDescription(getString(R.string.nearby_invite_qrLabel));
            image.setScaleType(ImageView.ScaleType.FIT_CENTER);
            FrameLayout frame = findViewById(R.id.nearby_invite_qr);
            frame.removeAllViews();
            frame.addView(image, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            shownMvpQr = qr;
            status.setText(R.string.nearby_mvp_waiting);
        } catch (WriterException error) {
            status.setText(R.string.nearby_mvp_connection_failed);
        }
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

    /** Shared result-consumption seam used by the camera scanner and simulator injection. */
    void consumeScannedInvite(@NonNull String inviteText) {
        if (!MODE_SCAN.equals(mode) || handoffStarted) return;
        String localIpv4 = NearbyMvpLanAddress.current();
        TextView status = findViewById(R.id.nearby_pairing_footer_copy);
        if (localIpv4 == null || mvpOwner == null ||
                !mvpOwner.startGuest(localIpv4, inviteText)) {
            status.setText(localIpv4 == null ? R.string.nearby_mvp_no_lan
                    : R.string.nearby_mvp_connection_failed);
            return;
        }
        mvpSession = mvpOwner.session();
        status.setText(R.string.nearby_screen_connecting);
        ticker.post(new Runnable() {
            @Override public void run() {
                if (isFinishing() || isDestroyed() || mvpSession == null) return;
                int[] snapshot = mvpSession.snapshot();
                if (snapshot != null && snapshot.length >= 2 &&
                        snapshot[0] == NearbyMvpSession.LOBBY) {
                    handoffStarted = true;
                    startActivity(new Intent(NearbyPairingActivity.this,
                            NearbyLobbyActivity.class));
                    finish();
                    return;
                }
                if (snapshot != null && snapshot.length >= 2 &&
                        snapshot[0] == NearbyMvpSession.ENDED) {
                    status.setText(snapshot[1] == 4 ? R.string.nearby_mvp_expired
                            : R.string.nearby_mvp_connection_failed);
                    return;
                }
                ticker.postDelayed(this, 50L);
            }
        });
    }

    private MaterialToolbar toolbar() {
        return findViewById(R.id.nearby_pairing_toolbar);
    }
}
