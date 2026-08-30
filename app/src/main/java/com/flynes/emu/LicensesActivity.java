package com.flynes.emu;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.res.ColorStateList;
import android.os.Bundle;
import android.text.method.LinkMovementMethod;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/** Landscape master-detail licenses. Asset failures remain recoverable and never expose exceptions. */
public final class LicensesActivity extends AppCompatActivity {
    public static final String EXTRA_FORCE_READ_ERROR = "licenses.force_read_error";
    private static final String LICENSES_DIR = "licenses";
    private boolean failNextRead;
    private String selectedName;
    private String selectedUrl;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.activity_licenses);
        ((MaterialToolbar) findViewById(R.id.licenses_toolbar)).setNavigationOnClickListener(view -> finish());
        View root = findViewById(R.id.licenses_root);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets;
        });
        root.requestApplyInsets();
        failNextRead = getIntent().getBooleanExtra(EXTRA_FORCE_READ_ERROR, false);
        findViewById(R.id.license_retry).setOnClickListener(view -> showEntry(selectedName));
        findViewById(R.id.license_copy_link).setOnClickListener(view -> copyLink());
        populateList(state == null ? null : state.getString("selected.license"));
    }

    @Override protected void onSaveInstanceState(Bundle outState) {
        outState.putString("selected.license", selectedName);
        super.onSaveInstanceState(outState);
    }

    private void populateList(String restore) {
        LinearLayout list = findViewById(R.id.license_list);
        try {
            String[] names = getAssets().list(LICENSES_DIR);
            if (names == null) names = new String[0];
            Arrays.sort(names);
            String first = null;
            for (String name : names) {
                if (!name.endsWith(".txt")) continue;
                if (first == null) first = name;
                MaterialButton button = new MaterialButton(this);
                button.setId(View.generateViewId());
                button.setTag(name);
                button.setText(displayName(name));
                button.setGravity(android.view.Gravity.CENTER_VERTICAL | android.view.Gravity.START);
                button.setMinHeight(dp(48));
                button.setTextColor(getColor(R.color.fly_on_surface));
                button.setBackgroundTintList(ColorStateList.valueOf(getColor(R.color.fly_surface)));
                button.setOnClickListener(view -> showEntry(name));
                list.addView(button, new LinearLayout.LayoutParams(-1, -2));
            }
            selectedName = restore == null ? first : restore;
            showEntry(selectedName);
        } catch (IOException failure) {
            selectedName = null;
            showFailure();
        }
    }

    private void showEntry(String name) {
        selectedName = name;
        selectedUrl = sourceUrl(name);
        LinearLayout list = findViewById(R.id.license_list);
        for (int i = 0; i < list.getChildCount(); i++) {
            MaterialButton button = (MaterialButton) list.getChildAt(i);
            boolean active = name != null && name.equals(button.getTag());
            button.setSelected(active);
            button.setTextColor(getColor(active ? R.color.fly_on_primary : R.color.fly_on_surface));
            button.setBackgroundTintList(ColorStateList.valueOf(
                    getColor(active ? R.color.fly_primary : R.color.fly_surface)));
        }
        TextView heading = findViewById(R.id.license_heading);
        heading.setText(name == null ? getString(R.string.license_information) : displayName(name));
        try {
            if (failNextRead) { failNextRead = false; throw new IOException("forced"); }
            String text = readAssetText(LICENSES_DIR + "/" + name);
            if (selectedUrl != null) text += "\n\n" + selectedUrl;
            ((TextView) findViewById(R.id.license_text)).setText(text);
            ((TextView) findViewById(R.id.license_text)).setMovementMethod(LinkMovementMethod.getInstance());
            findViewById(R.id.license_error).setVisibility(View.GONE);
            findViewById(R.id.license_retry).setVisibility(View.GONE);
            findViewById(R.id.license_text_scroll).setVisibility(View.VISIBLE);
            MaterialButton copy = findViewById(R.id.license_copy_link);
            copy.setEnabled(selectedUrl != null);
            copy.setContentDescription(selectedUrl == null
                    ? getString(R.string.license_source_unconfigured) : getString(R.string.license_copy_link));
        } catch (IOException | RuntimeException failure) {
            showFailure();
        }
    }

    private void showFailure() {
        findViewById(R.id.license_error).setVisibility(View.VISIBLE);
        findViewById(R.id.license_retry).setVisibility(View.VISIBLE);
        findViewById(R.id.license_text_scroll).setVisibility(View.GONE);
        findViewById(R.id.license_copy_link).setEnabled(false);
    }

    private void copyLink() {
        if (selectedUrl == null) return;
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        clipboard.setPrimaryClip(ClipData.newPlainText(getString(R.string.project_source), selectedUrl));
        Toast.makeText(this, R.string.license_link_copied, Toast.LENGTH_SHORT).show();
    }

    private static String displayName(String name) {
        if (name == null) return "";
        if (name.startsWith("from-below")) return "From Below";
        if (name.startsWith("nestopia")) return "Nestopia UE";
        if (name.startsWith("own")) return "FlyNES";
        if (name.startsWith("zlib")) return "zlib";
        return name;
    }

    private static String sourceUrl(String name) {
        if (name == null) return null;
        if (name.startsWith("nestopia")) return "https://github.com/0ldsk00l/nestopia";
        if (name.startsWith("zlib")) return "https://zlib.net/";
        if (name.startsWith("from-below")) return "https://mhughson.itch.io/from-below";
        return null;
    }

    private String readAssetText(String path) throws IOException {
        try (InputStream in = getAssets().open(path); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = in.read(buffer)) != -1) if (count > 0) out.write(buffer, 0, count);
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        }
    }

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
}
