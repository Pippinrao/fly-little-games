import SwiftUI

/// Hosts the UIKit run surface so landscape safe areas and multi-touch stay in UIKit.
struct RunGameView: UIViewControllerRepresentable {
    let canonicalId: String
    var onPauseCommand: (String) -> Void = { _ in }

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
    }

    final class Coordinator {
        var onPauseCommand: (String) -> Void
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

    var body: some View {
        RunGameView(canonicalId: canonicalId) { command in
            if command == "game_center" {
                path = NavigationPath()
            } else if command == "settings" {
                showSettings = true
            }
        }
        .ignoresSafeArea()
        .navigationBarBackButtonHidden(true)
        .toolbar(.hidden, for: .navigationBar)
        .sheet(isPresented: $showSettings) {
            SettingsView()
        }
    }
}
