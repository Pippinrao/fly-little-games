import SwiftUI

/// Settings page bound to `fly_settings_get` / `fly_settings_apply` through the ObjC++ bridge.
struct SettingsView: View {
    @State private var aspectMode: UInt32 = 1
    @State private var spatialMode: UInt32 = 2
    @State private var postEffect: UInt32 = 1
    @State private var directionMode: UInt32 = 3
    @State private var controlOpacity: Float = 0.85
    @State private var hapticLevel: UInt32 = 3
    @State private var audioEnabled: UInt32 = 1
    @State private var status: String = ""

    var body: some View {
        NavigationStack {
            Form {
                Section("settings.video") {
                    Picker("Aspect", selection: $aspectMode) {
                        Text("4:3").tag(UInt32(1))
                        Text("Square").tag(UInt32(2))
                        Text("Integer").tag(UInt32(3))
                    }
                    Picker("Spatial", selection: $spatialMode) {
                        Text("Nearest").tag(UInt32(1))
                        Text("Sharp").tag(UInt32(2))
                        Text("MMPX").tag(UInt32(3))
                        Text("ScaleFX").tag(UInt32(4))
                    }
                    Picker("CRT", selection: $postEffect) {
                        Text("Off").tag(UInt32(1))
                        Text("On").tag(UInt32(2))
                    }
                }
                Section("settings.controls") {
                    Picker("Direction", selection: $directionMode) {
                        Text("Follow joystick").tag(UInt32(1))
                        Text("Fixed joystick").tag(UInt32(2))
                        Text("D-pad").tag(UInt32(3))
                    }
                    Slider(value: Binding(
                        get: { Double(controlOpacity) },
                        set: { controlOpacity = Float($0) }
                    ), in: 0.40...1.00)
                    Picker("Haptics", selection: $hapticLevel) {
                        Text("Off").tag(UInt32(1))
                        Text("Light").tag(UInt32(2))
                        Text("Standard").tag(UInt32(3))
                        Text("Strong").tag(UInt32(4))
                    }
                }
                Section("settings.audio") {
                    Toggle("Audio", isOn: Binding(
                        get: { audioEnabled != 0 },
                        set: { audioEnabled = $0 ? 1 : 0 }
                    ))
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

    private func load() {
        let snapshot = FlyNesAppBridge.sharedInstance().settingsGet()
        aspectMode = snapshot["aspect_mode"] as? UInt32 ?? aspectMode
        spatialMode = snapshot["custom_spatial_mode"] as? UInt32 ?? spatialMode
        postEffect = snapshot["custom_post_effect"] as? UInt32 ?? postEffect
        directionMode = snapshot["direction_mode"] as? UInt32 ?? directionMode
        if let opacity = snapshot["control_opacity"] as? NSNumber {
            controlOpacity = opacity.floatValue
        }
        hapticLevel = snapshot["haptic_level"] as? UInt32 ?? hapticLevel
        audioEnabled = snapshot["audio_enabled"] as? UInt32 ?? audioEnabled
    }

    private func apply() {
        let payload: [String: Any] = [
            "aspect_mode": aspectMode,
            "custom_spatial_mode": spatialMode,
            "custom_post_effect": postEffect,
            "direction_mode": directionMode,
            "control_opacity": controlOpacity,
            "haptic_level": hapticLevel,
            "audio_enabled": audioEnabled,
        ]
        do {
            try FlyNesAppBridge.sharedInstance().applySettings(payload)
            status = "applied"
        } catch {
            status = error.localizedDescription
        }
    }
}
