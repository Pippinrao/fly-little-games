import SwiftUI

struct CatalogGameDetailView: View {
    let game: CatalogGame?
    var largeText = false
    @ObservedObject private var sources = CatalogSourceModel.shared

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            if !largeText {
                ZStack {
                    RoundedRectangle(cornerRadius: 8).fill(Color(uiColor: .tertiarySystemFill))
                    Text(game?.displayName ?? "FlyNES")
                        .font(.headline).multilineTextAlignment(.center).padding(16)
                }.frame(maxWidth: .infinity, maxHeight: .infinity)
                    .accessibilityHidden(true)
            } else { Spacer(minLength: 0) }
            HStack(spacing: 4) {
                if let game = game {
                    Text(game.displayName).font(.title3.bold()).lineLimit(largeText ? 2 : 1)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .accessibilityAddTraits(.isHeader)
                } else {
                    Text("library.no_games").font(.headline).frame(maxWidth: .infinity, alignment: .leading)
                }
                Button {
                    guard let game = game else { return }
                    sources.perform {
                        try FlyNesAppBridge.sharedInstance().setFavorite(game.favorite == 0, canonicalID: game.id)
                    }
                } label: {
                    Image(systemName: game?.favorite == 1 ? "star.fill" : "star")
                        .frame(width: 48, height: 48)
                }
                .accessibilityLabel(Text(game?.favorite == 1 ? "library.favorite.remove" : "library.favorite.add"))
                .accessibilityIdentifier("favorite_toggle")
                .disabled(game == nil || sources.busy)
            }
            if !largeText, let game = game {
                if game.compatibilityState != 1 {
                    Text("library.source.game_unsupported").font(.footnote).foregroundColor(.secondary).lineLimit(2)
                } else if game.freshness != 1 {
                    Text("library.source.game_stale").font(.footnote).foregroundColor(.secondary).lineLimit(2)
                } else if game.sourceScope == 1 {
                    Text("game_center.builtin").font(.footnote).foregroundColor(.secondary)
                }
            }
            NavigationLink(value: LibraryRoute.run(game?.id ?? "")) {
                Label((game?.lastPlayedSequence ?? 0) > 0 ? "library.continue" : "library.play",
                      systemImage: "play.fill")
                    .frame(maxWidth: .infinity, minHeight: largeText ? 88 : 56)
            }
            .buttonStyle(.borderedProminent)
            .accessibilityIdentifier("launch_selected")
            .disabled(game == nil || game?.compatibilityState != 1 || game?.freshness != 1 || sources.busy)
        }
        .padding(16)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(uiColor: .secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 20))
    }
}
