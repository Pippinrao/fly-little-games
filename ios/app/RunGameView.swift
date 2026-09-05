import SwiftUI

/// Hosts the UIKit run surface so landscape safe areas and multi-touch stay in UIKit.
struct RunGameView: UIViewControllerRepresentable {
    let canonicalId: String

    func makeUIViewController(context: Context) -> RunSurfaceViewController {
        let controller = RunSurfaceViewController()
        controller.canonicalId = canonicalId
        return controller
    }

    func updateUIViewController(_ uiViewController: RunSurfaceViewController, context: Context) {
        uiViewController.canonicalId = canonicalId
    }
}
