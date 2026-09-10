import SwiftUI

struct CatalogGameDetailView: View {
    let game: CatalogGame
    @ObservedObject private var sources = CatalogSourceModel.shared
    @State private var favorite = false
    @State private var compatibility: UInt32 = 0
    @State private var freshness: UInt32 = 0

    var body: some View {
        List {
            Section {
                Text(game.displayName)
                    .font(.title2)
                Button {
                    let newValue = !favorite
                    sources.perform {
                        try FlyNesAppBridge.sharedInstance().setFavorite(newValue, canonicalID: game.canonicalId)
                    }
                } label: {
                    Label(favorite ? "library.favorite.remove" : "library.favorite.add",
                          systemImage: favorite ? "star.fill" : "star")
                }
                .disabled(sources.busy)
            }
            Section {
                NavigationLink(value: LibraryRoute.run(game.canonicalId)) {
                    Label("library.play", systemImage: "play.fill")
                }
                .disabled(compatibility != 1 || freshness != 1 || sources.busy)
                if compatibility != 1 {
                    Text("library.source.game_unsupported").font(.footnote).foregroundStyle(.secondary)
                } else if freshness != 1 {
                    Text("library.source.game_stale").font(.footnote).foregroundStyle(.secondary)
                }
                NavigationLink("library.sources") { CatalogSourceManagementView() }
            }
            if let error = sources.error { Section { Text(error).foregroundStyle(.red) } }
        }
        .navigationTitle("library.detail")
        .onAppear(perform: refresh)
        .onReceive(sources.$generation) { _ in refresh() }
    }

    private func refresh() {
        let row = FlyNesAppBridge.sharedInstance().gameCenterFilteredGames(forCategory: "ALL", query: "").first {
            ($0["canonicalId"] as? String) == game.canonicalId
        }
        favorite = (row?["favorite"] as? NSNumber)?.boolValue ?? false
        compatibility = (row?["compatibilityState"] as? NSNumber)?.uint32Value ?? 0
        freshness = (row?["freshness"] as? NSNumber)?.uint32Value ?? 0
    }
}
