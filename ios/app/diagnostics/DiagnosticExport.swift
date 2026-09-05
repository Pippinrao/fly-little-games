import Foundation
import UIKit

/// Diagnostic export for qualification. Advanced Metal modes stay locked until
/// an evidence tool records a matching device profile.
struct DiagnosticExport: Codable {
    var device: String
    var system: String
    var gpuFamily: String
    var displayCadenceHz: Double
    var buildHash: String
    var algorithmHash: String
    var gpuTimeMs: Double
    var audioUnderrun: UInt64
    var frameGapNs: UInt64
    var thermalState: String
    var advancedModesLocked: Bool

    static func capture(algorithmHash: String, buildHash: String, gpuTimeMs: Double,
                        audioUnderrun: UInt64, frameGapNs: UInt64) -> DiagnosticExport {
        let process = ProcessInfo.processInfo
        let thermal: String
        switch process.thermalState {
        case .nominal: thermal = "nominal"
        case .fair: thermal = "fair"
        case .serious: thermal = "serious"
        case .critical: thermal = "critical"
        @unknown default: thermal = "unknown"
        }
        return DiagnosticExport(
            device: UIDevice.current.model,
            system: process.operatingSystemVersionString,
            gpuFamily: "metal",
            displayCadenceHz: 0,
            buildHash: buildHash,
            algorithmHash: algorithmHash,
            gpuTimeMs: gpuTimeMs,
            audioUnderrun: audioUnderrun,
            frameGapNs: frameGapNs,
            thermalState: thermal,
            advancedModesLocked: true
        )
    }

    func jsonData() throws -> Data {
        try JSONEncoder().encode(self)
    }
}

enum DeviceQualification {
    static func advancedModesUnlocked(matchingProfile: DiagnosticExport?) -> Bool {
        guard let matchingProfile, !matchingProfile.advancedModesLocked else {
            return false
        }
        return matchingProfile.displayCadenceHz > 0
    }
}
