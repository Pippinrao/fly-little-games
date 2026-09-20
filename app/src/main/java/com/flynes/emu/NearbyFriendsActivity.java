package com.flynes.emu;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.button.MaterialButtonToggleGroup;

/**
 * N00 nearby entry (approved HTML nearby() / design U06–U07). Left column holds the
 * kicker, headline, subtitle and 创建联机 / 输入配对码 / 扫码加入. Right column holds
 * 附近设备 / 好友 and 寻找设备. Pairing stages belong on N07/N10, not this page.
 */
public final class NearbyFriendsActivity extends AppCompatActivity
        implements PermissionGate.CameraPermissionHost {

    private boolean cameraDeniedOnce;
    private PermissionGate.Outcome pendingCameraOutcome;

    @Override public void requestCamera(PermissionGate.Outcome outcome) {
        pendingCameraOutcome = outcome;
        ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.CAMERA},
                PermissionGate.REQUEST_CAMERA);
    }

    @Override public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
                                                     @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != PermissionGate.REQUEST_CAMERA || pendingCameraOutcome == null) return;
        boolean denied = grantResults.length == 0
                || grantResults[0] != PackageManager.PERMISSION_GRANTED;
        PermissionGate.Outcome outcome = pendingCameraOutcome;
        pendingCameraOutcome = null;
        outcome.onDone(denied);
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.activity_nearby_friends);

        MaterialToolbar toolbar = findViewById(R.id.nearby_toolbar);
        toolbar.setNavigationOnClickListener(view -> finish());

        View root = findViewById(R.id.nearby_root);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(insets.getSystemWindowInsetLeft(),
                    insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(),
                    insets.getSystemWindowInsetBottom());
            return insets;
        });
        root.requestApplyInsets();

        MaterialButtonToggleGroup tabs = findViewById(R.id.nearby_tabs);
        tabs.addOnButtonCheckedListener((group, checkedId, isChecked) -> {
            if (!isChecked) return;
            showTab(checkedId == R.id.nearby_tab_friends);
        });
        // A fresh visit opens on 附近设备 while no friend is saved, so a first-time user lands on
        // the only action this page can offer instead of an empty 好友 list; it opens on 好友 as
        // soon as a saved friend exists (spec §4, `content-priority` + `empty-states`). The
        // selected tab is deliberately never persisted: it always follows the friend-store state,
        // so do not replace this with a hardcoded tab or a last-used preference. Today the count
        // is truthfully zero because no friend store is connected to the session ABI, and this
        // flips to 好友 on its own once one lands.
        boolean hasSavedFriend = savedFriendCount() > 0;
        tabs.check(hasSavedFriend ? R.id.nearby_tab_friends : R.id.nearby_tab_devices);
        showTab(hasSavedFriend);

        // N00's three primary actions (design 2026-09-13 U07 / HTML nearby()).
        // None requires a selected game (C04). 扫码加入 is the only camera
        // consumer: the permission is requested on use, a denial disables
        // nothing else (C10), and the system prompt is never repeated in one visit.
        findViewById(R.id.nearby_action_create).setOnClickListener(view ->
                NearbyPairingActivity.start(this, NearbyPairingActivity.MODE_CREATE));
        findViewById(R.id.nearby_action_enter_code).setOnClickListener(view ->
                NearbyPairingActivity.start(this, NearbyPairingActivity.MODE_JOIN_CODE));
        findViewById(R.id.nearby_action_scan_qr).setOnClickListener(view -> onScanClicked());

        findViewById(R.id.nearby_friends_manage).setOnClickListener(view ->
                startActivity(new Intent(this, NearbyFriendsManageActivity.class)));

        describeDisabled(R.id.nearby_find_devices, R.id.nearby_find_devices_reason);
        applyResponsiveColumns();
    }

    private void applyResponsiveColumns() {
        View root = findViewById(R.id.nearby_root);
        root.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight,
                                        oldBottom) -> {
            if (right - left != oldRight - oldLeft) {
                layoutNearbyColumns();
            }
        });
        root.post(this::layoutNearbyColumns);
    }

    private void layoutNearbyColumns() {
        View root = findViewById(R.id.nearby_root);
        LinearLayout columns = findViewById(R.id.nearby_device_columns);
        LinearLayout actions = findViewById(R.id.nearby_action_column);
        LinearLayout statusColumn = findViewById(R.id.nearby_status_column);
        if (root.getWidth() == 0) return;
        float density = getResources().getDisplayMetrics().density;
        float contentAfterInsets = (root.getWidth() - root.getPaddingLeft()
                - root.getPaddingRight()) / density;
        boolean split = contentAfterInsets > 580f;
        columns.setOrientation(split ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);

        LinearLayout.LayoutParams actionParams = new LinearLayout.LayoutParams(
                split ? Math.round(224f * density) : ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        actions.setLayoutParams(actionParams);

        LinearLayout.LayoutParams statusParams = new LinearLayout.LayoutParams(
                split ? 0 : ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                split ? 1f : 0f);
        if (split) statusParams.leftMargin = Math.round(18f * density);
        else statusParams.topMargin = Math.round(18f * density);
        statusColumn.setLayoutParams(statusParams);
    }

    private void onScanClicked() {
        if (PermissionGate.hasCamera(this)) {
            startActivity(NearbyPairingActivity.scanIntent(this));
            return;
        }
        if (cameraDeniedOnce) {
            // One system prompt per visit; the denial reason explains the
            // remaining legal paths instead of looping the dialog (C10).
            showScanDeniedReason();
            return;
        }
        cameraDeniedOnce = true;
        PermissionGate.requestCamera(this, denied -> {
            if (denied) showScanDeniedReason();
            else startActivity(NearbyPairingActivity.scanIntent(this));
        });
    }

    private void showScanDeniedReason() {
        TextView reason = findViewById(R.id.nearby_scan_denied_reason);
        reason.setText(R.string.nearby_reason_permission_cameraDenied);
        reason.setVisibility(View.VISIBLE);
    }

    /**
     * Friends this build can prove are saved locally. The page renders the empty state plus
     * {@code nearby_blocked_friend_store} because no local friend store exists yet, so the count
     * is truthfully zero; once the ABI carries friend identities this must read that store rather
     * than return a constant, which is what makes the initial-tab rule above follow the real list.
     */
    private int savedFriendCount() {
        return 0;
    }

    private void showTab(boolean friends) {
        findViewById(R.id.nearby_friends_panel)
                .setVisibility(friends ? View.VISIBLE : View.GONE);
        findViewById(R.id.nearby_devices_panel)
                .setVisibility(friends ? View.GONE : View.VISIBLE);
    }

    private void describeDisabled(int buttonId, int reasonId) {
        MaterialButton button = findViewById(buttonId);
        TextView reason = findViewById(reasonId);
        button.setContentDescription(button.getText() + ", " + reason.getText());
    }
}
