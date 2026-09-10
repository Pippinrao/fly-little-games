import SwiftUI

/// Hosts the UIKit run surface so landscape safe areas and multi-touch stay in UIKit.
struct RunGameView: UIViewControllerRepresentable {
    let canonicalId: String
    let romData: Data
    var onPauseCommand: (String) -> Void = { _ in }
    var overlayReloadGeneration: Int = 0

    func makeCoordinator() -> Coordinator {
        Coordinator(onPauseCommand: onPauseCommand)
    }

    func makeUIViewController(context: Context) -> RunSurfaceViewController {
        let controller = RunSurfaceViewController()
        controller.canonicalId = canonicalId
        controller.romData = romData
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
    @State private var romData: Data?
    @State private var loadError: String?
    @State private var loading = false

    var body: some View {
        Group {
            if let romData = romData {
                RunGameView(canonicalId: canonicalId, romData: romData,
                            onPauseCommand: { command in
                    if command == "game_center" { path = NavigationPath() }
                    else if command == "settings" { showSettings = true }
                }, overlayReloadGeneration: overlayReloadGeneration)
                .ignoresSafeArea()
            } else if let error = loadError {
                VStack(spacing: 16) {
                    Image(systemName: "exclamationmark.triangle").font(.largeTitle)
                    Text(error).multilineTextAlignment(.center)
                    Button("library.source.retry", action: loadROM)
                    Button("game_center.title") { path = NavigationPath() }
                }.padding()
            } else {
                ProgressView("library.source.loading_game")
            }
        }
        .navigationBarBackButtonHidden(true)
        .toolbar(.hidden, for: .navigationBar)
        .onAppear { if romData == nil && !loading { loadROM() } }
        .fullScreenCover(isPresented: $showSettings, onDismiss: {
            overlayReloadGeneration += 1
        }) {
            SettingsView()
        }
    }

    private func loadROM() {
        loading = true
        loadError = nil
        CatalogSourceModel.shared.prepareROM(canonicalId) { result in
            loading = false
            switch result {
            case .success(let data): romData = data
            case .failure(let error): loadError = error.localizedDescription
            }
        }
    }
}
