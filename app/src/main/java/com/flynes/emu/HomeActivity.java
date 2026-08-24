package com.flynes.emu;

import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;
import androidx.activity.OnBackPressedCallback;
import androidx.core.view.ViewCompat;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.flynes.emu.catalog.GameCatalogEntry;
import com.flynes.emu.catalog.GameVariant;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.android.AndroidBuiltinCatalogAdapter;
import com.flynes.emu.catalog.android.AndroidCatalogRuntime;
import com.flynes.emu.catalog.android.PersistedReadPermissionGateway;
import com.flynes.emu.catalog.persistence.SourceCatalogState;
import com.flynes.emu.catalog.persistence.SourceScanResult;
import com.flynes.emu.gamecenter.GameCenterItem;
import com.flynes.emu.gamecenter.GameCenterState;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.button.MaterialButtonToggleGroup;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/** Unified, landscape-first Game Center and source manager. */
public final class HomeActivity extends AppCompatActivity {
    public static final String ACTION_SHOW_SOURCES = "com.flynes.emu.action.SHOW_SOURCES";
    private static final int REQUEST_TREE = 4101;
    private static final String UI_PREFS = "game_center_ui";
    private static final int WIDE_PAGE_SIZE = 6;
    private static final int COMPACT_PAGE_SIZE = 4;

    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService waiter = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "flynes-game-center-ui");
        thread.setDaemon(true);
        return thread;
    });
    private AndroidCatalogRuntime runtime;
    private GameCenterState navigation;
    private SharedPreferences preferences;
    private final ArrayList<GameCenterItem> allItems = new ArrayList<>();
    private final Map<String, GameCatalogEntry> entries = new HashMap<>();
    private GameCardAdapter gameAdapter;
    private SourceAdapter sourceAdapter;
    private TextView status;
    private EditText searchInput;
    private View gameContent;
    private View sourceContent;
    private MaterialButton launch;
    private boolean busy;
    private long suppressCardClicksUntil;
    private float gestureDownX;
    private int pageSize = WIDE_PAGE_SIZE;
    private boolean largeText;

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_home);
        runtime = ((FlyNesApplication) getApplication()).catalogRuntime();
        preferences = getSharedPreferences(UI_PREFS, MODE_PRIVATE);
        navigation = restoreNavigation(savedInstanceState);
        bindViews();
        getOnBackPressedDispatcher().addCallback(this, new OnBackPressedCallback(true) {
            @Override public void handleOnBackPressed() {
                if (sourceContent.getVisibility() == View.VISIBLE) { showSources(false); return; }
                if (findViewById(R.id.search_bar).getVisibility() == View.VISIBLE) {
                    closeSearch(); return;
                }
                setEnabled(false);
                getOnBackPressedDispatcher().onBackPressed();
            }
        });
        applyInsets();
        showStatus(R.string.loading_game_center);
        setBusy(true);
        await(runtime.bootstrap(), result -> {
            setBusy(false);
            if (result.loadResult().status()
                    == com.flynes.emu.catalog.persistence.CatalogRepository.LoadStatus.RECOVERY_NEEDED) {
                showStatus(R.string.source_recovery_needed);
            }
            refreshSnapshot();
            if (ACTION_SHOW_SOURCES.equals(getIntent().getAction())) showSources(true);
        }, failure -> {
            setBusy(false);
            showStatus(R.string.source_scan_error);
            refreshSnapshot();
        });
    }

    @Override protected void onResume() {
        super.onResume();
        if (runtime != null && !busy) refreshSnapshot();
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        if (ACTION_SHOW_SOURCES.equals(intent.getAction())) showSources(true);
    }

    @Override protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        out.putString("category", navigation.category().name());
        out.putString("query", navigation.query());
        out.putString("selected", navigation.selectedCanonicalId());
        out.putIntArray("pages", navigation.pagesSnapshot());
    }

    @Override protected void onStop() {
        persistNavigation();
        super.onStop();
    }

    @Override protected void onDestroy() {
        waiter.shutdownNow();
        super.onDestroy();
    }

    private void bindViews() {
        status = findViewById(R.id.library_status);
        launch = findViewById(R.id.launch_selected);
        searchInput = findViewById(R.id.search_input);
        gameContent = findViewById(R.id.game_center_content);
        sourceContent = findViewById(R.id.source_content);

        MaterialButtonToggleGroup tabs = findViewById(R.id.category_tabs);
        tabs.check(buttonFor(navigation.category()));
        tabs.addOnButtonCheckedListener((group, checkedId, isChecked) -> {
            if (!isChecked) return;
            navigation.setCategory(categoryFor(checkedId));
            renderGames();
        });
        findViewById(R.id.open_search).setOnClickListener(view -> openSearch());
        findViewById(R.id.close_search).setOnClickListener(view -> closeSearch());
        findViewById(R.id.open_sources).setOnClickListener(view -> showSources(true));
        findViewById(R.id.close_sources).setOnClickListener(view -> showSources(false));
        findViewById(R.id.open_settings).setOnClickListener(
                view -> startActivity(new Intent(this, SettingsActivity.class)));
        findViewById(R.id.add_source).setOnClickListener(view -> chooseSource());
        findViewById(R.id.page_previous).setOnClickListener(view -> movePage(-1));
        findViewById(R.id.page_next).setOnClickListener(view -> movePage(1));
        launch.setOnClickListener(view -> launchSelected());

        searchInput.setText(navigation.query());
        if (!navigation.query().isEmpty()) findViewById(R.id.search_bar).setVisibility(View.VISIBLE);
        searchInput.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                navigation.setQuery(s.toString()); renderGames();
            }
            @Override public void afterTextChanged(Editable s) {}
        });

        RecyclerView grid = findViewById(R.id.game_grid);
        grid.setLayoutManager(new GridLayoutManager(this, 3));
        gameAdapter = new GameCardAdapter();
        grid.setAdapter(gameAdapter);
        grid.setOnTouchListener(this::handlePageGesture);

        RecyclerView sources = findViewById(R.id.source_list);
        sources.setLayoutManager(new LinearLayoutManager(this));
        sourceAdapter = new SourceAdapter();
        sources.setAdapter(sourceAdapter);
        ViewCompat.setAccessibilityHeading(findViewById(R.id.game_center_heading), true);
        ViewCompat.setAccessibilityHeading(findViewById(R.id.detail_title), true);
        ViewCompat.setAccessibilityHeading(findViewById(R.id.sources_heading), true);
    }

    private void applyInsets() {
        View root = findViewById(R.id.home_root);
        int base = dp(8);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(base + insets.getSystemWindowInsetLeft(),
                    base + insets.getSystemWindowInsetTop(),
                    base + insets.getSystemWindowInsetRight(),
                    base + insets.getSystemWindowInsetBottom());
            return insets;
        });
        root.requestApplyInsets();
        float fontScale = getResources().getConfiguration().fontScale;
        int widthDp = getResources().getConfiguration().screenWidthDp;
        largeText = fontScale >= 1.8f;
        pageSize = largeText || widthDp < 720 ? COMPACT_PAGE_SIZE : WIDE_PAGE_SIZE;
        GridLayoutManager layout = (GridLayoutManager) ((RecyclerView) findViewById(
                R.id.game_grid)).getLayoutManager();
        layout.setSpanCount(pageSize == WIDE_PAGE_SIZE ? 3 : 2);
        if (largeText) {
            findViewById(R.id.game_center_heading).setVisibility(View.GONE);
            findViewById(R.id.detail_art_label).setVisibility(View.GONE);
            findViewById(R.id.game_center_topbar).getLayoutParams().height = dp(80);
            findViewById(R.id.category_tabs).getLayoutParams().height = dp(64);
            int[] buttons = {R.id.category_recent, R.id.category_favorites,
                    R.id.category_all, R.id.category_builtin};
            for (int id : buttons) findViewById(id).getLayoutParams().height = dp(64);
            findViewById(R.id.launch_selected).getLayoutParams().height = dp(64);
        }
    }

    private GameCenterState restoreNavigation(Bundle state) {
        String category = state == null ? preferencesValue("category", "ALL")
                : state.getString("category", "ALL");
        String query = state == null ? preferencesValue("query", "")
                : state.getString("query", "");
        String selected = state == null ? preferencesValue("selected", null)
                : state.getString("selected");
        int[] pages = state == null ? readPages() : state.getIntArray("pages");
        return GameCenterState.restore(category, query, selected, pages);
    }

    private String preferencesValue(String key, String fallback) {
        return getSharedPreferences(UI_PREFS, MODE_PRIVATE).getString(key, fallback);
    }

    private int[] readPages() {
        int[] values = new int[GameCenterState.Category.values().length];
        SharedPreferences saved = getSharedPreferences(UI_PREFS, MODE_PRIVATE);
        for (int i = 0; i < values.length; i++) values[i] = saved.getInt("page_" + i, 0);
        return values;
    }

    private void persistNavigation() {
        SharedPreferences.Editor edit = preferences.edit()
                .putString("category", navigation.category().name())
                .putString("query", navigation.query())
                .putString("selected", navigation.selectedCanonicalId());
        int[] pages = navigation.pagesSnapshot();
        for (int i = 0; i < pages.length; i++) edit.putInt("page_" + i, pages[i]);
        edit.apply();
    }

    private void refreshSnapshot() {
        allItems.clear();
        entries.clear();
        for (GameCatalogEntry entry : runtime.gameCatalog().canonicalEntries()) {
            entries.put(entry.canonicalGame().id(), entry);
            boolean builtin = false;
            String filename = "";
            for (GameVariant variant : entry.variants()) {
                builtin |= AndroidBuiltinCatalogAdapter.SOURCE.id().equals(variant.sourceId());
                if (filename.isEmpty()) filename = variant.originalFilename();
            }
            allItems.add(new GameCenterItem(entry.canonicalGame().id(),
                    entry.canonicalGame().englishTitle(), entry.canonicalGame().zhHansTitle(),
                    builtin, entry.favorite(), entry.lastPlayedSequence(), filename));
        }
        renderGames();
        sourceAdapter.submit(new ArrayList<>(runtime.stateSnapshot().sources().values()));
    }

    private List<GameCenterItem> visibleItems() {
        if (navigation.query().isEmpty()) return navigation.filtered(allItems);
        ArrayList<GameCenterItem> searched = new ArrayList<>();
        for (GameCatalogEntry entry : runtime.gameCatalog().search(navigation.query())) {
            for (GameCenterItem item : allItems) {
                if (item.canonicalId().equals(entry.canonicalGame().id())) searched.add(item);
            }
        }
        return navigation.itemsFor(navigation.category(), searched);
    }

    private void renderGames() {
        if (gameAdapter == null) return;
        List<GameCenterItem> visible = visibleItems();
        navigation.reconcile(visible, pageSize);
        List<GameCenterItem> page = navigation.pageItems(visible, pageSize);
        gameAdapter.submit(page, navigation.selectedCanonicalId());
        int pages = Math.max(1, (visible.size() + pageSize - 1) / pageSize);
        ((TextView) findViewById(R.id.page_indicator)).setText(
                getString(R.string.page_count, navigation.page() + 1, pages));
        findViewById(R.id.page_previous).setEnabled(navigation.canMovePrevious());
        findViewById(R.id.page_next).setEnabled(navigation.canMoveNext(visible.size(), pageSize));
        if (visible.isEmpty()) {
            showStatus(navigation.query().isEmpty() ? R.string.empty_category : R.string.empty_search);
            renderDetail(null);
        } else {
            status.setText(hasExternalSource()
                    ? getString(R.string.game_count_status,
                    visible.size(), navigation.page() + 1, pages)
                    : getString(largeText ? R.string.no_external_sources_large_text
                    : R.string.no_external_sources));
            renderDetail(entries.get(navigation.selectedCanonicalId()));
        }
    }

    private void renderDetail(GameCatalogEntry entry) {
        TextView title = findViewById(R.id.detail_title);
        TextView subtitle = findViewById(R.id.detail_subtitle);
        TextView meta = findViewById(R.id.detail_meta);
        TextView art = findViewById(R.id.detail_art_label);
        if (entry == null) {
            title.setText(R.string.empty_category); subtitle.setText(""); meta.setText("");
            art.setText(R.string.app_name);
            launch.setEnabled(false); return;
        }
        String display = displayTitle(entry);
        title.setText(display);
        String secondary = secondaryTitle(entry, display);
        subtitle.setText(secondary);
        GameVariant variant = preferredVariant(entry);
        meta.setText(variant == null ? getString(R.string.game_unavailable)
                : variant.originalFilename());
        art.setText(display);
        launch.setEnabled(!busy && variant != null);
        launch.setText(entry.isRecent() ? R.string.continue_selected_game : R.string.start_game);
        launch.setContentDescription(launch.getText() + ", " + display);
    }

    private void launchSelected() {
        GameCatalogEntry entry = entries.get(navigation.selectedCanonicalId());
        GameVariant variant = entry == null ? null : preferredVariant(entry);
        if (variant == null) return;
        setBusy(true);
        status.setText(getString(R.string.launching_game, displayTitle(entry)));
        ((FlyNesApplication) getApplication()).gameLaunchService().launch(
                variant.variantId(), result -> {
                    setBusy(false);
                    if (result.sessionCommitted()) {
                        startActivity(new Intent(this, MainActivity.class));
                    } else {
                        showStatus(R.string.launch_failed);
                        refreshSnapshot();
                    }
                });
    }

    private static GameVariant preferredVariant(GameCatalogEntry entry) {
        for (GameVariant variant : entry.variants()) if (variant.isLaunchable()) return variant;
        return null;
    }

    private String displayTitle(GameCatalogEntry entry) {
        boolean chinese = getResources().getConfiguration().getLocales().get(0)
                .getLanguage().equals("zh");
        String preferred = chinese ? entry.canonicalGame().zhHansTitle()
                : entry.canonicalGame().englishTitle();
        String fallback = chinese ? entry.canonicalGame().englishTitle()
                : entry.canonicalGame().zhHansTitle();
        if (!preferred.isEmpty()) return preferred;
        if (!fallback.isEmpty()) return fallback;
        return entry.variants().isEmpty() ? getString(R.string.game_unavailable)
                : entry.variants().get(0).originalFilename();
    }

    private String secondaryTitle(GameCatalogEntry entry, String primary) {
        String english = entry.canonicalGame().englishTitle();
        String chinese = entry.canonicalGame().zhHansTitle();
        if (!english.isEmpty() && !english.equals(primary)) return english;
        if (!chinese.isEmpty() && !chinese.equals(primary)) return chinese;
        return "";
    }

    private void movePage(int delta) {
        List<GameCenterItem> visible = visibleItems();
        navigation.movePage(delta, visible.size(), pageSize);
        renderGames();
    }

    private boolean handlePageGesture(View touched, MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) gestureDownX = event.getX();
        if (event.getActionMasked() == MotionEvent.ACTION_UP) {
            float distance = event.getX() - gestureDownX;
            if (Math.abs(distance) >= getResources().getDisplayMetrics().widthPixels * .15f) {
                suppressCardClicksUntil = android.os.SystemClock.uptimeMillis() + 300;
                movePage(distance < 0 ? 1 : -1);
                return true;
            }
            touched.performClick();
        }
        return false;
    }

    private boolean hasExternalSource() {
        for (SourceCatalogState source : runtime.stateSnapshot().sources().values()) {
            if (source.source().type() == RomSource.Type.SAF_TREE) return true;
        }
        return false;
    }

    private void openSearch() {
        findViewById(R.id.search_bar).setVisibility(View.VISIBLE);
        searchInput.requestFocus();
    }

    private void closeSearch() {
        searchInput.setText("");
        findViewById(R.id.search_bar).setVisibility(View.GONE);
    }

    private void showSources(boolean show) {
        gameContent.setVisibility(show ? View.GONE : View.VISIBLE);
        sourceContent.setVisibility(show ? View.VISIBLE : View.GONE);
        findViewById(R.id.category_tabs).setVisibility(show ? View.INVISIBLE : View.VISIBLE);
        sourceAdapter.submit(new ArrayList<>(runtime.stateSnapshot().sources().values()));
    }

    private void chooseSource() {
        startActivityForResult(PersistedReadPermissionGateway.pickerIntent(), REQUEST_TREE);
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_TREE || resultCode != RESULT_OK || data == null
                || data.getData() == null) return;
        Uri uri = data.getData();
        int flags = data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        setBusy(true); showStatus(R.string.source_scanning);
        waiter.execute(() -> {
            try {
                RomSource source = runtime.addOrReauthorizeTree(uri.toString(), flags).get();
                runtime.scanSource(source.id()).get();
                main.post(() -> { setBusy(false); refreshSnapshot(); showSources(true); });
            } catch (Exception failure) {
                main.post(() -> { setBusy(false); showStatus(R.string.source_operation_failed); refreshSnapshot(); });
            }
        });
    }

    private void scanSource(SourceCatalogState item) {
        setBusy(true); sourceAdapter.setBusySource(item.source().id());
        await(runtime.scanSource(item.source().id()), result -> {
            setBusy(false); sourceAdapter.setBusySource(null); refreshSnapshot(); showSources(true);
        }, failure -> { setBusy(false); sourceAdapter.setBusySource(null); showStatus(R.string.source_scan_error); refreshSnapshot(); });
    }

    private void removeSource(SourceCatalogState item) {
        new AlertDialog.Builder(this).setTitle(R.string.remove_source_title)
                .setMessage(R.string.remove_source_message).setNegativeButton(R.string.cancel, null)
                .setPositiveButton(R.string.remove, (dialog, which) -> {
                    setBusy(true);
                    await(runtime.removeSource(item.source().id()), ignored -> {
                        setBusy(false); refreshSnapshot(); showSources(true);
                    }, failure -> { setBusy(false); showStatus(R.string.source_operation_failed); });
                }).show();
    }

    private <T> void await(Future<T> future, Success<T> success, Failure failure) {
        waiter.execute(() -> {
            try { T value = future.get(); main.post(() -> success.accept(value)); }
            catch (Exception error) { main.post(() -> failure.accept(error)); }
        });
    }

    private void setBusy(boolean value) {
        busy = value;
        findViewById(R.id.add_source).setEnabled(!value);
        findViewById(R.id.open_sources).setEnabled(!value);
        GameCatalogEntry entry = entries.get(navigation == null ? null : navigation.selectedCanonicalId());
        launch.setEnabled(!value && entry != null && preferredVariant(entry) != null);
    }

    private void showStatus(int stringId) { status.setText(stringId); }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }

    private int buttonFor(GameCenterState.Category category) {
        return switch (category) {
            case RECENT -> R.id.category_recent;
            case FAVORITES -> R.id.category_favorites;
            case ALL -> R.id.category_all;
            case BUILTIN -> R.id.category_builtin;
        };
    }
    private GameCenterState.Category categoryFor(int id) {
        if (id == R.id.category_recent) return GameCenterState.Category.RECENT;
        if (id == R.id.category_favorites) return GameCenterState.Category.FAVORITES;
        if (id == R.id.category_builtin) return GameCenterState.Category.BUILTIN;
        return GameCenterState.Category.ALL;
    }

    private final class GameCardAdapter extends RecyclerView.Adapter<GameCardHolder> {
        private List<GameCenterItem> items = Collections.emptyList();
        private String selected;
        void submit(List<GameCenterItem> values, String selectedId) {
            items = new ArrayList<>(values); selected = selectedId; notifyDataSetChanged();
        }
        @Override public GameCardHolder onCreateViewHolder(ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(
                    R.layout.item_game_center_card, parent, false);
            int columns = pageSize == WIDE_PAGE_SIZE ? 3 : 2;
            view.getLayoutParams().width = Math.max(dp(120), parent.getWidth() / columns - dp(8));
            view.getLayoutParams().height = Math.max(dp(112), parent.getHeight() / 2 - dp(8));
            return new GameCardHolder(view);
        }
        @Override public void onBindViewHolder(GameCardHolder holder, int position) {
            GameCenterItem item = items.get(position);
            GameCatalogEntry entry = entries.get(item.canonicalId());
            String title = entry == null ? item.titleEn() : displayTitle(entry);
            holder.title.setText(title); holder.art.setText(title);
            holder.art.setVisibility(largeText ? View.GONE : View.VISIBLE);
            holder.meta.setText(item.builtin() ? getString(R.string.builtin_badge)
                    : item.originalFilename());
            holder.itemView.setSelected(item.canonicalId().equals(selected));
            holder.itemView.setContentDescription(title + (holder.itemView.isSelected()
                    ? ", " + getString(R.string.game_ready) : ""));
            holder.itemView.setOnClickListener(view -> {
                if (android.os.SystemClock.uptimeMillis() < suppressCardClicksUntil) return;
                navigation.select(item.canonicalId()); renderGames();
            });
        }
        @Override public int getItemCount() { return items.size(); }
    }

    private static final class GameCardHolder extends RecyclerView.ViewHolder {
        final TextView title, meta, art;
        GameCardHolder(View view) { super(view); title = view.findViewById(R.id.card_title); meta = view.findViewById(R.id.card_meta); art = view.findViewById(R.id.card_art); }
    }

    private final class SourceAdapter extends RecyclerView.Adapter<SourceHolder> {
        private List<SourceCatalogState> items = Collections.emptyList();
        private String busySource;
        void submit(List<SourceCatalogState> values) { items = values; notifyDataSetChanged(); }
        void setBusySource(String id) { busySource = id; notifyDataSetChanged(); }
        @Override public SourceHolder onCreateViewHolder(ViewGroup parent, int viewType) {
            return new SourceHolder(LayoutInflater.from(parent.getContext()).inflate(R.layout.item_source, parent, false));
        }
        @Override public void onBindViewHolder(SourceHolder holder, int position) {
            SourceCatalogState item = items.get(position); RomSource source = item.source();
            boolean builtin = source.type() == RomSource.Type.BUILTIN;
            holder.name.setText(builtin ? R.string.source_builtin : R.string.source_device_folder);
            boolean rowBusy = source.id().equals(busySource);
            if (rowBusy) holder.state.setText(R.string.source_scanning);
            else if (source.permissionState() == RomSource.PermissionState.NEEDS_REAUTHORIZE) holder.state.setText(R.string.source_permission_lost);
            else if (source.availability() != RomSource.Availability.AVAILABLE) holder.state.setText(R.string.source_unavailable);
            else if (item.lastScanCompleteness() == SourceScanResult.Completeness.FATAL) holder.state.setText(R.string.source_scan_error);
            else if (!builtin && item.lastScanToken() > 0 && item.packages().isEmpty()) holder.state.setText(R.string.source_scan_empty);
            else holder.state.setText(getString(R.string.source_ready, item.packages().size()));
            holder.rescan.setVisibility(builtin ? View.GONE : View.VISIBLE);
            holder.remove.setVisibility(builtin ? View.GONE : View.VISIBLE);
            holder.rescan.setText(source.permissionState() == RomSource.PermissionState.NEEDS_REAUTHORIZE ? R.string.reauthorize : R.string.rescan);
            holder.rescan.setEnabled(!busy && !rowBusy); holder.remove.setEnabled(!busy && !rowBusy);
            holder.rescan.setOnClickListener(view -> { if (source.permissionState() == RomSource.PermissionState.NEEDS_REAUTHORIZE) chooseSource(); else scanSource(item); });
            holder.remove.setOnClickListener(view -> removeSource(item));
        }
        @Override public int getItemCount() { return items.size(); }
    }

    private static final class SourceHolder extends RecyclerView.ViewHolder {
        final TextView name, state; final MaterialButton rescan, remove;
        SourceHolder(View view) { super(view); name = view.findViewById(R.id.source_name); state = view.findViewById(R.id.source_status); rescan = view.findViewById(R.id.source_rescan); remove = view.findViewById(R.id.source_remove); }
    }

    @FunctionalInterface private interface Success<T> { void accept(T value); }
    @FunctionalInterface private interface Failure { void accept(Throwable failure); }
}
