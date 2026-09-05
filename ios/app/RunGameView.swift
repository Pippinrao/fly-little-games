import SwiftUI

/// Hosts the UIKit run surface so landscape safe areas and multi-touch stay in UIKit.
struct RunGameView: UIViewControllerRepresentable {
    let canonicalId: String
    var onPauseCommand: (String) -> Void = { _ in }
    var overlayReloadGeneration: Int = 0

    func makeCoordinator() -> Coordinator {
        Coordinator(onPauseCommand: onPauseCommand)
    }

    func makeUIViewController(context: Context) -> RunSurfaceViewController {
        let controller = RunSurfaceViewController()
        controller.canonicalId = canonicalId
        controller.onPauseCommand = { command in
            context.coordinator.onPauseCommand(command)
        }
        return controller
    }

    func updateUIViewController(_ uiViewController: RunSurfaceViewController, context: Context) {
        uiViewController.canonicalId = canonicalId
        context.coordinator.onPauseCommand = onPauseCommand
        uiViewController.onPauseCommand = { command in
            context.coordinator.onPauseCommand(command)
        }
        if context.coordinator.overlayReloadGeneration != overlayReloadGeneration {
            context.coordinator.overlayReloadGeneration = overlayReloadGeneration
            uiViewController.reloadProductSettings()
        }
    }

    final class Coordinator {
        var onPauseCommand: (String) -> Void
        var overlayReloadGeneration = 0
        init(onPauseCommand: @escaping (String) -> Void) {
            self.onPauseCommand = onPauseCommand
        }
    }
}

/// Pause drawer Resume / Game Center / Settings. Game Center pops to the library root.
struct RunGameContainer: View {
    let canonicalId: String
    @Binding var path: NavigationPath
    @State private var showSettings = false
    @State private var overlayReloadGeneration = 0

    var body: some View {
        RunGameView(
            canonicalId: canonicalId,
            overlayReloadGeneration: overlayReloadGeneration
        ) { command in
            if command == "game_center" {
                path = NavigationPath()
            } else if command == "settings" {
                showSettings = true
            }
        }
        .ignoresSafeArea()
        .navigationBarBackButtonHidden(true)
        .toolbar(.hidden, for: .navigationBar)
        .sheet(isPresented: $showSettings, onDismiss: {
            overlayReloadGeneration += 1
        }) {
            SettingsView()
        }
    }
}
