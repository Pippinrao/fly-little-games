import SwiftUI

/// Read-only library list of every game in a `CatalogSnapshot`.
/// Scanning still happens through borrowed FDs in platform code; this view
/// never holds security-scoped bookmarks.
struct CatalogLibraryView: View {
    let snapshot: CatalogSnapshot

    init(snapshot: CatalogSnapshot = CatalogSnapshot(generation: 0, games: [])) {
        self.snapshot = snapshot
    }

    var body: some View {
        NavigationStack {
            Group {
                if snapshot.games.isEmpty {
                    ContentUnavailableView(
                        "No Games",
                        systemImage: "square.stack",
                        description: Text("This catalog snapshot has no indexed games.")
                    )
                } else {
                    List(snapshot.games) { game in
                        CatalogGameRow(game: game)
                    }
                }
            }
            .navigationTitle("Library")
        }
    }
}
