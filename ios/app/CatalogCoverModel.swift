import SwiftUI

/// Async cover lookup for the game center. Covers are captured from the game's own
/// native frames by `CoverCaptureSession`, so a card shows the title placeholder
/// until a frame passes the Android quality gate.
@MainActor
final class GameCoverModel: ObservableObject {
    static let shared = GameCoverModel()

    @Published private(set) var covers: [String: UIImage] = [:]

    private var observer: NSObjectProtocol?

    init() {
        observer = NotificationCenter.default.addObserver(
            forName: Notification.Name.FlyNesCoverStoreDidChange, object: nil, queue: .main
        ) { note in
            guard let id = note.userInfo?[FlyNesCoverStoreCanonicalIdKey] as? String else { return }
            Task { @MainActor in GameCoverModel.shared.refresh(canonicalId: id) }
        }
    }

    deinit {
        if let observer { NotificationCenter.default.removeObserver(observer) }
    }

    func image(for canonicalId: String) -> UIImage? { covers[canonicalId] }

    /// Loads every missing cover off the main thread; already-loaded covers are kept.
    func preload(canonicalIds: [String]) {
        let missing = canonicalIds.filter { covers[$0] == nil }
        guard !missing.isEmpty else { return }
        Task.detached(priority: .utility) {
            let loaded = GameCoverModel.readCovers(missing)
            guard !loaded.isEmpty else { return }
            await MainActor.run {
                for (id, image) in loaded where GameCoverModel.shared.covers[id] == nil {
                    GameCoverModel.shared.covers[id] = image
                }
            }
        }
    }

    func refresh(canonicalId: String) {
        guard covers[canonicalId] == nil else { return }
        Task.detached(priority: .utility) {
            guard let image = FlyNesCoverStore.sharedInstance().cover(forCanonicalId: canonicalId) else { return }
            await MainActor.run { GameCoverModel.shared.covers[canonicalId] = image }
        }
    }

    /// The store is internally synchronized, so covers can be decoded off the main
    /// thread without touching actor-isolated state.
    nonisolated static func readCovers(_ canonicalIds: [String]) -> [String: UIImage] {
        var loaded: [String: UIImage] = [:]
        for id in canonicalIds {
            if let image = FlyNesCoverStore.sharedInstance().cover(forCanonicalId: id) {
                loaded[id] = image
            }
        }
        return loaded
    }
}
