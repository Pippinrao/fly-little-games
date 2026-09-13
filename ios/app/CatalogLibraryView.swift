import SwiftUI

enum LibraryRoute: Hashable {
    /// The resolved ROM travels with the route, so reaching the run screen already
    /// means the game could be opened.
    case run(canonicalId: String, rom: Data, game: CatalogGame)
    /// 附近联机. A real destination, and the only route on this screen that does not
    /// need a ROM: the nearby pages exist to render the exact stage that blocks them.
    case nearby
}

enum LibraryFilter: String, CaseIterable, Identifiable {
    case recent = "RECENT", favorites = "FAVORITES", all = "ALL", builtin = "BUILTIN"
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

/// Mirrors Android HomeActivity: selected detail on the left, two-row horizontal
/// card grid on the right. Selecting a card never navigates away from the grid.
struct CatalogLibraryView: View {
    @State private var snapshot: CatalogSnapshot
    @State private var cachedRows: [[String: Any]] = []
    @AppStorage("GameCenterCategory") private var category = LibraryFilter.all.rawValue
    @AppStorage("GameCenterQuery") private var searchText = ""
    @AppStorage("GameCenterSelected.ALL") private var allSelection = ""
    @AppStorage("GameCenterSelected.RECENT") private var recentSelection = ""
    @AppStorage("GameCenterSelected.FAVORITES") private var favoriteSelection = ""
    @AppStorage("GameCenterSelected.BUILTIN") private var builtinSelection = ""
    @State private var searchOpen = false
    @State private var sourcesOpen = false
    @State private var settingsOpen = false
    @State private var path = NavigationPath()
    @ObservedObject private var sources = CatalogSourceModel.shared
    @ObservedObject private var covers = GameCoverModel.shared
    @Environment(\.dynamicTypeSize) private var typeSize
    @Environment(\.locale) private var locale

    init(snapshot: CatalogSnapshot = CatalogSnapshot(generation: 0, games: [])) {
        _snapshot = State(initialValue: snapshot)
    }

    private var largeText: Bool { typeSize.isAccessibilitySize }
    private var selectedID: String {
        get {
            switch LibraryFilter(rawValue: category) ?? .all {
            case .all: return allSelection
            case .recent: return recentSelection
            case .favorites: return favoriteSelection
            case .builtin: return builtinSelection
            }
        }
        nonmutating set {
            switch LibraryFilter(rawValue: category) ?? .all {
            case .all: allSelection = newValue
            case .recent: recentSelection = newValue
            case .favorites: favoriteSelection = newValue
            case .builtin: builtinSelection = newValue
            }
        }
    }
    private var compactHeader: Bool { largeText || locale.identifier == "en_XA" }
    private var selectedGame: CatalogGame? { snapshot.games.first { $0.id == selectedID } }

    var body: some View {
        NavigationStack(path: $path) {
            VStack(spacing: 0) {
                header
                if searchOpen {
                    HStack {
                        TextField("library.search", text: $searchText)
                            .textFieldStyle(.roundedBorder)
                            .accessibilityIdentifier("search_input")
                        icon("xmark", "common.cancel", "close_search") {
                            searchText = ""; searchOpen = false
                        }
                    }.frame(minHeight: 56)
                }
                if sourcesOpen {
                    CatalogSourceManagementView(onClose: { sourcesOpen = false })
                } else {
                    GeometryReader { geometry in
                        HStack(spacing: 8) {
                            CatalogGameDetailView(game: selectedGame, largeText: largeText,
                                                  cover: selectedGame.flatMap { covers.image(for: $0.id) },
                                                  onLaunch: launch)
                                .frame(width: max(0, (geometry.size.width - 8) * 0.3))
                            VStack(alignment: .leading, spacing: 4) {
                                status.lineLimit(2).frame(minHeight: 32, alignment: .leading)
                                ScrollView(.horizontal) {
                                    LazyHGrid(rows: Array(repeating: GridItem(.flexible(), spacing: 8),
                                                         count: largeText ? 1 : 2), spacing: 8) {
                                        ForEach(snapshot.games) { game in
                                            Button { selectedID = game.id } label: {
                                                CatalogGameRow(game: game, selected: selectedID == game.id,
                                                               largeText: largeText,
                                                               cover: covers.image(for: game.id))
                                            }
                                            .buttonStyle(.plain)
                                            .accessibilityIdentifier("game_card_" + game.id)
                                        }
                                    }.padding(4)
                                }
                                .accessibilityIdentifier("game_grid")
                                .frame(maxWidth: .infinity, maxHeight: .infinity)
                            }
                        }
                    }
                }
            }
            .padding(8)
            .background(Color(uiColor: .systemGroupedBackground))
            .toolbar(.hidden, for: .navigationBar)
            .navigationDestination(for: LibraryRoute.self) { route in
                switch route {
                case .run(let id, let rom, let game): RunGameContainer(canonicalId: id, romData: rom, game: game, path: $path)
                case .nearby: NearbyFriendsView()
                }
            }
            .fullScreenCover(isPresented: $settingsOpen) { SettingsView() }
            .onAppear {
                searchOpen = !searchText.isEmpty
                sources.initialize()
                reloadSnapshot()
            }
            .onReceive(sources.$generation) { _ in reloadSnapshot() }
            .onChange(of: category) { _ in sourcesOpen = false; reloadSnapshot() }
            .onChange(of: searchText) { _ in reloadSnapshot() }
            .onChange(of: locale) { _ in reprojectTitles() }
        }
    }

    private var header: some View {
        HStack(spacing: 4) {
            if !compactHeader {
                Text("game_center.title").font(.headline).lineLimit(1)
                    .accessibilityAddTraits(.isHeader).padding(.trailing, 8)
            }
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 0) {
                    ForEach(LibraryFilter.allCases) { item in
                        Button {
                            category = item.rawValue
                            sourcesOpen = false
                        } label: {
                            Text(item.titleKey).lineLimit(1)
                                .padding(.horizontal, 14)
                                .frame(minWidth: 72, minHeight: largeText ? 64 : 48)
                                .foregroundColor(category == item.rawValue ? .white : .primary)
                                .background(category == item.rawValue ? Color.accentColor : Color.clear)
                        }
                        .buttonStyle(.plain)
                        .accessibilityIdentifier("category_" + item.rawValue.lowercased())
                        .accessibilityAddTraits(category == item.rawValue ? .isSelected : [])
                    }
                }
                .background(Color(uiColor: .secondarySystemGroupedBackground))
                .clipShape(RoundedRectangle(cornerRadius: 12))
            }
            icon("magnifyingglass", "library.search", "open_search") { searchOpen = true; sourcesOpen = false }
            icon("folder", "library.sources", "open_sources") { sourcesOpen = true }
            // 附近联机 sits with the other header actions on every platform's game center.
            icon("dot.radiowaves.left.and.right", "nearby.title", "open_nearby") {
                sourcesOpen = false; searchOpen = false; path.append(LibraryRoute.nearby)
            }
            icon("gearshape", "settings.title", "open_settings") { settingsOpen = true }
        }.frame(height: largeText ? 80 : 64)
    }

    @ViewBuilder private var status: some View {
        if sources.busy {
            HStack { ProgressView(); Text("library.source.working") }
        } else if let launching = sources.launching {
            HStack { ProgressView(); Text(String(format: FlyNesLocalizedString("library.launching_game"), launching)) }
        } else if let error = sources.error {
            Text(error).foregroundColor(.red)
        } else if snapshot.games.isEmpty {
            Text(searchText.isEmpty ? "library.no_games" : "library.search.empty")
                .foregroundColor(.secondary)
        } else if sources.sources.isEmpty {
            Text("library.source.add_hint").foregroundColor(.secondary)
        } else {
            Text(String(format: FlyNesLocalizedString("library.game_count"), snapshot.games.count))
                .foregroundColor(.secondary)
        }
    }

    private func icon(_ image: String, _ label: LocalizedStringKey, _ id: String,
                      action: @escaping () -> Void) -> some View {
        Button(action: action) { Image(systemName: image).frame(width: 48, height: 48) }
            .accessibilityLabel(Text(label)).accessibilityIdentifier(id)
    }

    /// Android resolves and commits the selected game before leaving the Game
    /// Center; a failure is reported in the status line with the grid still visible.
    private func launch(_ game: CatalogGame) {
        sources.launch(canonicalID: game.id, title: game.titlePrimary) { rom in
            path.append(LibraryRoute.run(canonicalId: game.id, rom: rom, game: game))
        }
    }

    private func reloadSnapshot() {
        cachedRows = FlyNesAppBridge.sharedInstance().gameCenterFilteredGames(
            forCategory: LibraryFilter(rawValue: category)?.rawValue ?? "ALL", query: searchText)
        reprojectTitles()
        covers.preload(canonicalIds: snapshot.games.map(\.id))
    }

    private func reprojectTitles() {
        let games = cachedRows.compactMap { CatalogGameFactory.game(from: $0, localeIdentifier: locale.identifier) }
        snapshot = CatalogSnapshot(generation: snapshot.generation &+ 1, games: games)
        if !games.contains(where: { $0.id == selectedID }) { selectedID = games.first?.id ?? "" }
    }
}
