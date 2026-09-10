import SwiftUI

/// Placeholder detail surface. Play navigation hosts UIKit; locators stay out of this model.
struct CatalogGameDetailView: View {
    let game: CatalogGame

    var body: some View {
        List {
            Section {
                Text(game.displayName)
                    .font(.title2)
                Text(game.canonicalId)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)
            }
            Section {
                if mapsToBuiltinFromBelow {
                    NavigationLink(value: LibraryRoute.run(game.canonicalId)) {
                        Text("library.play")
                    }
                    .disabled(game.compatibilityState != 1)
                } else {
                    Text("library.play")
                        .foregroundStyle(.tertiary)
                    Text("library.rom_open_failed")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .navigationTitle("library.detail")
    }

    private var mapsToBuiltinFromBelow: Bool {
        let cid = game.canonicalId.lowercased()
        return cid == "builtin" || cid.contains("from_below") || cid.contains("from-below")
    }
}
