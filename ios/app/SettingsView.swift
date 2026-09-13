import SwiftUI

/// Each change is committed through the validated shared settings repository.
struct SettingsView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var section = "section.display"
    @State private var snapshot: [String: Any] = [:]
    @State private var failure: String?
    private let sections = ["section.display", "section.controls", "section.audio", "section.game_language", "section.about"]

    var body: some View {
        NavigationStack {
            HStack(spacing: 0) {
                List(sections, id: \.self) { key in
                    Button { section = key } label: {
                        HStack {
                            Text(LocalizedStringKey(key))
                            Spacer()
                            if section == key { Image(systemName: "checkmark").font(.caption) }
                        }
                        .foregroundStyle(section == key ? Color.accentColor : Color.primary)
                    }.accessibilityIdentifier(key)
                }.frame(width: 240)
                Form {
                    if snapshot.isEmpty {
                        Text("settings.load_failed")
                        Button("library.source.retry", action: load)
                    } else {
                        Section(LocalizedStringKey(section)) {
                            switch section {
                            case "section.display": displaySection
                            case "section.controls": controlsSection
                            case "section.audio": audioSection
                            case "section.game_language": gameLanguageSection
                            default: aboutSection
                            }
                        }
                    }
                }
            }
            .navigationTitle("settings.title")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("common.done") { dismiss() }.accessibilityIdentifier("settings_done")
                }
            }
            .onAppear(perform: load)
            .alert("settings.save_failed", isPresented: Binding(get: { failure != nil }, set: { if !$0 { failure = nil } })) {
                Button("common.ok", role: .cancel) { failure = nil }
            } message: { Text(failure ?? "") }
        }
    }

    private var preset: UInt32 { number("video_quality_preset") }
    private var spatialSelection: Binding<UInt32> {
        Binding(get: { preset == 1 ? 1 : preset == 2 || preset == 3 ? 2 : number("custom_spatial_mode") },
                set: { persist(["custom_spatial_mode": $0]) })
    }
    private var postSelection: Binding<UInt32> {
        Binding(get: { preset == 4 ? number("custom_post_effect") : 1 },
                set: { persist(["custom_post_effect": $0]) })
    }
    @ViewBuilder private var displaySection: some View {
        Picker("settings.aspect", selection: integer("aspect_mode")) {
            Text("4:3").tag(UInt32(1))
            Text("settings.aspect_square").tag(UInt32(2))
            Text("settings.aspect_integer").tag(UInt32(3))
        }
        LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 8) {
            presetCard(1, "settings.video_power_saver", "settings.video_power_summary")
            presetCard(2, "settings.video_balanced", "settings.video_balanced_summary")
            presetCard(3, "settings.video_extreme_unavailable", "settings.video_extreme_summary")
            presetCard(4, "settings.video_custom", "settings.video_custom_summary")
        }.accessibilityIdentifier("settings_video_quality")
        if preset == 4 {
            LabeledContent("settings.refresh", value: "60 Hz")
            LabeledContent("settings.temporal") { Text("settings.temporal_native") }
        }
        Picker("settings.spatial", selection: spatialSelection) {
            Text("settings.spatial_nearest").tag(UInt32(1))
            Text("settings.spatial_sharp").tag(UInt32(2))
            Text("MMPX").tag(UInt32(3))
            Text("ScaleFX").tag(UInt32(4))
        }.disabled(preset != 4)
        Picker("settings.crt", selection: postSelection) {
            Text("common.off").tag(UInt32(1))
            Text("common.on").tag(UInt32(2))
        }.disabled(preset != 4)
        Text("settings.video_extreme_locked").font(.caption).foregroundStyle(.secondary)
        Toggle("settings.adaptive_protection", isOn: boolean("adaptive_protection"))
    }
    private func presetCard(_ value: UInt32, _ title: String, _ subtitle: String) -> some View {
        Button { persist(["video_quality_preset": value]) } label: {
            VStack(alignment: .leading, spacing: 6) {
                HStack(alignment: .top) {
                    Image(systemName: value == 3 ? "lock.fill" : preset == value ? "checkmark.circle.fill" : "circle")
                    Text(LocalizedStringKey(title)).font(.subheadline.weight(.semibold))
                }
                Text(LocalizedStringKey(subtitle)).font(.caption).foregroundStyle(.secondary)
            }
            .frame(maxWidth: .infinity, minHeight: 76, alignment: .topLeading)
            .padding(10)
            .background(preset == value ? Color.accentColor.opacity(0.12) : Color.secondary.opacity(0.08))
            .clipShape(RoundedRectangle(cornerRadius: 12))
            .overlay(RoundedRectangle(cornerRadius: 12).stroke(preset == value ? Color.accentColor : Color.clear))
        }
        .buttonStyle(.plain)
        .disabled(value == 3)
        .accessibilityIdentifier("video_preset_\(value)")
    }
    @ViewBuilder private var controlsSection: some View {
        Picker("settings.direction", selection: integer("direction_mode")) {
            Text("settings.direction_follow").tag(UInt32(1))
            Text("settings.direction_fixed").tag(UInt32(2))
            Text("settings.direction_dpad").tag(UInt32(3))
        }
        NavigationLink { ControlLayoutEditorView() } label: {
            VStack(alignment: .leading) {
                Text("control_layout.title")
                Text("control_layout.summary").font(.caption).foregroundStyle(.secondary)
            }
        }.accessibilityIdentifier("settings_layout")
        if number("direction_mode") != 3 {
            VStack(alignment: .leading) {
                Text("settings.dead_zone")
                Slider(value: decimal("dead_zone"), in: 0.08...0.45)
            }
        }
        Picker("settings.haptics", selection: integer("haptic_level")) {
            Text("common.off").tag(UInt32(1))
            Text("settings.haptics_light").tag(UInt32(2))
            Text("settings.haptics_standard").tag(UInt32(3))
            Text("settings.haptics_strong").tag(UInt32(4))
        }
        Toggle("settings.distinct_ab_haptics", isOn: boolean("distinct_ab_haptics"))
            .disabled(number("haptic_level") == 1)
        Button("settings.haptic_preview", action: previewHaptics)
            .disabled(number("haptic_level") == 1)
        Button("control_layout.recommended") {
            do {
                let bridge = FlyNesAppBridge.sharedInstance()
                // Android controls.reset restores the whole controls section;
                // the editor's Reset button still changes only its layout draft.
                try bridge.applySettings(["layout_preset": UInt32(1), "direction_mode": UInt32(2),
                    "button_scale": 1.0, "vertical_offset": 0.0, "control_opacity": 0.78,
                    "joystick_scale": 1.0, "dead_zone": 0.18, "haptic_level": UInt32(2),
                    "distinct_ab_haptics": UInt32(1)])
                try bridge.controlLayoutApply(ControlLayoutDraft.recommended)
                load()
            }
            catch { failure = error.localizedDescription }
        }
    }
    @ViewBuilder private var audioSection: some View {
        Toggle("settings.audio_enabled", isOn: boolean("audio_enabled"))
            .accessibilityIdentifier("settings_audio")
        Picker("settings.audio_focus", selection: integer("audio_focus_policy")) {
            Text("settings.audio_focus_pause").tag(UInt32(1))
            Text("settings.audio_focus_duck").tag(UInt32(2))
            Text("settings.audio_focus_ignore").tag(UInt32(3))
        }
    }
    @ViewBuilder private var gameLanguageSection: some View {
        Picker("settings.app_language", selection: Binding<String>(
            get: {
                let tag = snapshot["locale_tag"] as? String ?? "system"
                return tag.hasPrefix("zh") ? "zh-Hans" : tag.hasPrefix("en") ? "en" : "system"
            },
            set: { persist(["locale_tag": $0]) })) {
            Text("settings.language_system").tag("system")
            Text("English").tag("en")
            Text("简体中文").tag("zh-Hans")
        }.accessibilityIdentifier("settings_language")
        Toggle("settings.autosave", isOn: boolean("autosave_enabled"))
        NavigationLink { CatalogSourceManagementView() } label: { Text("library.sources") }
    }
    @ViewBuilder private var aboutSection: some View {
        Text("FlyNES").font(.headline)
        Text("settings.about_description").foregroundStyle(.secondary)
        NavigationLink { LicenseListView() } label: { Text("settings.licenses") }
            .accessibilityIdentifier("settings_licenses")
    }

    private func number(_ key: String) -> UInt32 { (snapshot[key] as? NSNumber)?.uint32Value ?? 0 }
    private func integer(_ key: String) -> Binding<UInt32> {
        Binding(get: { number(key) }, set: { persist([key: $0]) })
    }
    private func boolean(_ key: String) -> Binding<Bool> {
        Binding(get: { number(key) != 0 }, set: { persist([key: $0 ? UInt32(1) : UInt32(0)]) })
    }
    private func decimal(_ key: String) -> Binding<Double> {
        Binding(get: { (snapshot[key] as? NSNumber)?.doubleValue ?? 0.22 }, set: { persist([key: $0]) })
    }
    private func load() { snapshot = FlyNesAppBridge.sharedInstance().settingsGet() }
    private func persist(_ patch: [String: Any]) {
        do {
            try FlyNesAppBridge.sharedInstance().applySettings(patch)
            load()
            if let tag = patch["locale_tag"] as? String {
                UserDefaults.standard.set(tag, forKey: "FlyNesLocaleTag")
            }
        } catch { failure = error.localizedDescription }
    }
    private func previewHaptics() {
        let level = number("haptic_level")
        guard level > 1 else { return }
        let distinct = number("distinct_ab_haptics") != 0
        let intensity: CGFloat = level == 2 ? 0.4 : level == 4 ? 1.0 : 0.7
        let style: UIImpactFeedbackGenerator.FeedbackStyle = level == 2 ? .light : level == 4 ? .heavy : .medium
        UIImpactFeedbackGenerator(style: distinct ? .rigid : style).impactOccurred(intensity: intensity)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.26) {
            UIImpactFeedbackGenerator(style: distinct ? .soft : style).impactOccurred(intensity: intensity)
        }
    }
}

private struct LicenseListView: View {
    /// The component licences plus one row per bundled game, from the manifest.
    private let licenses: [(String, String)] = {
        var entries: [(String, String)] = [
            ("FlyNES", "FlyNES-GPL-2.0"),
            ("Nestopia UE", "Nestopia-GPL-2.0"),
            ("MMPX", "MMPX-MIT"),
            ("ScaleFX", "ScaleFX-MIT"),
            ("zlib", "zlib-license")
        ]
        for game in FlyNesBuiltinGames.shared().all() where !game.licenseFile.isEmpty {
            entries.append((game.titleEn, (game.licenseFile as NSString).deletingPathExtension))
        }
        return entries
    }()
    var body: some View {
        List(licenses, id: \.0) { title, resource in
            NavigationLink(title) {
                ScrollView {
                    Text(contents(resource))
                        .font(.system(.caption, design: .monospaced))
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding()
                }.navigationTitle(title).navigationBarTitleDisplayMode(.inline)
            }
        }.navigationTitle("settings.licenses").navigationBarTitleDisplayMode(.inline)
    }
    private func contents(_ name: String) -> String {
        guard let url = Bundle.main.url(forResource: name, withExtension: "txt"),
              let text = try? String(contentsOf: url, encoding: .utf8) else {
            return FlyNesLocalizedString("settings.license_missing")
        }
        return text
    }
}
