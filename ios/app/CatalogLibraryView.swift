import SwiftUI

enum LibraryRoute: Hashable {
    case detail(CatalogGame)
    case run(String)
}

enum LibraryFilter: String, CaseIterable, Identifiable {
    case recent = "RECENT"
    case favorites = "FAVORITES"
    case all = "ALL"
    case builtin = "BUILTIN"

    var id: String { rawValue }

    var titleKey: LocalizedStringKey {
        switch self {
        case .recent: return "game_center.recent"
        case .favorites: return "game_center.favorites"
        case .all: return "game_center.all"
        case .builtin: return "game_center.builtin"
        }
    }
}

/// Game Center: four categories, search, landscape-first, no extra tabs.
/// Scanning still happens through borrowed FDs in platform code; this view
/// never holds security-scoped bookmarks.
struct CatalogLibraryView: View {
    @State private var snapshot: CatalogSnapshot
    @State private var filter: LibraryFilter = .all
    @State private var searchText = ""
    @State private var path = NavigationPath()
    @ObservedObject private var sources = CatalogSourceModel.shared

    init(snapshot: CatalogSnapshot = CatalogSnapshot(generation: 0, games: [])) {
        _snapshot = State(initialValue: snapshot)
    }

    var body: some View {
        NavigationStack(path: $path) {
            Group {
                if visibleGames.isEmpty {
                    VStack(spacing: 12) {
                        Image(systemName: "square.stack").font(.largeTitle)
                        Text("library.no_games").font(.headline)
                        Text("library.no_games.detail").foregroundStyle(.secondary)
                        if let error = sources.error {
                            Text(error).font(.footnote).foregroundStyle(.red)
                        }
                        NavigationLink("library.sources") { CatalogSourceManagementView() }
                    }
                    .padding()
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else {
                    List(visibleGames) { game in
                        NavigationLink(value: LibraryRoute.detail(game)) {
                            CatalogGameRow(game: game)
                        }
                    }
                }
            }
            .navigationTitle("game_center.title")
            .navigationBarTitleDisplayMode(.inline)
            .searchable(text: $searchText, prompt: Text("library.search"))
            .toolbar {
                ToolbarItem(placement: .principal) {
                    Picker("game_center.title", selection: $filter) {
                        ForEach(LibraryFilter.allCases) { item in
                            Text(item.titleKey).tag(item)
                        }
                    }
                    .pickerStyle(.segmented)
                    .frame(maxWidth: 520)
                    .frame(minHeight: 44)
                }
                ToolbarItemGroup(placement: .navigationBarTrailing) {
                    NavigationLink {
                        CatalogSourceManagementView()
                    } label: {
                        Text("library.sources")
                    }
                    NavigationLink {
                        SettingsView()
                    } label: {
                        Text("settings.title")
                    }
                }
            }
            .navigationDestination(for: LibraryRoute.self) { route in
                switch route {
                case .detail(let game):
                    CatalogGameDetailView(game: game)
                case .run(let canonicalId):
                    RunGameContainer(canonicalId: canonicalId, path: $path)
                }
            }
            .onAppear {
                sources.initialize()
                reloadSnapshot()
            }
            .onReceive(sources.$generation) { _ in reloadSnapshot() }
            .overlay(alignment: .bottom) {
                if sources.busy {
                    ProgressView("library.source.working").padding().background(.regularMaterial, in: Capsule())
                }
            }
            .onChange(of: filter) { _ in
                reloadSnapshot()
            }
            .onChange(of: searchText) { _ in
                reloadSnapshot()
            }
        }
    }

    private var visibleGames: [CatalogGame] {
        snapshot.games
    }

    private func reloadSnapshot() {
        let rows = FlyNesAppBridge.sharedInstance().gameCenterFilteredGames(
            forCategory: filter.rawValue,
            query: searchText
        )
        let games: [CatalogGame] = rows.compactMap { row in
            guard let canonicalId = row["canonicalId"] as? String,
                  let displayName = row["displayName"] as? String else {
                return nil
            }
            return CatalogGame(
                canonicalId: canonicalId,
                displayName: displayName,
                compatibilityState: (row["compatibilityState"] as? NSNumber)?.uint32Value ?? 0,
                freshness: (row["freshness"] as? NSNumber)?.uint32Value ?? 0,
                sourceScope: (row["sourceScope"] as? NSNumber)?.uint32Value ?? 0,
                favorite: (row["favorite"] as? NSNumber)?.uint32Value ?? 0,
                lastPlayedSequence: (row["lastPlayedSequence"] as? NSNumber)?.uint64Value ?? 0
            )
        }
        snapshot = CatalogSnapshot(generation: snapshot.generation &+ 1, games: games)
    }
}
