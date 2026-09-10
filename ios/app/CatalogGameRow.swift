import SwiftUI

struct CatalogGameRow: View {
    let game: CatalogGame
    var selected = false
    var largeText = false

    var body: some View {
        HStack(spacing: 8) {
            if !largeText {
                Text(game.displayName)
                    .font(.system(size: 10, weight: .bold)).lineLimit(2)
                    .multilineTextAlignment(.center)
                    .padding(4).frame(width: 80).frame(maxHeight: .infinity)
                    .background(Color(uiColor: .tertiarySystemFill))
                    .accessibilityHidden(true)
            }
            VStack(alignment: .leading, spacing: 2) {
                Text(game.displayName).font(.body.weight(.semibold)).lineLimit(2)
                if game.sourceScope == 1 {
                    Text("game_center.builtin").font(.system(size: 11)).foregroundColor(.secondary).lineLimit(1)
                }
            }.frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(8)
        .frame(width: largeText ? 232 : 168)
        .frame(maxHeight: .infinity)
        .background(selected ? Color.accentColor.opacity(0.16) : Color(uiColor: .secondarySystemGroupedBackground),
                    in: RoundedRectangle(cornerRadius: 12))
        .overlay(RoundedRectangle(cornerRadius: 12).stroke(selected ? Color.accentColor : Color.clear, lineWidth: 2))
        .accessibilityElement(children: .combine)
        .accessibilityLabel(game.displayName)
        .accessibilityAddTraits(selected ? .isSelected : [])
    }
}
