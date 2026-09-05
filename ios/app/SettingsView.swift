import SwiftUI

/// Settings roots match Android `SettingsSection` order. Layout persist goes
/// through `FlyNesAppBridge` (`fly_control_layout_*`).
struct SettingsView: View {
    @State private var section: String = "section.display"
    @State private var aspectMode: UInt32 = 1
    @State private var videoQualityPreset: UInt32 = 2
    @State private var spatialMode: UInt32 = 2
    @State private var postEffect: UInt32 = 1
    @State private var adaptiveProtection: UInt32 = 1
    @State private var directionMode: UInt32 = 3
    @State private var hapticLevel: UInt32 = 3
    @State private var distinctAbHaptics: UInt32 = 0
    @State private var audioEnabled: UInt32 = 1
    @State private var audioFocusPolicy: UInt32 = 1
    @State private var localeTag: String = "system"
    @State private var autosaveEnabled: UInt32 = 1
    @State private var status: String = ""

    private let sections = [
        "section.display",
        "section.controls",
        "section.audio",
        "section.game_language",
        "section.about",
    ]

    var body: some View {
        NavigationStack {
            List {
                ForEach(sections, id: \.self) { key in
                    Button {
                        section = key
                    } label: {
                        Text(LocalizedStringKey(key))
                            .foregroundStyle(section == key ? Color.primary : Color.secondary)
                    }
                }
                Section(LocalizedStringKey(section)) {
                    switch section {
                    case "section.display":
                        displaySection
                    case "section.controls":
                        controlsSection
                    case "section.audio":
                        audioSection
                    case "section.game_language":
                        gameLanguageSection
                    default:
                        aboutSection
                    }
                }
                if !status.isEmpty {
                    Section {
                        Text(status)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .navigationTitle("settings.title")
            .toolbar {
                Button("settings.apply") {
                    apply()
                }
            }
            .onAppear(perform: load)
        }
    }

    @ViewBuilder private var displaySection: some View {
        Picker("settings.aspect", selection: $aspectMode) {
            Text("4:3").tag(UInt32(1))
            Text("Square").tag(UInt32(2))
            Text("Integer").tag(UInt32(3))
        }
        Picker("settings.video_quality", selection: $videoQualityPreset) {
            Text("settings.video_power_saver").tag(UInt32(1))
            Text("settings.video_balanced").tag(UInt32(2))
            Text("settings.video_custom").tag(UInt32(4))
        }
        Text("settings.video_extreme_locked")
            .foregroundStyle(.secondary)
        Picker("settings.spatial", selection: $spatialMode) {
            Text("Nearest").tag(UInt32(1))
            Text("Sharp").tag(UInt32(2))
            Text("MMPX").tag(UInt32(3))
            Text("ScaleFX").tag(UInt32(4))
        }
        .disabled(videoQualityPreset != 4)
        Picker("settings.crt", selection: $postEffect) {
            Text("Off").tag(UInt32(1))
            Text("On").tag(UInt32(2))
        }
        .disabled(videoQualityPreset != 4)
        Toggle("settings.adaptive_protection", isOn: Binding(
            get: { adaptiveProtection != 0 },
            set: { adaptiveProtection = $0 ? 1 : 0 }
        ))
    }

    @ViewBuilder private var controlsSection: some View {
        Picker("settings.direction", selection: $directionMode) {
            Text("Follow joystick").tag(UInt32(1))
            Text("Fixed joystick").tag(UInt32(2))
            Text("D-pad").tag(UInt32(3))
        }
        Picker("settings.haptics", selection: $hapticLevel) {
            Text("Off").tag(UInt32(1))
            Text("Light").tag(UInt32(2))
            Text("Standard").tag(UInt32(3))
            Text("Strong").tag(UInt32(4))
        }
        Toggle("settings.distinct_ab_haptics", isOn: Binding(
            get: { distinctAbHaptics != 0 },
            set: { distinctAbHaptics = $0 ? 1 : 0 }
        ))
        NavigationLink {
            ControlLayoutEditorView()
        } label: {
            Text("control_layout.title")
        }
    }

    @ViewBuilder private var audioSection: some View {
        Toggle("settings.audio_enabled", isOn: Binding(
            get: { audioEnabled != 0 },
            set: { audioEnabled = $0 ? 1 : 0 }
        ))
        Picker("settings.audio_focus", selection: $audioFocusPolicy) {
            Text("settings.audio_focus_pause").tag(UInt32(1))
            Text("settings.audio_focus_duck").tag(UInt32(2))
            Text("settings.audio_focus_ignore").tag(UInt32(3))
        }
    }

    @ViewBuilder private var gameLanguageSection: some View {
        Picker("settings.app_language", selection: $localeTag) {
            Text("settings.language_system").tag("system")
            Text("English").tag("en")
            Text("简体中文").tag("zh-Hans")
        }
        Toggle("settings.autosave", isOn: Binding(
            get: { autosaveEnabled != 0 },
            set: { autosaveEnabled = $0 ? 1 : 0 }
        ))
    }

    @ViewBuilder private var aboutSection: some View {
        Text("settings.about_app")
        Text("settings.licenses")
            .font(.caption)
            .foregroundStyle(.secondary)
    }

    private func load() {
        let snapshot = FlyNesAppBridge.sharedInstance().settingsGet()
        aspectMode = snapshot["aspect_mode"] as? UInt32 ?? aspectMode
        videoQualityPreset = snapshot["video_quality_preset"] as? UInt32 ?? videoQualityPreset
        if videoQualityPreset == 3 {
            videoQualityPreset = 2
        }
        spatialMode = snapshot["custom_spatial_mode"] as? UInt32 ?? spatialMode
        postEffect = snapshot["custom_post_effect"] as? UInt32 ?? postEffect
        adaptiveProtection = snapshot["adaptive_protection"] as? UInt32 ?? adaptiveProtection
        directionMode = snapshot["direction_mode"] as? UInt32 ?? directionMode
        hapticLevel = snapshot["haptic_level"] as? UInt32 ?? hapticLevel
        distinctAbHaptics = snapshot["distinct_ab_haptics"] as? UInt32 ?? distinctAbHaptics
        audioEnabled = snapshot["audio_enabled"] as? UInt32 ?? audioEnabled
        audioFocusPolicy = snapshot["audio_focus_policy"] as? UInt32 ?? audioFocusPolicy
        localeTag = snapshot["locale_tag"] as? String ?? localeTag
        autosaveEnabled = snapshot["autosave_enabled"] as? UInt32 ?? autosaveEnabled
    }

    private func apply() {
        let payload: [String: Any] = [
            "aspect_mode": aspectMode,
            "video_quality_preset": videoQualityPreset == 3 ? 2 : videoQualityPreset,
            "custom_spatial_mode": spatialMode,
            "custom_post_effect": postEffect,
            "adaptive_protection": adaptiveProtection,
            "direction_mode": directionMode,
            "haptic_level": hapticLevel,
            "distinct_ab_haptics": distinctAbHaptics,
            "audio_enabled": audioEnabled,
            "audio_focus_policy": audioFocusPolicy,
            "locale_tag": localeTag,
            "autosave_enabled": autosaveEnabled,
        ]
        do {
            try FlyNesAppBridge.sharedInstance().applySettings(payload)
            status = "applied"
        } catch {
            status = error.localizedDescription
        }
    }
}
