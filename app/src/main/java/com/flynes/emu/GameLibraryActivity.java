package com.flynes.emu;

import android.content.Intent;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
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
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.core.content.ContextCompat;
import androidx.appcompat.app.AppCompatActivity;

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
public class GameLibraryActivity extends AppCompatActivity {

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
    private TextView titleCount;
    private final List<Button> sortButtons = new ArrayList<>();
    private GameAdapter adapter;

    private int sortMode = SORT_POPULARITY;
    private boolean scanning = false;
    private boolean launching = false;
    private int titleLoadGeneration;

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
        root.setBackgroundColor(color(R.color.fly_background));

        // (a) Title bar + game count.
        TextView title = new TextView(this);
        title.setText(R.string.library_title);
        title.setTextSize(22f);
        title.setTextColor(color(R.color.fly_on_surface));
        title.setTypeface(null, Typeface.BOLD);
        title.setPadding(pad, dp(18), pad, dp(2));
        root.addView(title, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        titleCount = new TextView(this);
        titleCount.setTextColor(color(R.color.fly_on_surface_muted));
        titleCount.setTextSize(12f);
        titleCount.setPadding(pad, 0, pad, dp(10));
        root.addView(titleCount, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        // (b) Search box (rounded capsule).
        searchBox = new EditText(this);
        searchBox.setHint(R.string.search_games);
        searchBox.setSingleLine(true);
        searchBox.setTextColor(color(R.color.fly_on_surface));
        searchBox.setHintTextColor(color(R.color.fly_on_surface_muted));
        searchBox.setTextSize(15f);
        searchBox.setBackground(roundedBox(color(R.color.fly_surface),
                color(R.color.fly_outline)));
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
        sortRow.addView(makeSortButton(getString(R.string.sort_popularity), SORT_POPULARITY));
        sortRow.addView(makeSortButton(getString(R.string.sort_name), SORT_NAME));
        sortRow.addView(makeSortButton(getString(R.string.sort_size), SORT_SIZE));

        searchAndSort = new LinearLayout(this);
        searchAndSort.setOrientation(LinearLayout.VERTICAL);
        searchAndSort.addView(searchBox, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        searchAndSort.addView(sortRow, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(searchAndSort, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        // (c) Game list (card rows, no dividers).
        listView = new ListView(this);
        listView.setBackgroundColor(0x00000000);
        listView.setDivider(null);
        listView.setDividerHeight(0);
        listView.setPadding(dp(10), 0, dp(10), dp(10));
        listView.setClipToPadding(false);
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
        ImageView emptyIcon = new ImageView(this);
        emptyIcon.setImageResource(R.drawable.ic_library);
        emptyIcon.setColorFilter(color(R.color.fly_on_surface_muted));
        emptyIcon.setContentDescription(getString(R.string.no_local_games));
        emptyState.addView(emptyIcon, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        TextView emptyTitle = new TextView(this);
        emptyTitle.setText(R.string.no_local_games);
        emptyTitle.setTextSize(18f);
        emptyTitle.setTextColor(color(R.color.fly_on_surface));
        emptyTitle.setGravity(Gravity.CENTER);
        emptyTitle.setPadding(0, dp(8), 0, 0);
        emptyState.addView(emptyTitle, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        chooseButton = new Button(this);
        chooseButton.setText(R.string.choose_rom_folder);
        chooseButton.setTextSize(16f);
        chooseButton.setAllCaps(false);
        chooseButton.setPadding(dp(36), dp(12), dp(36), dp(12));
        GradientDrawable btnBg = new GradientDrawable();
        btnBg.setCornerRadius(dp(24));
        btnBg.setColor(color(R.color.fly_primary));
        chooseButton.setBackground(btnBg);
        chooseButton.setTextColor(color(R.color.fly_on_primary));
        chooseButton.setOnClickListener(v -> pickTree());
        emptyState.addView(chooseButton, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        TextView emptyHint = new TextView(this);
        emptyHint.setText(R.string.add_source_hint);
        emptyHint.setTextColor(color(R.color.fly_on_surface_muted));
        emptyHint.setTextSize(13f);
        emptyHint.setGravity(Gravity.CENTER);
        emptyHint.setPadding(0, dp(12), 0, 0);
        emptyState.addView(emptyHint, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(emptyState, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        // (d) Compliance line.
        TextView compliance = new TextView(this);
        compliance.setText(R.string.legal_rom_notice);
        compliance.setTextColor(color(R.color.fly_on_surface_muted));
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
        b.setPadding(dp(20), 0, dp(20), 0);
        b.setBackground(segmentedStyle(mode == sortMode));
        b.setTextColor(color(mode == sortMode ? R.color.fly_on_primary
                : R.color.fly_on_surface));
        b.setOnClickListener(v -> {
            sortMode = mode;
            updateSortHighlight();
            applyFilterAndSort();
        });
        sortButtons.add(b);
        return b;
    }

    private void updateSortHighlight() {
        for (int i = 0; i < sortButtons.size(); i++) {
            sortButtons.get(i).setBackground(segmentedStyle(i == sortMode));
            sortButtons.get(i).setTextColor(color(i == sortMode ? R.color.fly_on_primary
                    : R.color.fly_on_surface));
        }
    }

    private GradientDrawable segmentedStyle(boolean selected) {
        GradientDrawable d = new GradientDrawable();
        d.setCornerRadius(dp(18));
        d.setColor(color(selected ? R.color.fly_primary : R.color.fly_surface_variant));
        return d;
    }

    private GradientDrawable roundedBox(int fill, int stroke) {
        GradientDrawable d = new GradientDrawable();
        d.setCornerRadius(dp(24));
        d.setColor(fill);
        d.setStroke(1, stroke);
        return d;
    }

    private GradientDrawable cardStyle() {
        GradientDrawable d = new GradientDrawable();
        d.setCornerRadius(dp(12));
        d.setColor(color(R.color.fly_surface));
        return d;
    }

    // ------------------------------------------------------------------
    // Data: stored games + the bundled From Below entry
    // ------------------------------------------------------------------

    private void reloadData() {
        int generation = ++titleLoadGeneration;
        new Thread(() -> {
            List<GameEntry> stored = RomStore.loadGames(this);
            // Show the old list immediately while fingerprints missing from legacy records
            // are backfilled. Copies keep background metadata writes off the displayed rows.
            List<GameEntry> initial = new ArrayList<>();
            for (GameEntry game : stored) initial.add(new GameEntry(game.name, game.uri,
                    game.source, game.size, game.mapper, game.prgKb, game.chrKb,
                    game.zipped, game.popularity));
            ui.post(() -> {
                if (!isFinishing() && !isDestroyed() && generation == titleLoadGeneration) {
                    showGames(initial);
                }
            });
            for (GameEntry game : stored) LegacyGameTitles.hydrate(getContentResolver(), game);
            ui.post(() -> {
                if (!isFinishing() && !isDestroyed() && generation == titleLoadGeneration) {
                    RomStore.saveGames(this, stored);
                    showGames(stored);
                }
            });
        }, "FlyNES-Titles").start();
    }

    private void showGames(List<GameEntry> stored) {
        allGames.clear();
        GameEntry builtin = GameEntry.builtinFromBelow();
        LegacyGameTitles.hydrate(getContentResolver(), builtin);
        allGames.add(builtin);
        if (stored != null) {
            allGames.addAll(stored);
        }
        boolean hasUserGames = stored != null && !stored.isEmpty();
        titleCount.setText(getString(R.string.library_game_count, allGames.size()));
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
            String searchable = g.name + "\n" + g.romName + "\n" + g.titleMetadata.english()
                    + "\n" + g.titleMetadata.chinese() + "\n" + String.join("\n", g.titleMetadata.aliases());
            if (query.isEmpty() || searchable.toLowerCase(Locale.ROOT).contains(query)) {
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
                    return displayName(a).compareTo(displayName(b));
                };
            case SORT_SIZE:
                return (a, b) -> Long.compare(b.size, a.size); // size desc
            case SORT_POPULARITY:
            default:
                return (a, b) -> {
                    int builtin = builtinFirst(a, b);
                    if (builtin != 0) return builtin;
                    int byPop = Integer.compare(b.popularity, a.popularity); // desc
                    return byPop != 0 ? byPop : displayName(a).compareTo(displayName(b));
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
            Toast.makeText(this, R.string.folder_permission_failed, Toast.LENGTH_LONG).show();
            return;
        }

        if (scanning) return;
        scanning = true;
        chooseButton.setEnabled(false);
        Toast.makeText(this, R.string.scanning_games, Toast.LENGTH_SHORT).show();

        new Thread(() -> {
            final List<GameEntry> games = RomScanner.scanTree(this, treeUri, SCAN_MAX_DEPTH);
            ui.post(() -> {
                scanning = false;
                chooseButton.setEnabled(true);
                RomStore.saveTreeUri(this, treeUri);
                RomStore.saveGames(this, games);
                Toast.makeText(this, getString(R.string.scan_complete, games.size()),
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
            final var launchTitle = LegacyGameTitles.forLoadedRom(entry, rom);
            ui.post(() -> {
                launching = false;
                if (rom == null) {
                    Toast.makeText(this, getString(R.string.cannot_load_game,
                            displayName(entry)), Toast.LENGTH_LONG).show();
                    return;
                }
                // byte[] cannot cross an Intent extra; the static field is the
                // in-process handoff to MainActivity, which clears it after use.
                NesCore.sPendingRom = rom;
                setResult(RESULT_OK, new Intent().putExtra("gameTitleEn", launchTitle.english())
                        .putExtra("gameTitleZh", launchTitle.chinese())
                        .putExtra("gameTitleFallback", entry.name));
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
                row.setPadding(dp(16), dp(12), dp(16), dp(12));
                row.setBackground(cardStyle());
                line1 = new TextView(GameLibraryActivity.this);
                line1.setTextColor(color(R.color.fly_on_surface));
                line1.setTextSize(16f);
                line2 = new TextView(GameLibraryActivity.this);
                line2.setTextColor(color(R.color.fly_on_surface_muted));
                line2.setTextSize(12f);
                row.addView(line1, new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT));
                row.addView(line2, new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT));
            }

            StringBuilder title = new StringBuilder(displayName(g));
            if (g.popularity > 0) {
                title.append(" · ").append(getString(R.string.popularity_score, g.popularity));
            }
            if (isBuiltin(g)) {
                title.append(" · ").append(getString(R.string.builtin_badge));
            }
            line1.setText(title.toString());
            line2.setText(secondLine(g));
            return row;
        }

        private String secondLine(GameEntry g) {
            if (isBuiltin(g)) return getString(R.string.builtin_rom);
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

    private String displayName(GameEntry entry) {
        Locale locale = getResources().getConfiguration().getLocales().get(0);
        return GameTitleLocalizer.localize(entry.name, entry.titleMetadata, locale);
    }

    private int color(int resource) {
        return ContextCompat.getColor(this, resource);
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}
