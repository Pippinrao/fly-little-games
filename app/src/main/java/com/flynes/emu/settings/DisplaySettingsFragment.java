package com.flynes.emu.settings;

import android.content.res.ColorStateList;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.Display;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.RadioButton;
import android.widget.Spinner;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import com.flynes.emu.R;
import com.flynes.emu.video.platform.AndroidDisplayPlatformFacade;
import com.flynes.emu.video.quality.DisplayCapabilities;
import com.flynes.emu.video.quality.DisplayModeCapability;
import com.flynes.emu.video.quality.GlCapabilities;
import com.flynes.emu.video.quality.CustomVideoSettings;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;
import com.flynes.emu.video.quality.PostEffect;
import com.flynes.emu.video.quality.SpatialMode;
import com.flynes.emu.video.quality.TemporalMode;
import com.flynes.emu.video.quality.VideoPreferences;
import com.flynes.emu.video.quality.VideoQualityPreset;
import com.flynes.emu.video.status.DisplayCapabilitiesReader;
import com.flynes.emu.video.status.DisplayStatusMonitor;
import com.flynes.emu.video.status.StatusFreshness;
import com.flynes.emu.video.status.VideoRuntimeStatus;
import com.flynes.emu.video.status.VideoStatusRepository;
import com.google.android.material.card.MaterialCardView;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.materialswitch.MaterialSwitch;

import java.util.ArrayList;
import java.util.List;

/** Dedicated landscape display surface; it does not persist sampled runtime facts. */
public final class DisplaySettingsFragment extends Fragment {
    private static final TemporalMode[] TEMPORAL_VALUES = {
            TemporalMode.NATIVE, TemporalMode.MOTION_INTERPOLATION
    };
    private static final SpatialMode[] SPATIAL_VALUES = {
            SpatialMode.NEAREST, SpatialMode.SHARP_BILINEAR,
            SpatialMode.MMPX, SpatialMode.SCALEFX
    };
    private static final PostEffect[] POST_VALUES = {PostEffect.NONE, PostEffect.CRT};

    private SettingsRepository repository;
    private View customControls;
    private final RadioButton[] presetButtons = new RadioButton[4];
    private final MaterialCardView[] presetCards = new MaterialCardView[4];
    private Spinner refreshSpinner;
    private Spinner temporalSpinner;
    private Spinner spatialSpinner;
    private Spinner postSpinner;
    private Spinner aspectSpinner;
    private MaterialSwitch adaptiveSwitch;
    private final List<PhysicalRefreshPolicy> availableRefreshPolicies = new ArrayList<>();
    private boolean binding;
    private VideoStatusRepository.Subscription statusSubscription;

    @Nullable @Override public View onCreateView(@NonNull LayoutInflater inflater,
                                                 @Nullable ViewGroup container,
                                                 @Nullable Bundle state) {
        return inflater.inflate(R.layout.fragment_display_settings, container, false);
    }

    @Override public void onViewCreated(@NonNull View view, @Nullable Bundle state) {
        repository = SettingsAccess.repository(requireContext());
        customControls = view.findViewById(R.id.video_custom_controls);
        presetButtons[0] = view.findViewById(R.id.video_preset_power);
        presetButtons[1] = view.findViewById(R.id.video_preset_balanced);
        presetButtons[2] = view.findViewById(R.id.video_preset_extreme);
        presetButtons[3] = view.findViewById(R.id.video_preset_custom);
        presetCards[0] = view.findViewById(R.id.video_preset_power_card);
        presetCards[1] = view.findViewById(R.id.video_preset_balanced_card);
        presetCards[2] = view.findViewById(R.id.video_preset_extreme_card);
        presetCards[3] = view.findViewById(R.id.video_preset_custom_card);
        for (int index = 0; index < presetCards.length; index++) {
            final int selected = index;
            if (index == 2) {
                presetCards[index].setOnClickListener(ignored -> showExtremeLocked());
                presetButtons[index].setOnClickListener(ignored -> {
                    render(repository.load());
                    showExtremeLocked();
                });
            } else {
                presetCards[index].setOnClickListener(ignored -> selectPreset(selected));
                presetButtons[index].setOnClickListener(ignored -> selectPreset(selected));
            }
        }

        refreshSpinner = view.findViewById(R.id.video_refresh_spinner);
        temporalSpinner = view.findViewById(R.id.video_temporal_spinner);
        spatialSpinner = view.findViewById(R.id.video_spatial_spinner);
        postSpinner = view.findViewById(R.id.video_post_spinner);
        aspectSpinner = view.findViewById(R.id.video_aspect_spinner);
        adaptiveSwitch = view.findViewById(R.id.video_adaptive_protection);
        bindSpinnerAdapters();
        render(repository.load());
        bindChanges();
        renderStatus(view, repository.load());
    }

    @Override public void onStart() {
        super.onStart();
        statusSubscription = VideoStatusRepository.process().observe(
                runnable -> requireActivity().runOnUiThread(runnable), this::renderRuntimeStatus);
    }

    @Override public void onStop() {
        if (statusSubscription != null) {
            statusSubscription.close();
            statusSubscription = null;
        }
        super.onStop();
    }

    private void bindSpinnerAdapters() {
        availableRefreshPolicies.clear();
        availableRefreshPolicies.add(PhysicalRefreshPolicy.FOLLOW_SYSTEM);
        List<String> refreshLabels = new ArrayList<>();
        refreshLabels.add(getString(R.string.refresh_auto));
        Display display = requireActivity().getWindowManager().getDefaultDisplay();
        DisplayCapabilities capabilities = new DisplayCapabilitiesReader().read(
                new AndroidDisplayPlatformFacade(display), GlCapabilities.unknown());
        for (DisplayModeCapability mode : capabilities.sameResolutionModes()) {
            PhysicalRefreshPolicy policy = policyFor(mode.refreshMilliHz());
            // 120 Hz remains certification-only until a signed physical-device
            // evidence profile is installed. The instrumentation hook exercises it
            // without exposing an uncertified production choice.
            if (policy == PhysicalRefreshPolicy.HZ_120) continue;
            if (policy != null && !availableRefreshPolicies.contains(policy)) {
                availableRefreshPolicies.add(policy);
                refreshLabels.add(Math.round(mode.refreshMilliHz() / 1000.0f) + " Hz");
            }
        }
        setAdapter(refreshSpinner, refreshLabels.toArray(new String[0]));
        setAdapter(temporalSpinner, new String[]{getString(R.string.video_temporal_native),
                getString(R.string.video_temporal_motion)});
        setAdapter(spatialSpinner, new String[]{getString(R.string.video_spatial_nearest),
                getString(R.string.video_spatial_sharp), getString(R.string.video_spatial_mmpx),
                getString(R.string.video_spatial_scalefx)});
        setAdapter(postSpinner, new String[]{getString(R.string.video_post_none),
                getString(R.string.video_post_crt)});
        setAdapter(aspectSpinner, getResources().getStringArray(R.array.aspect_entries));
    }

    private void setAdapter(Spinner spinner, String[] labels) {
        ArrayAdapter<String> adapter = new ArrayAdapter<>(requireContext(),
                android.R.layout.simple_spinner_item, labels);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
    }

    private void bindChanges() {
        android.widget.AdapterView.OnItemSelectedListener listener =
                new android.widget.AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(android.widget.AdapterView<?> parent,
                                                 View view, int position, long id) {
                if (!binding) saveCustomAxes();
            }
            @Override public void onNothingSelected(android.widget.AdapterView<?> parent) { }
        };
        refreshSpinner.setOnItemSelectedListener(listener);
        temporalSpinner.setOnItemSelectedListener(listener);
        spatialSpinner.setOnItemSelectedListener(listener);
        postSpinner.setOnItemSelectedListener(listener);
        aspectSpinner.setOnItemSelectedListener(listener);
        adaptiveSwitch.setOnCheckedChangeListener((button, checked) -> {
            if (binding) return;
            AppSettings current = repository.load();
            VideoPreferences old = current.videoPreferences();
            repository.save(current.toBuilder().videoPreferences(new VideoPreferences(
                    old.preset(), old.custom(), checked)).build());
            renderStatus(requireView(), repository.load());
        });
    }

    private void selectPreset(int index) {
        VideoQualityPreset preset = index == 0 ? VideoQualityPreset.POWER_SAVER
                : index == 1 ? VideoQualityPreset.BALANCED : VideoQualityPreset.CUSTOM;
        AppSettings current = repository.load();
        VideoPreferences old = current.videoPreferences();
        repository.save(current.toBuilder().videoPreferences(new VideoPreferences(
                preset, old.custom(), old.adaptiveProtection())).build());
        render(repository.load());
        renderStatus(requireView(), repository.load());
    }

    private void saveCustomAxes() {
        AppSettings current = repository.load();
        VideoPreferences old = current.videoPreferences();
        CustomVideoSettings custom = new CustomVideoSettings(
                availableRefreshPolicies.get(refreshSpinner.getSelectedItemPosition()),
                TEMPORAL_VALUES[temporalSpinner.getSelectedItemPosition()],
                SPATIAL_VALUES[spatialSpinner.getSelectedItemPosition()],
                POST_VALUES[postSpinner.getSelectedItemPosition()]);
        AspectMode aspect = AspectMode.values()[aspectSpinner.getSelectedItemPosition()];
        repository.save(current.toBuilder().aspectMode(aspect).videoPreferences(
                new VideoPreferences(VideoQualityPreset.CUSTOM, custom,
                        old.adaptiveProtection())).build());
        render(repository.load());
        renderStatus(requireView(), repository.load());
    }

    private void render(AppSettings settings) {
        binding = true;
        VideoPreferences video = settings.videoPreferences();
        int selected = video.preset() == VideoQualityPreset.POWER_SAVER ? 0
                : video.preset() == VideoQualityPreset.BALANCED ? 1
                : video.preset() == VideoQualityPreset.EXTREME ? 2 : 3;
        int primary = requireContext().getColor(R.color.fly_primary);
        int outline = requireContext().getColor(R.color.fly_outline_soft);
        for (int index = 0; index < presetButtons.length; index++) {
            presetButtons[index].setChecked(index == selected);
            presetCards[index].setStrokeColor(index == selected ? primary : outline);
            presetCards[index].setStrokeWidth(dp(index == selected ? 2 : 1));
        }
        customControls.setVisibility(selected == 3 ? View.VISIBLE : View.GONE);
        CustomVideoSettings custom = video.custom();
        int refreshPosition = availableRefreshPolicies.indexOf(custom.refreshPolicy());
        refreshSpinner.setSelection(refreshPosition >= 0 ? refreshPosition : 0);
        temporalSpinner.setSelection(indexOf(TEMPORAL_VALUES, custom.temporalMode()));
        spatialSpinner.setSelection(indexOf(SPATIAL_VALUES, custom.spatialMode()));
        postSpinner.setSelection(indexOf(POST_VALUES, custom.postEffect()));
        aspectSpinner.setSelection(settings.aspectMode().ordinal());
        adaptiveSwitch.setChecked(video.adaptiveProtection());
        adaptiveSwitch.setButtonTintList(ColorStateList.valueOf(primary));
        binding = false;
    }

    private void renderStatus(View root, AppSettings settings) {
        VideoPreferences video = settings.videoPreferences();
        String request = presetLabel(video.preset());
        ((TextView) root.findViewById(R.id.video_status_request)).setText(
                getString(R.string.video_status_request_format, request));
        ((TextView) root.findViewById(R.id.video_status_protection)).setText(
                R.string.video_status_protection_idle);
        Display.Mode mode = requireActivity().getWindowManager().getDefaultDisplay().getMode();
        ((TextView) root.findViewById(R.id.video_status_system)).setText(getString(
                R.string.video_status_system_format, mode.getPhysicalWidth(),
                mode.getPhysicalHeight(), mode.getRefreshRate()));
        ((TextView) root.findViewById(R.id.video_status_runtime)).setText(
                R.string.video_status_runtime_idle);
        ((TextView) root.findViewById(R.id.video_status_evidence)).setText(
                R.string.video_status_evidence_unverified);
        ((TextView) root.findViewById(R.id.video_status_power)).setText(getString(
                R.string.video_status_power_format, estimatedPower(video)));
        VideoRuntimeStatus runtime = VideoStatusRepository.process().current();
        if (runtime != null) renderRuntimeStatus(runtime);
    }

    private void renderRuntimeStatus(VideoRuntimeStatus status) {
        View root = getView();
        if (root == null) return;
        StatusFreshness freshness = status.freshnessAt(SystemClock.elapsedRealtime(),
                DisplayStatusMonitor.MOTION_LEASE_MS);
        if (status.systemReportedActiveMode() == null) {
            ((TextView) root.findViewById(R.id.video_status_system)).setText(
                    R.string.video_status_system_unknown);
        } else {
            DisplayModeCapability mode = status.systemReportedActiveMode();
            ((TextView) root.findViewById(R.id.video_status_system)).setText(getString(
                    R.string.video_status_system_format, mode.width(), mode.height(),
                    mode.refreshMilliHz() / 1000.0f));
        }
        ((TextView) root.findViewById(R.id.video_status_protection)).setText(getString(
                R.string.video_status_protection_runtime,
                status.runtimeTemporalState().name(), status.fallbacks().size()));
        int runtimeString = freshness == StatusFreshness.FRESH
                ? R.string.video_status_runtime_format : R.string.video_status_runtime_stale_format;
        ((TextView) root.findViewById(R.id.video_status_runtime)).setText(getString(runtimeString,
                status.sourceNominalFps(), status.copiedUniqueSourceFps(),
                status.textureUploadFps()));
    }


    private String presetLabel(VideoQualityPreset preset) {
        switch (preset) {
            case POWER_SAVER: return getString(R.string.video_preset_power);
            case EXTREME: return getString(R.string.video_preset_extreme_locked);
            case CUSTOM: return getString(R.string.video_preset_custom);
            case BALANCED:
            default: return getString(R.string.video_preset_balanced);
        }
    }

    private String estimatedPower(VideoPreferences video) {
        if (video.preset() == VideoQualityPreset.POWER_SAVER)
            return getString(R.string.video_power_low);
        if (video.preset() == VideoQualityPreset.BALANCED)
            return getString(R.string.video_power_medium_low);
        if (video.preset() == VideoQualityPreset.EXTREME)
            return getString(R.string.video_power_high);
        CustomVideoSettings custom = video.custom();
        if (custom.temporalMode() == TemporalMode.MOTION_INTERPOLATION
                || custom.spatialMode() == SpatialMode.SCALEFX)
            return getString(R.string.video_power_high);
        if (custom.spatialMode() == SpatialMode.MMPX || custom.postEffect() == PostEffect.CRT)
            return getString(R.string.video_power_medium);
        return custom.spatialMode() == SpatialMode.NEAREST
                ? getString(R.string.video_power_low)
                : getString(R.string.video_power_medium_low);
    }

    private void showExtremeLocked() {
        new MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.video_extreme_locked_title)
                .setMessage(R.string.video_extreme_locked_message)
                .setPositiveButton(R.string.ok, null)
                .show();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private static <T> int indexOf(T[] values, T target) {
        for (int index = 0; index < values.length; index++) {
            if (values[index] == target) return index;
        }
        return 0;
    }

    private static PhysicalRefreshPolicy policyFor(int refreshMilliHz) {
        if (Math.abs(refreshMilliHz - 60_000) <= 1_001) return PhysicalRefreshPolicy.HZ_60;
        if (Math.abs(refreshMilliHz - 90_000) <= 1_001) return PhysicalRefreshPolicy.HZ_90;
        if (Math.abs(refreshMilliHz - 120_000) <= 1_001) return PhysicalRefreshPolicy.HZ_120;
        return null;
    }
}
