import SwiftUI

struct ControlLayoutEditorView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var draft = ControlLayoutDraft()
    @State private var selected = "A"
    @State private var directionMode: UInt32 = 2
    @State private var previewSettings: [String: Any] = [:]
    @State private var status = ""
    @State private var loaded = false
    @State private var history: [ControlLayoutDraft] = []
    @State private var dragging = false
    @State private var tryMode = false
    @State private var confirmSave = false

    private var deviceSize: CGSize {
        let size = UIScreen.main.bounds.size
        return CGSize(width: max(size.width, size.height), height: min(size.width, size.height))
    }
    private var safeRect: CGRect {
        let insets = (UIApplication.shared.connectedScenes.first as? UIWindowScene)?
            .windows.first(where: \.isKeyWindow)?.safeAreaInsets ?? .zero
        return CGRect(x: insets.left, y: insets.top,
                      width: deviceSize.width - insets.left - insets.right,
                      height: deviceSize.height - insets.top - insets.bottom)
    }

    var body: some View {
        HStack(spacing: 0) {
            GeometryReader { geo in
                let scale = min(geo.size.width / deviceSize.width, geo.size.height / deviceSize.height)
                ZStack {
                    Color.black
                    preview
                        .frame(width: deviceSize.width, height: deviceSize.height)
                        .scaleEffect(scale)
                        .frame(width: deviceSize.width * scale, height: deviceSize.height * scale)
                }
                .frame(width: geo.size.width, height: geo.size.height)
                .clipped()
            }
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    Group {
                    Text("control_layout.hint").font(.caption).foregroundStyle(.secondary)
                    Picker("control_layout.selected", selection: $selected) {
                        ForEach(draft.elements) { element in
                            Text(LocalizedStringKey("control.\(element.name)")).tag(element.name)
                        }
                    }
                    }
                    Text("control_layout.scale")
                    Slider(value: selectedScale, in: 0.5...1.8)
                        .accessibilityIdentifier("layout_scale")
                    Text("control_layout.opacity")
                    Slider(value: opacityBinding, in: 0.4...1)
                        .accessibilityIdentifier("layout_opacity")
                    Toggle("control_layout.try", isOn: $tryMode)
                        .accessibilityIdentifier("layout_try")
                    if let warning = draft.warningKeys.first {
                        Text(LocalizedStringKey(warning)).font(.caption).foregroundStyle(.orange)
                    }
                    Button("control_layout.undo") {
                        if let previous = history.popLast() { draft = previous }
                    }.disabled(history.isEmpty).accessibilityIdentifier("layout_undo")
                    Button("control_layout.recommended") { remember(); draft = ControlLayoutDraft() }
                        .accessibilityIdentifier("layout_reset")
                    if !status.isEmpty { Text(status).font(.caption).foregroundStyle(.red) }
                }.padding()
            }
            .frame(width: 240)
        }
        .navigationTitle("control_layout.title")
        .navigationBarTitleDisplayMode(.inline)
        .navigationBarBackButtonHidden()
        .toolbar {
            ToolbarItem(placement: .cancellationAction) {
                Button("common.cancel") { dismiss() }.accessibilityIdentifier("layout_cancel")
            }
            ToolbarItem(placement: .confirmationAction) {
                Button("control_layout.save") {
                    if draft.warningKeys.isEmpty { save() } else { confirmSave = true }
                }.accessibilityIdentifier("layout_save")
            }
        }
        .alert("control_layout.warning_save", isPresented: $confirmSave) {
            Button("control_layout.keep_editing", role: .cancel) { }
            Button("control_layout.save", action: save)
        }
        .onAppear {
            guard !loaded else { return }
            draft = ControlLayoutDraft(encoded: FlyNesAppBridge.sharedInstance().controlLayoutGet())
            previewSettings = FlyNesAppBridge.sharedInstance().settingsGet()
            directionMode = (previewSettings["direction_mode"] as? NSNumber)?.uint32Value ?? 2
            loaded = true
        }
    }

    private var preview: some View {
        ZStack(alignment: .topLeading) {
            Color.black
            LayoutTestFrame()
                .frame(width: safeRect.width, height: safeRect.height)
                .offset(x: safeRect.minX, y: safeRect.minY)
            LayoutGamepadPreview(encoded: draft.encoded, directionMode: directionMode, tryMode: tryMode, settings: previewSettings)
                .frame(width: safeRect.width, height: safeRect.height)
                .offset(x: safeRect.minX, y: safeRect.minY)
                .allowsHitTesting(tryMode)
            ForEach(draft.elements) { element in
                let rect = draft.bounds(for: element, safe: safeRect, directionMode: directionMode)
                RoundedRectangle(cornerRadius: element.name == "A" ? rect.width / 2 : 10)
                    .stroke(element.name == selected && !tryMode ? Color.orange : Color.clear, lineWidth: 3)
                    .background(Color.white.opacity(0.001))
                    .frame(width: rect.width, height: rect.height)
                    .position(x: rect.midX, y: rect.midY)
                    .contentShape(Rectangle())
                    .allowsHitTesting(!tryMode)
                    .gesture(drag(element))
                    .onTapGesture { selected = element.name }
                    .accessibilityLabel(Text(LocalizedStringKey("control.\(element.name)")))
            }
        }.coordinateSpace(name: "layoutStage")
    }

    private var selectedScale: Binding<Double> {
        Binding(get: { draft.elements.first(where: { $0.name == selected })?.scale ?? 1 },
                set: { value in
                    if let index = draft.elements.firstIndex(where: { $0.name == selected }) {
                        remember()
                        draft.elements[index].scale = value
                    }
                })
    }
    private var opacityBinding: Binding<Double> {
        Binding(get: { draft.opacity }, set: { remember(); draft.opacity = $0 })
    }
    private func remember() {
        if history.count >= 20 { history.removeFirst() }
        history.append(draft)
    }
    private func drag(_ element: ControlLayoutDraft.Element) -> some Gesture {
        DragGesture(coordinateSpace: .named("layoutStage"))
            .onChanged { value in
                guard !tryMode else { return }
                if !dragging { remember(); dragging = true }
                selected = element.name
                if let index = draft.elements.firstIndex(where: { $0.name == element.name }) {
                    draft.elements[index].x = min(1, max(0, (value.location.x - safeRect.minX) / safeRect.width))
                    draft.elements[index].y = min(1, max(0, (value.location.y - safeRect.minY) / safeRect.height))
                }
            }
            .onEnded { _ in dragging = false }
    }
    private func save() {
        do {
            try FlyNesAppBridge.sharedInstance().controlLayoutApply(draft.encoded)
            dismiss()
        } catch { status = FlyNesLocalizedString("settings.save_failed") + "\n" + error.localizedDescription }
    }
}

private final class LayoutPreviewOverlay: GamepadOverlayView {
    override var safeAreaInsets: UIEdgeInsets { .zero }
}
private struct LayoutGamepadPreview: UIViewRepresentable {
    let encoded: String
    let directionMode: UInt32
    let tryMode: Bool
    let settings: [String: Any]
    func makeUIView(context: Context) -> GamepadOverlayView {
        let view = LayoutPreviewOverlay(frame: .zero)
        view.isUserInteractionEnabled = tryMode
        return view
    }
    func updateUIView(_ view: GamepadOverlayView, context: Context) {
        if view.isUserInteractionEnabled && !tryMode { view.releaseAllButtons() }
        view.isUserInteractionEnabled = tryMode
        view.layoutUtf8 = encoded
        view.deadZone = (settings["dead_zone"] as? NSNumber)?.floatValue ?? 0.22
        view.hapticLevel = (settings["haptic_level"] as? NSNumber)?.uint32Value ?? 2
        view.distinctAbHaptics = (settings["distinct_ab_haptics"] as? NSNumber)?.boolValue ?? true
        view.joystickMode = FlyNesJoystickMode(rawValue: Int(directionMode)) ?? FlyNesJoystickMode(rawValue: 2)!
    }
}

/// Same deterministic NES-style test frame as the Android layout editor.
private struct LayoutTestFrame: View {
    var body: some View {
        Canvas { context, size in
            let width = min(size.width, size.height * 4 / 3)
            let left = (size.width - width) / 2
            func rect(_ x: Double, _ y: Double, _ w: Double, _ h: Double, _ color: Color) {
                context.fill(Path(CGRect(x: left + width * x, y: size.height * y,
                                         width: width * w, height: size.height * h)), with: .color(color))
            }
            rect(0, 0, 1, 1, Color(red: 155 / 255, green: 196 / 255, blue: 215 / 255))
            rect(0.15, 0.12, 0.7, 0.3, Color(red: 101 / 255, green: 112 / 255, blue: 143 / 255))
            for column in 0..<7 {
                rect(0.22 + Double(column) * 0.09, 0.62, 0.05, 0.38,
                     Color(red: 52 / 255, green: 57 / 255, blue: 75 / 255))
            }
            rect(0.47, 0.58, 0.06, 0.42, Color(red: 1, green: 107 / 255, blue: 94 / 255))
        }
    }
}
