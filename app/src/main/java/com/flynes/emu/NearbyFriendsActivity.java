package com.flynes.emu;

import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.button.MaterialButtonToggleGroup;

/**
 * 好友 / 附近设备 — one page with two tabs (spec §10 D13).
 *
 * <p>The 附近设备 tab renders the §2.1 pairing pipeline through {@link NearbyStagePipeline}, so the
 * whole page has exactly one source for "which stage is marked". Until the session ABI reports the
 * current stage, 权限 is the first failing stage: it is marked current with its reason, and later
 * stages stay in the neutral not-yet-reached treatment with no blocked key and no reason (D5).
 *
 * <p>Nothing here is invented. The friends list is empty because no local friend store exists, and
 * it says so with the specific blocked key. Discovery controls are disabled because no bearer
 * exists, and each states the reason it cannot act. 好友管理 is the one control on this page that
 * really navigates, because its page must exist to display the blocking stages the design requires.
 *
 * <p>The initial tab follows the friend store (附近设备 while no friend is saved, 好友 once one is)
 * and is never persisted; see {@link #savedFriendCount()}.
 */
public final class NearbyFriendsActivity extends AppCompatActivity {

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

        NearbyStagePipeline.render(this, firstFailingStage());

        // 好友管理 is a real destination, not a placeholder: rename / delete / block / identity
        // reset are governed by the friend store and the page states exactly what is missing.
        findViewById(R.id.nearby_friends_manage).setOnClickListener(view ->
                startActivity(new Intent(this, NearbyFriendsManageActivity.class)));

        // The one navigate-only entry §4 permits besides the game-center entry, because the 配对
        // page must exist to display its blocked stages. 大厅 has no equivalent entry: it follows a
        // completed pairing, and §4 permits the exception only into 配对.
        findViewById(R.id.nearby_open_pairing).setOnClickListener(view ->
                startActivity(new Intent(this, NearbyPairingActivity.class)));

        // A disabled control still has to say why it cannot act (spec §4); the visible reason is
        // the row below each button and the same text is repeated to accessibility services.
        describeDisabled(R.id.nearby_find_devices, R.id.nearby_find_devices_reason);
        describeDisabled(R.id.nearby_scan_host_qr, R.id.nearby_scan_host_qr_reason);
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
