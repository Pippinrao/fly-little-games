import SwiftUI

@main
struct FlyNESApp: App {
    var body: some Scene {
        WindowGroup {
            TabView {
                CatalogLibraryView()
                    .tabItem {
                        Label("library.title", systemImage: "square.stack")
                    }
                SettingsView()
                    .tabItem {
                        Label("settings.title", systemImage: "gearshape")
                    }
            }
        }
    }
}
