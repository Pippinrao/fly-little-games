import SwiftUI

struct ControlLayoutEditorView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var draft = ControlLayoutDraft()
    @State private var selected = "A"
    @State private var directionMode: UInt32 = 2
    @State private var status = ""
    @State private var loaded = false

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
                    Text("control_layout.hint").font(.caption).foregroundStyle(.secondary)
                    Picker("control_layout.selected", selection: $selected) {
                        ForEach(draft.elements) { element in
                            Text(LocalizedStringKey("control.\(element.name)")).tag(element.name)
                        }
                    }
                    Text("control_layout.scale")
                    Slider(value: selectedScale, in: 0.5...1.8)
                        .accessibilityIdentifier("layout_scale")
                    Text("control_layout.opacity")
                    Slider(value: $draft.opacity, in: 0.4...1)
                        .accessibilityIdentifier("layout_opacity")
                    if hasOverlap {
                        Text("control_layout.overlap").font(.caption).foregroundStyle(.orange)
                    }
                    Button("control_layout.recommended") { draft = ControlLayoutDraft() }
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
                Button("control_layout.save", action: save).accessibilityIdentifier("layout_save")
            }
        }
        .onAppear {
            guard !loaded else { return }
            draft = ControlLayoutDraft(encoded: FlyNesAppBridge.sharedInstance().controlLayoutGet())
            directionMode = (FlyNesAppBridge.sharedInstance().settingsGet()["direction_mode"] as? NSNumber)?.uint32Value ?? 2
            loaded = true
        }
    }

    private var preview: some View {
        ZStack(alignment: .topLeading) {
            Color.black
            Rectangle().fill(Color.white.opacity(0.04))
                .frame(width: safeRect.width, height: safeRect.height)
                .offset(x: safeRect.minX, y: safeRect.minY)
            LayoutGamepadPreview(encoded: draft.encoded, directionMode: directionMode)
                .frame(width: safeRect.width, height: safeRect.height)
                .offset(x: safeRect.minX, y: safeRect.minY)
                .allowsHitTesting(false)
            ForEach(draft.elements) { element in
                let rect = draft.bounds(for: element, safe: safeRect, directionMode: directionMode)
                RoundedRectangle(cornerRadius: element.name == "A" ? rect.width / 2 : 10)
                    .stroke(element.name == selected ? Color.orange : Color.clear, lineWidth: 3)
                    .background(Color.white.opacity(0.001))
                    .frame(width: rect.width, height: rect.height)
                    .position(x: rect.midX, y: rect.midY)
                    .contentShape(Rectangle())
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
                        draft.elements[index].scale = value
                    }
                })
    }
    private var hasOverlap: Bool {
        let bounds = draft.elements.map { draft.bounds(for: $0, safe: safeRect, directionMode: directionMode) }
        return bounds.indices.contains { index in
            bounds.indices.contains { other in other > index && bounds[index].intersects(bounds[other]) }
        }
    }
    private func drag(_ element: ControlLayoutDraft.Element) -> some Gesture {
        DragGesture(coordinateSpace: .named("layoutStage"))
            .onChanged { value in
                selected = element.name
                if let index = draft.elements.firstIndex(where: { $0.name == element.name }) {
                    draft.elements[index].x = min(1, max(0, (value.location.x - safeRect.minX) / safeRect.width))
                    draft.elements[index].y = min(1, max(0, (value.location.y - safeRect.minY) / safeRect.height))
                }
            }
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
    func makeUIView(context: Context) -> GamepadOverlayView {
        let view = LayoutPreviewOverlay(frame: .zero)
        view.isUserInteractionEnabled = false
        return view
    }
    func updateUIView(_ view: GamepadOverlayView, context: Context) {
        view.layoutUtf8 = encoded
        view.joystickMode = FlyNesJoystickMode(rawValue: Int(directionMode)) ?? FlyNesJoystickMode(rawValue: 2)!
    }
}
