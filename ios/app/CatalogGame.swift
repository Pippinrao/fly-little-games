import Foundation

/// One catalog row copied from `fly_catalog_snapshot_get`. Identity is the
/// portable canonical id; no bookmark, URI, or file descriptor is stored.
struct CatalogGame: Identifiable, Equatable, Hashable {
    var id: String { canonicalId }

    let canonicalId: String
    /// Outer filename, kept for the shared search path and diagnostics.
    let displayName: String
    /// Locale primary title. Falls back to an unclassified-script title and then to
    /// the untranslated filename, exactly like Android `GameTitlePresentation`.
    let titlePrimary: String
    /// The other language when it differs, so a bilingual card can show both.
    let titleSecondary: String
    /// Every filename and title form accepted by alias search.
    let searchAliases: String
    let builtin: Bool
    let compatibilityState: UInt32
    let freshness: UInt32
    let sourceScope: UInt32
    let favorite: UInt32
    let lastPlayedSequence: UInt64

    /// Both language fields are retained so a new interface language can be applied
    /// without refetching the catalog row.
    let titleEn: String
    let titleZhHans: String
    let titleUnknown: String
}

/// Immutable generation published by `fly_catalog_snapshot`.
struct CatalogSnapshot: Equatable {
    let generation: UInt64
    let games: [CatalogGame]
}
