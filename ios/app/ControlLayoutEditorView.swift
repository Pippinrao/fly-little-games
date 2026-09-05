import SwiftUI

/// Landscape ControlLayoutV2 editor. Persist is `fly_control_layout_apply`
/// through `FlyNesAppBridge`.
struct ControlLayoutEditorView: View {
    private static let names = ["D_PAD", "A", "B", "SELECT", "START"]
    private static let recommended =
        "v2|0.5200|LANDSCAPE|D_PAD,0.1000,0.7600,1.0000|A,0.9400,0.6400,1.0000|B,0.8700,0.8600,1.0000|SELECT,0.0900,0.2800,1.0000|START,0.9400,0.2800,1.0000"

    @Environment(\.dismiss) private var dismiss
    @State private var opacity: Double = 0.52
    @State private var selected = "A"
    @State private var elements: [LayoutElement] = ControlLayoutEditorView.defaultElements()
    @State private var status = ""
    @State private var stage = CGSize(width: 1, height: 1)

    var body: some View {
        HStack(spacing: 0) {
            GeometryReader { geo in
                ZStack {
                    Color.black
                    ForEach(elements) { element in
                        Text(element.name)
                            .font(.caption.weight(.bold))
                            .foregroundStyle(.white)
                            .frame(width: elementWidth(element), height: elementHeight(element))
                            .background(element.name == selected ? Color.white.opacity(0.22) : Color.white.opacity(0.12))
                            .overlay(
                                RoundedRectangle(cornerRadius: element.name == "A" || element.name == "B" ? 40 : 10)
                                    .stroke(element.name == selected ? Color.orange : Color.white.opacity(0.7), lineWidth: 2)
                            )
                            .position(
                                x: element.x * geo.size.width,
                                y: element.y * geo.size.height
                            )
                            .gesture(drag(element.name, in: geo.size))
                            .onTapGesture {
                                selected = element.name
                            }
                    }
                }
                .onAppear { stage = geo.size }
                .onChange(of: geo.size) { _, size in
                    stage = size
                }
            }
            VStack(alignment: .leading, spacing: 12) {
                Text("control_layout.title")
                    .font(.title2.weight(.bold))
                Text("control_layout.hint")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                if !status.isEmpty {
                    Text(status)
                        .font(.caption)
                        .foregroundStyle(.red)
                }
                Text("control_layout.scale")
                Slider(value: selectedScaleBinding, in: 0.5...1.8)
                Text("control_layout.opacity")
                Slider(value: $opacity, in: 0.4...1.0)
                Spacer()
                Button("control_layout.recommended") {
                    applyEncoded(Self.recommended)
                }
                Button("control_layout.save") {
                    save()
                }
                .buttonStyle(.borderedProminent)
            }
            .padding()
            .frame(width: 280)
        }
        .navigationTitle("control_layout.title")
        .navigationBarTitleDisplayMode(.inline)
        .onAppear(perform: load)
    }

    private var selectedScaleBinding: Binding<Double> {
        Binding(
            get: { elements.first(where: { $0.name == selected })?.scale ?? 1 },
            set: { value in
                if let index = elements.firstIndex(where: { $0.name == selected }) {
                    elements[index].scale = min(1.8, max(0.5, value))
                }
            }
        )
    }

    private func drag(_ name: String, in size: CGSize) -> some Gesture {
        DragGesture()
            .onChanged { value in
                selected = name
                if let index = elements.firstIndex(where: { $0.name == name }) {
                    elements[index].x = min(1, max(0, value.location.x / max(1, size.width)))
                    elements[index].y = min(1, max(0, value.location.y / max(1, size.height)))
                }
            }
    }

    private func elementWidth(_ element: LayoutElement) -> CGFloat {
        CGFloat((element.name == "SELECT" || element.name == "START" ? 72.0 : 64.0) * element.scale)
    }

    private func elementHeight(_ element: LayoutElement) -> CGFloat {
        CGFloat((element.name == "SELECT" || element.name == "START" ? 28.0 : 64.0) * element.scale)
    }

    private func load() {
        applyEncoded(FlyNesAppBridge.sharedInstance().controlLayoutGet())
    }

    private func save() {
        do {
            try FlyNesAppBridge.sharedInstance().controlLayoutApply(encode())
            status = ""
            dismiss()
        } catch {
            status = error.localizedDescription
        }
    }

    private func applyEncoded(_ encoded: String) {
        let parsed = Self.parseWire(encoded)
        opacity = parsed.opacity
        elements = parsed.elements
    }

    private func encode() -> String {
        var out = String(format: "v2|%.4f|LANDSCAPE", opacity)
        for element in elements {
            out += "|\(element.name),"
            out += String(format: "%.4f,%.4f,%.4f", element.x, element.y, element.scale)
        }
        return out
    }

    private static func defaultElements() -> [LayoutElement] {
        parseWire(recommended).elements
    }

    private static func parseWire(_ encoded: String) -> ParsedLayout {
        let parts = encoded.split(separator: "|").map(String.init)
        var parsed = ParsedLayout()
        if parts.count >= 8, parts[0] == "v2" {
            parsed.opacity = Double(parts[1]) ?? 0.52
            if parsed.opacity < 0.4 || parsed.opacity > 1.0 {
                parsed.opacity = 0.52
            }
            for part in parts.dropFirst(3) {
                let fields = part.split(separator: ",").map(String.init)
                if fields.count == 4 {
                    parsed.elements.append(LayoutElement(
                        name: fields[0],
                        x: Double(fields[1]) ?? 0,
                        y: Double(fields[2]) ?? 0,
                        scale: Double(fields[3]) ?? 1
                    ))
                }
            }
        }
        if parsed.elements.map(\.name) != names {
            parsed.opacity = 0.52
            parsed.elements = [
                LayoutElement(name: "D_PAD", x: 0.10, y: 0.76, scale: 1),
                LayoutElement(name: "A", x: 0.94, y: 0.64, scale: 1),
                LayoutElement(name: "B", x: 0.87, y: 0.86, scale: 1),
                LayoutElement(name: "SELECT", x: 0.09, y: 0.28, scale: 1),
                LayoutElement(name: "START", x: 0.94, y: 0.28, scale: 1),
            ]
        }
        return parsed
    }
}

private struct LayoutElement: Identifiable {
    var id: String { name }
    var name: String
    var x: Double
    var y: Double
    var scale: Double
}

private struct ParsedLayout {
    var opacity: Double = 0.52
    var elements: [LayoutElement] = []
}
