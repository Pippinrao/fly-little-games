import Foundation
import CoreGraphics
@main struct LayoutDraftTests {
 static func main() {
  let expected = "v2|0.5200|LANDSCAPE|D_PAD,0.1000,0.7600,1.0000|A,0.9400,0.6400,1.0000|B,0.8700,0.8600,1.0000|SELECT,0.0900,0.2800,1.0000|START,0.9400,0.2800,1.0000"
  let layout = ControlLayoutDraft(encoded: expected)
  precondition(layout.encoded == expected, "Wire format must preserve shared layout")
  var edited = layout
  edited.elements[1].scale = 1.8
  precondition(layout.elements[1].scale == 1, "Editing a draft must not mutate the saved layout")
  let safe = CGRect(x: 47, y: 0, width: 750, height: 369)
  let rect = edited.bounds(for: edited.elements[1], safe: safe, directionMode: 3)
  precondition(abs(rect.maxX - safe.maxX) < 0.001, "Large A button must clamp within safe bounds")
  precondition(abs(rect.width - 129.6) < 0.001, "A uses 72 point hit-map base size")
  let stick = layout.bounds(for: layout.elements[0], safe: safe, directionMode: 2)
  precondition(stick.width == 128, "Joystick uses 128 point hit-map size")
  let corrupt = ControlLayoutDraft(encoded: expected.replacingOccurrences(of: "0.9400", with: "nan"))
  precondition(corrupt.encoded == expected, "Non-finite wire values must fall back to recommended")
  var warningLayout = layout
  warningLayout.elements[1].scale = 0.5
  warningLayout.elements[1].x = 0.5
  warningLayout.elements[1].y = 0.98
  precondition(warningLayout.warningKeys.contains("control_layout.warning_small"), "Small target warning must match Android")
  precondition(warningLayout.warningKeys.contains("control_layout.warning_center"), "Central screen warning must match Android")
  precondition(warningLayout.warningKeys.contains("control_layout.warning_gesture"), "System gesture zone warning must match Android")
  warningLayout.elements[2] = .init(name: "B", x: 0.5, y: 0.98, scale: 1)
  precondition(warningLayout.warningKeys.contains("control_layout.overlap"), "Nearby controls must warn before save")
  print("PASS: Android layout warnings, layout wire, draft isolation, safe geometry, joystick geometry, corrupt input")
 }
}

