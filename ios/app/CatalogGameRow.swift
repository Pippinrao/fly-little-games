import SwiftUI

struct CatalogGameRow: View {
    let game: CatalogGame

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(game.displayName)
                    .font(.body)
                if game.favorite != 0 {
                    Image(systemName: "star.fill")
                        .foregroundStyle(.yellow)
                        .accessibilityHidden(true)
                }
            }
            Text(game.canonicalId)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(game.displayName)
    }
}
