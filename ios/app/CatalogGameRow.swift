import SwiftUI

struct CatalogGameRow: View {
    let game: CatalogGame
    var selected = false
    var largeText = false
    var cover: UIImage?

    var body: some View {
        HStack(spacing: 8) {
            if !largeText {
                // Android card art: the cover once a native frame passed the quality
                // gate, otherwise the localized title as the placeholder.
                ZStack {
                    if let cover {
                        Image(uiImage: cover)
                            .resizable()
                            .interpolation(.none)
                            .aspectRatio(contentMode: .fill)
                    } else {
                        Color(uiColor: .tertiarySystemFill)
                        Text(game.titlePrimary)
                            .font(.system(size: 10, weight: .bold)).lineLimit(3)
                            .multilineTextAlignment(.center)
                            .padding(4)
                    }
                }
                .frame(width: 80, height: 60)
                .clipped()
                .accessibilityHidden(true)
            }
            VStack(alignment: .leading, spacing: 2) {
                Text(game.titlePrimary).font(.body.weight(.semibold)).lineLimit(2)
                if metadata.isEmpty == false {
                    Text(metadata).font(.system(size: 11)).foregroundColor(.secondary).lineLimit(1)
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
        .accessibilityLabel(metadata.isEmpty ? game.titlePrimary : "\(game.titlePrimary), \(metadata)")
        .accessibilityAddTraits(selected ? .isSelected : [])
    }

    /// Android shows the other language first and only falls back to the builtin
    /// badge when no second title exists.
    private var metadata: String {
        if !game.titleSecondary.isEmpty { return game.titleSecondary }
        return game.sourceScope == 1 ? FlyNesLocalizedString("game_center.builtin") : ""
    }
}
