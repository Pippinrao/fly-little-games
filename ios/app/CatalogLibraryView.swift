import SwiftUI

enum LibraryFilter: String, CaseIterable, Identifiable {
    case recent
    case favorites
    case all
    case builtin

    var id: String { rawValue }

    var titleKey: LocalizedStringKey {
        switch self {
        case .recent: return "library.recent"
        case .favorites: return "library.favorites"
        case .all: return "library.all"
        case .builtin: return "library.builtin"
        }
    }
}

/// Read-only library list of every game in a `CatalogSnapshot`.
/// Scanning still happens through borrowed FDs in platform code; this view
/// never holds security-scoped bookmarks.
struct CatalogLibraryView: View {
    let snapshot: CatalogSnapshot
    @State private var filter: LibraryFilter = .all
    @State private var searchText = ""

    init(snapshot: CatalogSnapshot = CatalogSnapshot(generation: 0, games: [])) {
        self.snapshot = snapshot
    }

    var body: some View {
        NavigationStack {
            Group {
                if visibleGames.isEmpty {
                    ContentUnavailableView(
                        "library.no_games",
                        systemImage: "square.stack",
                        description: Text("library.no_games.detail")
                    )
                } else {
                    List(visibleGames) { game in
                        NavigationLink {
                            CatalogGameDetailView(game: game)
                        } label: {
                            CatalogGameRow(game: game)
                        }
                    }
                }
            }
            .navigationTitle("library.title")
            .searchable(text: $searchText, prompt: Text("library.search"))
            .toolbar {
                ToolbarItem(placement: .principal) {
                    Picker("library.title", selection: $filter) {
                        ForEach(LibraryFilter.allCases) { item in
                            Text(item.titleKey).tag(item)
                        }
                    }
                    .pickerStyle(.segmented)
                    .frame(maxWidth: 420)
                }
                ToolbarItem(placement: .topBarTrailing) {
                    NavigationLink {
                        CatalogSourceManagementView()
                    } label: {
                        Text("library.sources")
                    }
                }
            }
        }
    }

    private var visibleGames: [CatalogGame] {
        snapshot.games.filter { game in
            switch filter {
            case .all:
                return true
            case .recent:
                return game.lastPlayedSequence > 0
            case .favorites:
                return game.favorite != 0
            case .builtin:
                return game.sourceScope == 1
            }
        }
        .filter { game in
            searchText.isEmpty
                || game.displayName.localizedCaseInsensitiveContains(searchText)
                || game.canonicalId.localizedCaseInsensitiveContains(searchText)
        }
    }
}
