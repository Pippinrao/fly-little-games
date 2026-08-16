package com.flynes.emu;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

/**
 * Game library UI: search, sort, scan a SAF directory, and launch ROMs.
 *
 * Pure framework widgets (EditText / ListView / ArrayAdapter — zero extra
 * dependencies, per project policy). ROM bytes are handed back to
 * MainActivity through {@link NesCore#sPendingRom}: a byte[] cannot travel
 * through an Intent extra, and both activities live in the same process.
 */
public class GameLibraryActivity extends Activity {

    private static final int REQ_PICK_TREE = 100;
    private static final int SCAN_MAX_DEPTH = 3;

    private static final int SORT_POPULARITY = 0;
    private static final int SORT_NAME = 1;
    private static final int SORT_SIZE = 2;

    private final List<GameEntry> allGames = new ArrayList<>(); // stored + builtin
    private final List<GameEntry> visible = new ArrayList<>();  // filtered + sorted
    private final Handler ui = new Handler(Looper.getMainLooper());

    private EditText searchBox;
    private LinearLayout searchAndSort;
    private ListView listView;
    private LinearLayout emptyState;
    private Button chooseButton;
    private GameAdapter adapter;

    private int sortMode = SORT_POPULARITY;
    private boolean scanning = false;
    private boolean launching = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
        reloadData();
    }

    // ------------------------------------------------------------------
    // Layout (built in code — no XML files, matching LicensesActivity)
    // ------------------------------------------------------------------

    private void buildUi() {
        int pad = dp(12);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xFF101010);

        // (a) Search box.
        searchBox = new EditText(this);
        searchBox.setHint("搜索游戏…");
        searchBox.setSingleLine(true);
        searchBox.setTextColor(0xFFFFFFFF);
        searchBox.setHintTextColor(0xFF888888);
        searchBox.setTextSize(15f);
        searchBox.setPadding(pad, dp(10), pad, dp(10));
        searchBox.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                applyFilterAndSort();
            }

            @Override
            public void afterTextChanged(Editable s) {
            }
        });

        // (b) Sort row: 热度 / 名称 / 大小.
        LinearLayout sortRow = new LinearLayout(this);
        sortRow.setOrientation(LinearLayout.HORIZONTAL);
        sortRow.setGravity(Gravity.CENTER);
        sortRow.setPadding(0, dp(2), 0, dp(6));
        sortRow.addView(makeSortButton("热度", SORT_POPULARITY));
        sortRow.addView(makeSortButton("名称", SORT_NAME));
        sortRow.addView(makeSortButton("大小", SORT_SIZE));

        searchAndSort = new LinearLayout(this);
        searchAndSort.setOrientation(LinearLayout.VERTICAL);
        searchAndSort.addView(searchBox, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        searchAndSort.addView(sortRow, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(searchAndSort, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        // (c) Game list.
        listView = new ListView(this);
        listView.setBackgroundColor(0xFF101010);
        listView.setDivider(new android.graphics.drawable.ColorDrawable(0xFF222222));
        listView.setDividerHeight(1);
        adapter = new GameAdapter();
        listView.setAdapter(adapter);
        listView.setOnItemClickListener((parent, view, position, id) ->
                launchGame(adapter.getItem(position)));
        root.addView(listView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        // Empty state: shown instead of the list until a directory is scanned.
        emptyState = new LinearLayout(this);
        emptyState.setOrientation(LinearLayout.VERTICAL);
        emptyState.setGravity(Gravity.CENTER);
        emptyState.setPadding(pad * 2, 0, pad * 2, 0);
        chooseButton = new Button(this);
        chooseButton.setText("选择 ROM 目录");
        chooseButton.setTextSize(16f);
        chooseButton.setAllCaps(false);
        chooseButton.setOnClickListener(v -> pickTree());
        emptyState.addView(chooseButton, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        TextView emptyHint = new TextView(this);
        emptyHint.setText("从设备存储选择一个包含 .nes / .zip 的文件夹");
        emptyHint.setTextColor(0xFF888888);
        emptyHint.setTextSize(13f);
        emptyHint.setGravity(Gravity.CENTER);
        emptyHint.setPadding(0, dp(12), 0, 0);
        emptyState.addView(emptyHint, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(emptyState, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        // (d) Compliance line.
        TextView compliance = new TextView(this);
        compliance.setText("仅加载你合法拥有的 ROM");
        compliance.setTextColor(0xFF777777);
        compliance.setTextSize(11f);
        compliance.setGravity(Gravity.CENTER);
        compliance.setPadding(0, dp(6), 0, dp(6));
        root.addView(compliance, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        setContentView(root);
    }

    private Button makeSortButton(String label, final int mode) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextSize(13f);
        b.setAllCaps(false);
        b.setPadding(dp(16), 0, dp(16), 0);
        b.setOnClickListener(v -> {
            sortMode = mode;
            applyFilterAndSort();
        });
        return b;
    }

    // ------------------------------------------------------------------
    // Data: stored games + the bundled From Below entry
    // ------------------------------------------------------------------

    private void reloadData() {
        List<GameEntry> stored = RomStore.loadGames(this);
        allGames.clear();
        allGames.add(GameEntry.builtinFromBelow());
        if (stored != null) {
            allGames.addAll(stored);
        }
        boolean hasUserGames = stored != null && !stored.isEmpty();
        if (hasUserGames) {
            searchAndSort.setVisibility(View.VISIBLE);
            listView.setVisibility(View.VISIBLE);
            emptyState.setVisibility(View.GONE);
            applyFilterAndSort();
        } else {
            // Nothing scanned yet: offer the directory picker instead of the list.
            searchAndSort.setVisibility(View.GONE);
            listView.setVisibility(View.GONE);
            emptyState.setVisibility(View.VISIBLE);
        }
    }

    private void applyFilterAndSort() {
        String query = searchBox.getText().toString().trim().toLowerCase(Locale.ROOT);
        visible.clear();
        for (GameEntry g : allGames) {
            if (query.isEmpty() || g.name.toLowerCase(Locale.ROOT).contains(query)) {
                visible.add(g);
            }
        }
        Collections.sort(visible, comparatorFor(sortMode));
        adapter.notifyDataSetChanged();
    }

    private Comparator<GameEntry> comparatorFor(int mode) {
        switch (mode) {
            case SORT_NAME:
                return (a, b) -> {
                    int builtin = builtinFirst(a, b);
                    if (builtin != 0) return builtin;
                    return a.name.compareTo(b.name);
                };
            case SORT_SIZE:
                return (a, b) -> Long.compare(b.size, a.size); // size desc
            case SORT_POPULARITY:
            default:
                return (a, b) -> {
                    int builtin = builtinFirst(a, b);
                    if (builtin != 0) return builtin;
                    int byPop = Integer.compare(b.popularity, a.popularity); // desc
                    return byPop != 0 ? byPop : a.name.compareTo(b.name);
                };
        }
    }

    /** The bundled game pins to the top of the 热度/名称 sorts. */
    private static int builtinFirst(GameEntry a, GameEntry b) {
        boolean ba = isBuiltin(a);
        boolean bb = isBuiltin(b);
        return ba == bb ? 0 : (ba ? -1 : 1);
    }

    private static boolean isBuiltin(GameEntry g) {
        return g != null && "assets".equals(g.source);
    }

    // ------------------------------------------------------------------
    // SAF scan flow
    // ------------------------------------------------------------------

    private void pickTree() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        startActivityForResult(intent, REQ_PICK_TREE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_PICK_TREE || resultCode != RESULT_OK || data == null) {
            return;
        }
        final Uri treeUri = data.getData();
        if (treeUri == null) return;

        try {
            getContentResolver().takePersistableUriPermission(
                    treeUri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException e) {
            Toast.makeText(this, "无法获取目录访问权限", Toast.LENGTH_LONG).show();
            return;
        }

        if (scanning) return;
        scanning = true;
        chooseButton.setEnabled(false);
        Toast.makeText(this, "扫描中…", Toast.LENGTH_SHORT).show();

        new Thread(() -> {
            final List<GameEntry> games = RomScanner.scanTree(this, treeUri, SCAN_MAX_DEPTH);
            ui.post(() -> {
                scanning = false;
                chooseButton.setEnabled(true);
                RomStore.saveTreeUri(this, treeUri);
                RomStore.saveGames(this, games);
                Toast.makeText(this, "扫描完成: " + games.size() + " 个游戏",
                        Toast.LENGTH_SHORT).show();
                reloadData();
            });
        }, "FlyNES-Scan").start();
    }

    // ------------------------------------------------------------------
    // Launch: load bytes off the main thread, hand them over statically
    // ------------------------------------------------------------------

    private void launchGame(final GameEntry entry) {
        if (launching) return;
        launching = true;
        new Thread(() -> {
            final byte[] rom = RomLoader.load(this, entry);
            ui.post(() -> {
                launching = false;
                if (rom == null) {
                    Toast.makeText(this, "无法加载: " + entry.name, Toast.LENGTH_LONG).show();
                    return;
                }
                // byte[] cannot cross an Intent extra; the static field is the
                // in-process handoff to MainActivity, which clears it after use.
                NesCore.sPendingRom = rom;
                setResult(RESULT_OK);
                finish();
            });
        }, "FlyNES-Load").start();
    }

    // ------------------------------------------------------------------
    // Adapter
    // ------------------------------------------------------------------

    private final class GameAdapter extends ArrayAdapter<GameEntry> {
        GameAdapter() {
            super(GameLibraryActivity.this, android.R.layout.simple_list_item_1, visible);
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            GameEntry g = getItem(position);

            LinearLayout row;
            TextView line1;
            TextView line2;
            if (convertView instanceof LinearLayout) {
                row = (LinearLayout) convertView;
                line1 = (TextView) row.getChildAt(0);
                line2 = (TextView) row.getChildAt(1);
            } else {
                row = new LinearLayout(GameLibraryActivity.this);
                row.setOrientation(LinearLayout.VERTICAL);
                row.setPadding(dp(14), dp(8), dp(14), dp(8));
                line1 = new TextView(GameLibraryActivity.this);
                line1.setTextColor(0xFFFFFFFF);
                line1.setTextSize(16f);
                line2 = new TextView(GameLibraryActivity.this);
                line2.setTextColor(0xFF9E9E9E);
                line2.setTextSize(12f);
                row.addView(line1, new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT));
                row.addView(line2, new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT));
            }

            StringBuilder title = new StringBuilder(g.name);
            if (g.popularity > 0) {
                title.append(" ⭐").append(g.popularity);
            }
            if (isBuiltin(g)) {
                title.append(" [内置]");
            }
            line1.setText(title.toString());
            line2.setText(secondLine(g));
            return row;
        }

        private String secondLine(GameEntry g) {
            if (isBuiltin(g)) return "内置 ROM";
            List<String> parts = new ArrayList<>();
            if (g.mapper >= 0) parts.add("mapper " + g.mapper);
            if (g.prgKb >= 0) parts.add("PRG " + g.prgKb + "KB");
            if (g.chrKb >= 0) parts.add("CHR " + g.chrKb + "KB");
            if (g.zipped) parts.add("ZIP");
            if (g.size >= 0) parts.add(formatSize(g.size));
            return parts.isEmpty() ? "" : TextUtils.join(" · ", parts);
        }
    }

    private String formatSize(long bytes) {
        if (bytes < 1024) return bytes + " B";
        if (bytes < 1024 * 1024) return (bytes / 1024) + " KB";
        return String.format(Locale.ROOT, "%.1f MB", bytes / (1024.0 * 1024.0));
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}
