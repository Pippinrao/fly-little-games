package com.flynes.emu;

import android.os.Bundle;
import android.util.Log;
import android.view.ViewGroup;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * In-app license page: concatenates every text file shipped under
 * assets/licenses/*.txt (in filename order) into one scrollable TextView,
 * then appends the source-code URL. GPLv2 requires license text to travel
 * with the binary; this page is the in-app copy, and the same texts live at
 * the repo root (LICENSE) and in docs/COMPLIANCE.md.
 */
public class LicensesActivity extends AppCompatActivity {

    private static final String TAG = "FlyNES";
    private static final String LICENSES_DIR = "licenses";
    // TODO: replace with the real public repository URL before publishing to a store.
    private static final String SOURCE_URL = "https://github.com/";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        StringBuilder sb = new StringBuilder();
        try {
            String[] names = getAssets().list(LICENSES_DIR);
            if (names != null) {
                Arrays.sort(names);
                for (String name : names) {
                    if (!name.endsWith(".txt")) continue;
                    sb.append("════════════════════════════════════\n");
                    sb.append(name).append("\n");
                    sb.append("════════════════════════════════════\n\n");
                    sb.append(readAssetText(LICENSES_DIR + "/" + name)).append("\n\n");
                }
            }
        } catch (IOException e) {
            Log.e(TAG, "listing license assets failed", e);
            sb.append("(failed to read license assets: ").append(e).append(")\n\n");
        }
        sb.append("────────────────────────────────\n");
        sb.append("源代码: ").append(SOURCE_URL).append("\n");

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(0xFF000000);

        TextView text = new TextView(this);
        text.setText(sb.toString());
        text.setTextColor(0xFFE0E0E0);
        text.setTextSize(13f);
        text.setTypeface(android.graphics.Typeface.MONOSPACE);
        text.setPadding(dp(16), dp(16), dp(16), dp(16));
        scroll.addView(text, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(scroll);
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private String readAssetText(String path) throws IOException {
        try (InputStream in = getAssets().open(path);
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        }
    }
}
