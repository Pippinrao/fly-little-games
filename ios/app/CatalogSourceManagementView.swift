import SwiftUI

/// Source-management placeholder. Security-scoped bookmarks and document
/// picking live in `ios/app/platform/`; this view never stores locators.
struct CatalogSourceManagementView: View {
    var body: some View {
        List {
            Section {
                Text("library.sources.placeholder")
            }
        }
        .navigationTitle("library.sources")
    }
}
