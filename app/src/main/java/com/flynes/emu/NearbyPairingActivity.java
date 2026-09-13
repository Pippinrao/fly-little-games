package com.flynes.emu;

import android.os.Bundle;
import android.view.View;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;

/**
 * 配对 — the six-digit-code path and the Wi-Fi path, both shown with the exact stage that blocks
 * them (spec §8 A1a-2).
 *
 * <p>The pipeline is rendered through {@link NearbyStagePipeline}, the same element 好友/附近设备
 * uses, so both pages mark the same stage for the same reason (D5). The one control on this page
 * that could act — 确认六位码 — is disabled and names its reason, because there is no authenticated
 * session to confirm a code against yet.
 *
 * <p>The anonymous join request / accept / reject block is deliberately not built: design §22.2 does
 * not require it and its inclusion is pending §30 review (spec §4). Do not add it here.
 */
public final class NearbyPairingActivity extends AppCompatActivity {

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

        NearbyStagePipeline.render(this, firstFailingStage());

        MaterialButton confirm = findViewById(R.id.nearby_code_confirm);
        TextView reason = findViewById(R.id.nearby_code_confirm_reason);
        confirm.setContentDescription(confirm.getText() + ", " + reason.getText());
    }

    /**
     * Index in §2.1's order of the first failing pairing stage. This build declares no nearby
     * permission, so 权限 is truthfully the first failure. Once the session ABI reports the current
     * stage this must read it instead of returning a constant.
     */
    private int firstFailingStage() {
        return NearbyStagePipeline.PERMISSION;
    }
}
