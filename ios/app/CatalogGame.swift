import Foundation

/// One catalog row copied from `fly_catalog_snapshot_get`. Identity is the
/// portable canonical id; no bookmark, URI, or file descriptor is stored.
struct CatalogGame: Identifiable, Equatable, Hashable {
    var id: String { canonicalId }

    let canonicalId: String
    let displayName: String
    let compatibilityState: UInt32
    let freshness: UInt32
    let sourceScope: UInt32
    let favorite: UInt32
    let lastPlayedSequence: UInt64
}

/// Immutable generation published by `fly_catalog_snapshot`.
struct CatalogSnapshot: Equatable {
    let generation: UInt64
    let games: [CatalogGame]
}
