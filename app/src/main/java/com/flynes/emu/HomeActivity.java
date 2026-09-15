package com.flynes.emu;

import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.ImageView;
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
import com.flynes.emu.cover.AndroidCoverRepository;
import com.flynes.emu.gamecenter.GameCenterItem;
import com.flynes.emu.gamecenter.GameCenterState;
import com.flynes.emu.gamecenter.BuiltinMultiplayerCapabilities;
import com.flynes.emu.gamecenter.GameTitlePresentation;
import com.flynes.emu.gamecenter.HomeHeaderLayoutPolicy;
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
import java.util.concurrent.RejectedExecutionException;

/** Unified, landscape-first Game Center and source manager. */
public final class HomeActivity extends AppCompatActivity {
    public static final String ACTION_SHOW_SOURCES = "com.flynes.emu.action.SHOW_SOURCES";
    private static final int REQUEST_TREE = 4101;
    private static final String UI_PREFS = "game_center_ui";
    private static final String PREF_MULTIPLAYER_ONLY = "multiplayerOnly";

    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService waiter = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "flynes-game-center-ui");
        thread.setDaemon(true);
        return thread;
    });
    private AndroidCatalogRuntime runtime;
    private AndroidCoverRepository covers;
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
    private MaterialButton favoriteToggle;
    private boolean busy;
    private boolean largeText;
    private GameCenterState.MultiplayerCapabilityRegistry multiplayerRegistry;
    private final ExecutorService coverLoader = Executors.newFixedThreadPool(2, runnable -> {
        Thread thread = new Thread(runnable, "flynes-cover-loader");
        thread.setDaemon(true);
        return thread;
    });

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_home);
        runtime = ((FlyNesApplication) getApplication()).catalogRuntime();
        multiplayerRegistry = BuiltinMultiplayerCapabilities.from(runtime.builtinGames());
        covers = new AndroidCoverRepository(this);
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
    }

    @Override protected void onStop() {
        persistNavigation();
        super.onStop();
    }

    @Override protected void onDestroy() {
        waiter.shutdownNow();
        coverLoader.shutdownNow();
        super.onDestroy();
    }

    private void bindViews() {
        status = findViewById(R.id.library_status);
        launch = findViewById(R.id.launch_selected);
        favoriteToggle = findViewById(R.id.favorite_toggle);
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
        findViewById(R.id.open_nearby).setOnClickListener(
                view -> startActivity(new Intent(this, NearbyFriendsActivity.class)));
        // U02: the top-right entry projects the real connection status. Facts
        // come from the session layer only - this build truthfully reports a
        // clean disconnected state (no pairing flow), never a persisted
        // boolean or a synthesized success.
        NearbyEntryStatus.Entry entryStatus =
                NearbyEntryStatus.project(new NearbyEntryStatus.NearbyFacts());
        ((com.google.android.material.button.MaterialButton) findViewById(R.id.open_nearby))
                .setText(entryResourceFor(entryStatus.status));
        // Large-text reflow (design 6, C18): the count text keeps the full
        // width and the independent filter drops below it at accessibility
        // sizes, so neither is squeezed or truncated.
        android.widget.LinearLayout statusRow = findViewById(R.id.library_status_row);
        boolean statusReflow = getResources().getConfiguration().fontScale >= 1.8f;
        statusRow.setOrientation(statusReflow
                ? android.widget.LinearLayout.VERTICAL : android.widget.LinearLayout.HORIZONTAL);
        if (statusReflow) {
            android.widget.LinearLayout.LayoutParams statusParams =
                    new android.widget.LinearLayout.LayoutParams(
                            android.widget.LinearLayout.LayoutParams.MATCH_PARENT,
                            android.widget.LinearLayout.LayoutParams.WRAP_CONTENT);
            findViewById(R.id.library_status).setLayoutParams(statusParams);
        }
        statusRow.setGravity(statusReflow
                ? android.view.Gravity.END | android.view.Gravity.CENTER_VERTICAL
                : android.view.Gravity.CENTER_VERTICAL);
        com.google.android.material.switchmaterial.SwitchMaterial multiplayerFilter =
                findViewById(R.id.multiplayer_filter);
        multiplayerFilter.setOnCheckedChangeListener(null);
        multiplayerFilter.setChecked(preferences.getBoolean(PREF_MULTIPLAYER_ONLY, false));
        navigation.setMultiplayerOnly(multiplayerFilter.isChecked());
        multiplayerFilter.setOnCheckedChangeListener((buttonView, isChecked) -> {
            navigation.setMultiplayerOnly(isChecked);
            preferences.edit().putBoolean(PREF_MULTIPLAYER_ONLY, isChecked).apply();
            renderGames();
        });
        findViewById(R.id.disable_multiplayer_filter).setOnClickListener(view -> {
            multiplayerFilter.setChecked(false);
        });
        findViewById(R.id.add_source).setOnClickListener(view -> chooseSource());
        launch.setOnClickListener(view -> launchSelected());
        favoriteToggle.setOnClickListener(view -> toggleFavorite());

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
        grid.setLayoutManager(new GridLayoutManager(
                this, 2, RecyclerView.HORIZONTAL, false));
        gameAdapter = new GameCardAdapter();
        grid.setAdapter(gameAdapter);
        grid.setHasFixedSize(true);

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
        boolean compactHeader = HomeHeaderLayoutPolicy.compact(fontScale,
                getResources().getConfiguration().getLocales().get(0));
        GridLayoutManager layout = (GridLayoutManager) ((RecyclerView) findViewById(
                R.id.game_grid)).getLayoutManager();
        layout.setSpanCount(largeText ? 1 : 2);
        if (compactHeader) {
            findViewById(R.id.game_center_heading).setVisibility(View.GONE);
        }
        if (largeText) {
            findViewById(R.id.detail_art).setVisibility(View.GONE);
            findViewById(R.id.detail_meta).setVisibility(View.GONE);
            findViewById(R.id.detail_subtitle).setVisibility(View.GONE);
            ((TextView) findViewById(R.id.detail_title)).setMaxLines(
                    HomeHeaderLayoutPolicy.detailTitleMaxLines(fontScale));
            findViewById(R.id.game_center_topbar).getLayoutParams().height = dp(80);
            findViewById(R.id.category_tabs).getLayoutParams().height = dp(64);
            int[] buttons = {R.id.category_recent, R.id.category_favorites,
                    R.id.category_all, R.id.category_builtin};
            for (int id : buttons) findViewById(id).getLayoutParams().height = dp(64);
            findViewById(R.id.launch_selected).getLayoutParams().height = dp(
                    HomeHeaderLayoutPolicy.launchButtonHeightDp(fontScale));
        }
    }

    private GameCenterState restoreNavigation(Bundle state) {
        String category = state == null ? preferencesValue("category", "ALL")
                : state.getString("category", "ALL");
        String query = state == null ? preferencesValue("query", "")
                : state.getString("query", "");
        String selected = state == null ? preferencesValue("selected", null)
                : state.getString("selected");
        return GameCenterState.restore(category, query, selected);
    }

    private String preferencesValue(String key, String fallback) {
        return getSharedPreferences(UI_PREFS, MODE_PRIVATE).getString(key, fallback);
    }

    private void persistNavigation() {
        SharedPreferences.Editor edit = preferences.edit()
                .putString("category", navigation.category().name())
                .putString("query", navigation.query())
                .putString("selected", navigation.selectedCanonicalId());
        for (int i = 0; i < GameCenterState.Category.values().length; i++) {
            edit.remove("page_" + i);
        }
        edit.apply();
    }

    private void refreshSnapshot() {
        allItems.clear();
        entries.clear();
        for (GameCatalogEntry entry : runtime.gameCatalog().canonicalEntries()) {
            entries.put(entry.canonicalGame().id(), entry);
            boolean builtin = false;
            String filename = "";
            int popularity = 0;
            for (GameVariant variant : entry.variants()) {
                builtin |= AndroidBuiltinCatalogAdapter.SOURCE.id().equals(variant.sourceId());
                if (filename.isEmpty()) filename = variant.originalFilename();
                popularity = Math.max(popularity, Popularity.scorePackage(
                        variant.originalFilename(), variant.entryPath()));
            }
            allItems.add(new GameCenterItem(entry.canonicalGame().id(),
                    entry.canonicalGame().englishTitle(), entry.canonicalGame().zhHansTitle(),
                    builtin, entry.favorite(), entry.lastPlayedSequence(), filename, popularity));
        }
        renderGames();
        sourceAdapter.submit(new ArrayList<>(runtime.stateSnapshot().sources().values()));
    }

    /** Locale-correct text for the projected entry status (UI contract key). */
    private int entryResourceFor(NearbyEntryStatus.Status status) {
        switch (status) {
            case CONNECTED: return R.string.nearby_entry_connected;
            case INTERRUPTED: return R.string.nearby_entry_interrupted;
            case UNKNOWN: return R.string.nearby_entry_unavailable;
            default: return R.string.nearby_open;
        }
    }

    private List<GameCenterItem> visibleItems() {
        if (navigation.query().isEmpty()) return applyMultiplayerFilter(navigation.filtered(allItems));
        ArrayList<GameCenterItem> searched = new ArrayList<>();
        for (GameCatalogEntry entry : runtime.gameCatalog().search(navigation.query())) {
            for (GameCenterItem item : allItems) {
                if (item.canonicalId().equals(entry.canonicalGame().id())) searched.add(item);
            }
        }
        return applyMultiplayerFilter(navigation.itemsFor(navigation.category(), searched));
    }

    /** Stable post-filter: removes non-SUPPORTED rows only, never re-sorts. */
    private List<GameCenterItem> applyMultiplayerFilter(List<GameCenterItem> base) {
        if (!navigation.multiplayerOnly()) return base;
        ArrayList<GameCenterItem> result = new ArrayList<>();
        for (GameCenterItem item : base) {
            if (multiplayerRegistry.eligibilityFor(item.canonicalId())
                    == GameCenterState.MultiplayerEligibility.SUPPORTED) result.add(item);
        }
        return result;
    }

    private void renderGames() {
        if (gameAdapter == null) return;
        List<GameCenterItem> visible = visibleItems();
        navigation.reconcile(visible);
        gameAdapter.submit(visible, navigation.selectedCanonicalId());
        findViewById(R.id.disable_multiplayer_filter).setVisibility(
                visible.isEmpty() && navigation.multiplayerOnly() ? View.VISIBLE : View.GONE);
        if (visible.isEmpty()) {
            showStatus(navigation.query().isEmpty() ? R.string.empty_category : R.string.empty_search);
            renderDetail(null);
        } else {
            status.setText(hasExternalSource()
                    ? getString(R.string.game_count_continuous, visible.size())
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
        ImageView cover = findViewById(R.id.detail_cover);
        if (entry == null) {
            title.setText(R.string.empty_category); subtitle.setText(""); meta.setText("");
            art.setText(R.string.app_name);
            launch.setEnabled(false);
            favoriteToggle.setEnabled(false);
            favoriteToggle.setIconResource(R.drawable.ic_favorite_outline);
            favoriteToggle.setContentDescription(getString(R.string.add_favorite));
            cover.setVisibility(View.GONE);
            return;
        }
        GameTitlePresentation.Title presentation = titlePresentation(entry);
        String display = presentation.primary();
        title.setText(display);
        subtitle.setText(presentation.secondary());
        GameVariant variant = preferredVariant(entry);
        meta.setText(variant == null
                ? getString(R.string.game_unavailable)
                : getResources().getQuantityString(
                        R.plurals.game_variant_count, entry.variants().size(),
                        entry.variants().size()));
        art.setText(display);
        loadCover(entry.canonicalGame().id(), cover, art);
        launch.setEnabled(!busy && variant != null);
        launch.setText(entry.isRecent() ? R.string.continue_selected_game : R.string.start_game);
        launch.setContentDescription(launch.getText() + ", " + display);
        favoriteToggle.setEnabled(!busy);
        favoriteToggle.setIconResource(entry.favorite()
                ? R.drawable.ic_favorite_filled : R.drawable.ic_favorite_outline);
        favoriteToggle.setContentDescription(getString(entry.favorite()
                ? R.string.remove_favorite : R.string.add_favorite));
    }

    private void toggleFavorite() {
        GameCatalogEntry entry = entries.get(navigation.selectedCanonicalId());
        if (entry == null || busy) return;
        boolean next = !entry.favorite();
        setBusy(true);
        await(runtime.setFavorite(entry.canonicalGame().id(), next), changed -> {
            setBusy(false);
            if (Boolean.TRUE.equals(changed)) {
                status.setText(next ? R.string.favorite_added : R.string.favorite_removed);
                refreshSnapshot();
            } else {
                showStatus(R.string.favorite_failed);
                renderDetail(entry);
            }
        }, failure -> {
            setBusy(false);
            showStatus(R.string.favorite_failed);
            renderDetail(entry);
        });
    }

    private void loadCover(String canonicalId, ImageView image, TextView fallback) {
        image.setTag(canonicalId);
        image.setImageDrawable(null);
        image.setVisibility(View.GONE);
        fallback.setVisibility(View.VISIBLE);
        if (isDestroyed()) return;
        try {
            coverLoader.execute(() -> {
                Bitmap bitmap = covers.load(canonicalId);
                main.post(() -> {
                    if (isDestroyed() || !canonicalId.equals(image.getTag())) return;
                    if (bitmap == null) return;
                    image.setImageBitmap(bitmap);
                    image.setVisibility(View.VISIBLE);
                    fallback.setVisibility(View.GONE);
                });
            });
        } catch (RejectedExecutionException shutdownRace) {
            if (!isDestroyed()) throw shutdownRace;
        }
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
        return titlePresentation(entry).primary();
    }

    private GameTitlePresentation.Title titlePresentation(GameCatalogEntry entry) {
        return GameTitlePresentation.forLocale(
                entry.canonicalGame(),
                getResources().getConfiguration().getLocales().get(0));
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
        android.util.Log.i("FlyNesSources", "picker result uri=" + uri
                + " rawFlags=0x" + Integer.toHexString(data.getFlags())
                + " maskedFlags=0x" + Integer.toHexString(flags));
        setBusy(true); showStatus(R.string.source_scanning);
        waiter.execute(() -> {
            try {
                RomSource source = runtime.addOrReauthorizeTree(uri.toString(), flags).get();
                android.util.Log.i("FlyNesSources", "source registered id=" + source.id());
                runtime.scanSource(source.id()).get();
                android.util.Log.i("FlyNesSources", "source scan completed id=" + source.id());
                main.post(() -> { setBusy(false); refreshSnapshot(); showSources(true); });
            } catch (Exception failure) {
                android.util.Log.e("FlyNesSources", "add/scan failed uri=" + uri, failure);
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
            try {
                T value = future.get();
                main.post(() -> {
                    if (!isDestroyed()) success.accept(value);
                });
            } catch (Exception error) {
                main.post(() -> {
                    if (!isDestroyed()) failure.accept(error);
                });
            }
        });
    }

    private void setBusy(boolean value) {
        busy = value;
        findViewById(R.id.add_source).setEnabled(!value);
        findViewById(R.id.open_sources).setEnabled(!value);
        GameCatalogEntry entry = entries.get(navigation == null ? null : navigation.selectedCanonicalId());
        launch.setEnabled(!value && entry != null && preferredVariant(entry) != null);
        favoriteToggle.setEnabled(!value && entry != null);
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
        void select(String selectedId) {
            String previous = selected;
            selected = selectedId;
            if (java.util.Objects.equals(previous, selectedId)) return;
            for (int index = 0; index < items.size(); ++index) {
                String id = items.get(index).canonicalId();
                if (id.equals(previous) || id.equals(selectedId)) notifyItemChanged(index, "selection");
            }
        }
        @Override public void onBindViewHolder(GameCardHolder holder, int position, List<Object> payloads) {
            if (!payloads.isEmpty() && payloads.contains("selection")) {
                holder.itemView.setSelected(items.get(position).canonicalId().equals(selected));
                holder.itemView.setContentDescription(holder.title.getText() + (holder.itemView.isSelected()
                        ? ", " + getString(R.string.game_ready) : ""));
            } else {
                onBindViewHolder(holder, position);
            }
        }
        @Override public GameCardHolder onCreateViewHolder(ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(
                    R.layout.item_game_center_card, parent, false);
            view.getLayoutParams().width = dp(largeText ? 232 : 168);
            view.getLayoutParams().height = ViewGroup.LayoutParams.MATCH_PARENT;
            return new GameCardHolder(view);
        }
        @Override public void onBindViewHolder(GameCardHolder holder, int position) {
            GameCenterItem item = items.get(position);
            GameCatalogEntry entry = entries.get(item.canonicalId());
            GameTitlePresentation.Title presentation = entry == null
                    ? new GameTitlePresentation.Title(item.titleEn(), item.titleZhHans(), false)
                    : titlePresentation(entry);
            String title = presentation.primary();
            holder.title.setText(title); holder.art.setText(title);
            holder.art.setVisibility(largeText ? View.GONE : View.VISIBLE);
            holder.cover.setVisibility(View.GONE);
            if (!largeText) loadCover(item.canonicalId(), holder.cover, holder.art);
            String metadata = presentation.secondary();
            if (metadata.isEmpty() && item.builtin()) metadata = getString(R.string.builtin_badge);
            holder.meta.setText(metadata);
            holder.meta.setVisibility(metadata.isEmpty() ? View.GONE : View.VISIBLE);
            holder.itemView.setSelected(item.canonicalId().equals(selected));
            holder.itemView.setContentDescription(title + (holder.itemView.isSelected()
                    ? ", " + getString(R.string.game_ready) : ""));
            holder.itemView.setOnClickListener(view -> {
                navigation.select(item.canonicalId());
                gameAdapter.select(item.canonicalId());
                renderDetail(entries.get(item.canonicalId()));
            });
        }
        @Override public int getItemCount() { return items.size(); }
    }

    private static final class GameCardHolder extends RecyclerView.ViewHolder {
        final TextView title, meta, art;
        final ImageView cover;
        GameCardHolder(View view) { super(view); title = view.findViewById(R.id.card_title); meta = view.findViewById(R.id.card_meta); art = view.findViewById(R.id.card_art); cover = view.findViewById(R.id.card_cover); }
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
