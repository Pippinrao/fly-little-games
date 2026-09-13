import SwiftUI

/// 配对 — the 6-digit-code block and the Wi-Fi-path block, each capability
/// rendered per spec §4.
///
/// This page reads no session value: `FlyNesAppBridge` has no nearby/session
/// method yet (spec §3), so its controls are shown disabled with a visible
/// reason instead of pretending to act.
///
/// The anonymous-join control set (`nearby.join.request_anonymous` /
/// `nearby.join.accept` / `nearby.join.reject`) is deliberately **not built**:
/// spec §4 flags it as over-delivery against the approved design and keeps the
/// keys only until the §30 centralised review answers. The keys exist in both
/// locale files; no control is invented here.
struct NearbyPairingView: View {
    var body: some View {
        List {
            stagesSection
            codeSection
            wifiSection
        }
        .navigationTitle("nearby.pairing.title")
        .navigationBarTitleDisplayMode(.inline)
    }

    /// The seven pairing stages in pipeline order; earlier stages are marked
    /// passed, only the first failing stage carries a reason, later stages stay
    /// neutral with no blocked key and no reason (spec §2.1, §4, §10 D5).
    @ViewBuilder private var stagesSection: some View {
        Section {
            ForEach(PairingStage.allCases) { stage in
                PairingStageRow(stage: stage)
            }
        }
    }

    /// 6-digit code (design §22.2 BLE path): the code label and the confirm
    /// control. No entry surface is offered at all — nothing local can receive
    /// a code, and spec §4 forbids a control that cannot act, so a text field
    /// that would only collect a dead value is not built.
    @ViewBuilder private var codeSection: some View {
        Section("nearby.code.label") {
            DisabledActionRow(
                titleKey: "nearby.code.confirm",
                reasonKey: "nearby.blocked.auth",
                identifier: "nearby_code_confirm"
            )
        }
    }

    /// Wi-Fi path (design §22.2): `nearby.wifi.path_building` and the one-time
    /// system confirmation, neither able to act. The path's own stages are not
    /// re-listed here: they are the same seven pipeline stages rendered above,
    /// and rendering them twice with two different markings is the one thing
    /// spec §4 says the stage list must never look like. The path sits after
    /// 权限 and 发现, so while those are the current stage the whole path is
    /// unreached — which the reason states instead (spec §2.1, §10 D5).
    @ViewBuilder private var wifiSection: some View {
        Section("nearby.wifi.path_building") {
            DisabledActionRow(
                titleKey: "nearby.wifi.system_confirm_once",
                reasonKey: "nearby.blocked.wifi",
                identifier: "nearby_wifi_system_confirm"
            )
            Text("nearby.pairing.wifi.blocked")
                .font(.footnote)
                .foregroundStyle(.secondary)
        }
    }
}

/// A control that cannot act is shown disabled with its visible reason, never
/// as a fake action (spec §4). The identifier is on the control and its reason
/// carries the `_reason` suffix, matching the Android resource ids.
private struct DisabledActionRow: View {
    let titleKey: LocalizedStringKey
    let reasonKey: LocalizedStringKey
    let identifier: String

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Button(titleKey) {}
                .disabled(true)
                .accessibilityIdentifier(identifier)
            Text(reasonKey)
                .font(.footnote)
                .foregroundStyle(.secondary)
                .accessibilityIdentifier(identifier + "_reason")
        }
        .padding(.vertical, 2)
    }
}

/// The seven pairing stages in the design's pipeline order; display order is
/// pipeline order (spec §2.1, §10 D5). Same vocabulary as the 附近设备 tab —
/// file-private there, so it is restated here rather than shared through a
/// leading underscore type; the spec, not this file, is the source of truth.
private enum PairingStage: String, CaseIterable, Identifiable {
    case permission
    case discovery
    case auth
    case wifi
    case quic
    case version
    case codec

    /// Until the session ABI reports the current stage, the page marks the
    /// permission stage. iOS cannot query a permission it never declared, so
    /// its reason is the iOS-specific static key (spec §2.1, §10 D1).
    static let currentStage: PairingStage = .permission

    var id: String { rawValue }

    var titleKey: LocalizedStringKey {
        switch self {
        case .permission: return "nearby.stage.permission"
        case .discovery: return "nearby.stage.discovery"
        case .auth: return "nearby.stage.auth"
        case .wifi: return "nearby.stage.wifi"
        case .quic: return "nearby.stage.quic"
        case .version: return "nearby.stage.version"
        case .codec: return "nearby.stage.codec"
        }
    }

    /// Only the first failing stage renders a reason, and only for a stage
    /// whose reason is knowable with no session and no declared permission
    /// (spec §10 D1, D11). Later stages get no blocked key and no reason.
    var failingReasonKey: LocalizedStringKey? {
        switch self {
        case .permission: return "nearby.stage.permission.reason_ios"
        default: return nil
        }
    }

    var pipelineIndex: Int {
        PairingStage.allCases.firstIndex(of: self) ?? 0
    }
}

/// One pipeline stage: earlier stages passed, the first failing stage marked
/// with its reason, later stages neutral with no blocked key and no reason —
/// one list must not have two contradictory renderings (spec §4, §10 D5).
private struct PairingStageRow: View {
    let stage: PairingStage

    private enum StageState: Equatable {
        case passed
        case failing
        case notReached

        /// Announced state of a pipeline row. The glyphs stay visible, but a
        /// colour alone is not a state and VoiceOver must hear it
        /// (spec §2.3 Table K `nearby.stage.status.*`).
        var statusKey: LocalizedStringKey {
            switch self {
            case .passed: return "nearby.stage.status.passed"
            case .failing: return "nearby.stage.status.current"
            case .notReached: return "nearby.stage.status.not_reached"
            }
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(alignment: .firstTextBaseline, spacing: 8) {
                stageIcon
                Text(stage.titleKey)
                    .fontWeight(state == .failing ? .semibold : .regular)
                    .foregroundStyle(titleStyle)
            }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(Text(stage.titleKey))
            .accessibilityValue(Text(state.statusKey))
            .accessibilityIdentifier("nearby_stage_" + stage.rawValue)
            if state == .failing, let reasonKey = stage.failingReasonKey {
                Text(reasonKey)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, 2)
    }

    private var state: StageState {
        if stage == PairingStage.currentStage {
            return .failing
        }
        return stage.pipelineIndex < PairingStage.currentStage.pipelineIndex
            ? .passed
            : .notReached
    }

    private var titleStyle: HierarchicalShapeStyle {
        switch state {
        case .passed: return .secondary
        case .failing: return .primary
        case .notReached: return .tertiary
        }
    }

    @ViewBuilder private var stageIcon: some View {
        switch state {
        case .passed:
            Image(systemName: "checkmark.circle")
                .foregroundStyle(.secondary)
        case .failing:
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundStyle(.orange)
        case .notReached:
            Image(systemName: "circle")
                .foregroundStyle(.tertiary)
        }
    }
}
