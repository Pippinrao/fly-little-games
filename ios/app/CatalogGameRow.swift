import SwiftUI

struct CatalogGameRow: View {
    let game: CatalogGame

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(game.displayName)
                .font(.body)
            Text(game.canonicalId)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(game.displayName)
    }
}
