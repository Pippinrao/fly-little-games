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
                NavigationLink {
                    RunGameContainer(canonicalId: game.canonicalId)
                } label: {
                    Text("library.play")
                }
                .disabled(game.compatibilityState != 1)
            }
        }
        .navigationTitle("library.detail")
    }
}
