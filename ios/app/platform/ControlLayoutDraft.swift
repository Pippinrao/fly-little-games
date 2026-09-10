import Foundation
import CoreGraphics

/// A value-type editing buffer. The shared bridge remains the authority when saving.
struct ControlLayoutDraft {
    struct Element: Identifiable {
        var id: String { name }
        var name: String
        var x: Double
        var y: Double
        var scale: Double
    }
    static let recommended = "v2|0.5200|LANDSCAPE|D_PAD,0.1000,0.7600,1.0000|A,0.9400,0.6400,1.0000|B,0.8700,0.8600,1.0000|SELECT,0.0900,0.2800,1.0000|START,0.9400,0.2800,1.0000"
    var opacity: Double = 0.52
    var elements: [Element] = []

    init(encoded: String = Self.recommended) {
        let parts = encoded.components(separatedBy: "|")
        if parts.count == 8, parts[0] == "v2", parts[2] == "LANDSCAPE",
           let alpha = Double(parts[1]), alpha.isFinite, (0.4...1).contains(alpha) {
            opacity = alpha
            for part in parts.dropFirst(3) {
                let fields = part.components(separatedBy: ",")
                guard fields.count == 4, let x = Double(fields[1]), let y = Double(fields[2]),
                      let scale = Double(fields[3]), x.isFinite, y.isFinite, scale.isFinite,
                      (0...1).contains(x), (0...1).contains(y), (0.5...1.8).contains(scale) else { break }
                elements.append(Element(name: fields[0], x: x, y: y, scale: scale))
            }
        }
        if elements.map(\.name) != ["D_PAD", "A", "B", "SELECT", "START"] {
            self = Self(encoded: Self.recommended)
        }
    }

    var encoded: String {
        let locale = Locale(identifier: "en_US_POSIX")
        return String(format: "v2|%.4f|LANDSCAPE", locale: locale, opacity) + elements.map {
            "|\($0.name)," + String(format: "%.4f,%.4f,%.4f", locale: locale, $0.x, $0.y, $0.scale)
        }.joined()
    }

    /// Same point-density 1 geometry as GamepadHitMap::from_layout used by iOS.
    func bounds(for element: Element, safe: CGRect, directionMode: UInt32) -> CGRect {
        let base: CGSize
        switch element.name {
        case "D_PAD": base = CGSize(width: directionMode == 3 ? 144 : 128, height: directionMode == 3 ? 144 : 128)
        case "A": base = CGSize(width: 72, height: 72)
        case "B": base = CGSize(width: 64, height: 64)
        default: base = CGSize(width: 72, height: 48)
        }
        let width = base.width * element.scale, height = base.height * element.scale
        let x = max(safe.minX + width / 2, min(safe.maxX - width / 2, safe.minX + element.x * safe.width))
        let y = max(safe.minY + height / 2, min(safe.maxY - height / 2, safe.minY + element.y * safe.height))
        return CGRect(x: x - width / 2, y: y - height / 2, width: width, height: height)
    }
}
