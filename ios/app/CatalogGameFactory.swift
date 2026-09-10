import Foundation

/// Builds catalog rows from bridge dictionaries, applying Android title
/// presentation for the active interface language.
enum CatalogGameFactory {
    static func game(from row: [String: Any], localeIdentifier: String) -> CatalogGame? {
        guard let id = row["canonicalId"] as? String else { return nil }
        let filename = row["displayName"] as? String ?? ""
        let fields: [String: Any] = [
            "titleEn": row["titleEn"] as? String ?? "",
            "titleZhHans": row["titleZhHans"] as? String ?? "",
            "titleUnknown": row["titleUnknown"] as? String ?? "",
        ]
        let title = FlyNesCatalogPresentation.title(forFields: fields, locale: localeIdentifier)
        return CatalogGame(
            canonicalId: id,
            displayName: filename,
            titlePrimary: title["primary"] ?? filename,
            titleSecondary: title["secondary"] ?? "",
            searchAliases: row["searchAliases"] as? String ?? filename,
            builtin: (row["builtin"] as? NSNumber)?.boolValue ?? false,
            compatibilityState: (row["compatibilityState"] as? NSNumber)?.uint32Value ?? 0,
            freshness: (row["freshness"] as? NSNumber)?.uint32Value ?? 0,
            sourceScope: (row["sourceScope"] as? NSNumber)?.uint32Value ?? 0,
            favorite: (row["favorite"] as? NSNumber)?.uint32Value ?? 0,
            lastPlayedSequence: (row["lastPlayedSequence"] as? NSNumber)?.uint64Value ?? 0,
            titleEn: fields["titleEn"] as? String ?? "",
            titleZhHans: fields["titleZhHans"] as? String ?? "",
            titleUnknown: fields["titleUnknown"] as? String ?? "")
    }
}
