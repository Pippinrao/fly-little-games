import SwiftUI

/// 配对 — the 6-digit-code block and the Wi-Fi-path block, each capability
/// rendered per spec §4.
///
/// Invitation lifecycle and join attempts are owned by the shared session
/// route through `FlyNesAppBridge`; unsupported platform transport still
/// reports the discovery reason and never fabricates a match.
///
/// The anonymous-join control set (`nearby.join.request_anonymous` /
/// `nearby.join.accept` / `nearby.join.reject`) is deliberately **not built**:
/// spec §4 flags it as over-delivery against the approved design and keeps the
/// keys only until the §30 centralised review answers. The keys exist in both
/// locale files; no control is invented here.
/// Entry mode of the approved design (N01/N02/N03).
enum NearbyPairingMode: String {
    case create
    case joinCode
    case scan
}

/// Joiner-side input rule (UI contract nearby_ui_v1, shared
/// parse_invite_code): exactly six ASCII digits after trimming ASCII
/// whitespace, leading zeros preserved, no silent truncation (C05).
func normalizeInviteCode(_ raw: String) -> String? {
    let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
    guard trimmed.count == 6 else { return nil }
    return trimmed.allSatisfy { $0.isASCII && $0.isNumber } ? trimmed : nil
}

struct NearbyPairingView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var mode: NearbyPairingMode
    // N01 invite lifecycle: generation-bound code with the 60s continuous
    // clock; regeneration and cancellation kill the old generation (C16).
    @State private var inviteCode = ""
    @State private var inviteGeneration: UInt64 = 0
    @State private var inviteDeadlineNanoseconds: UInt64 = 0
    @State private var inviteRemainingSeconds = 0
    // N02 join form state: error only after a submit attempt, submit locked
    // while a request is in flight (C05/C16).
    @State private var joinInput = ""
    @State private var joinError = false
    @State private var joinSubmitted = false
    @State private var joinFailed = false
    @State private var nextJoinAttemptID: UInt64 = 1
    private let ticker = Timer.publish(every: 0.25, on: .main, in: .common).autoconnect()
    private let bridge = FlyNesAppBridge.sharedInstance()

    init(mode: NearbyPairingMode = .create) {
        _mode = State(initialValue: mode)
    }

    var body: some View {
        GeometryReader { geometry in
            let wide = geometry.size.width > 580
            VStack(spacing: 0) {
                ScrollView {
                    if wide {
                        HStack(alignment: .top, spacing: 18) {
                            leftPane.frame(width: 224, alignment: .topLeading)
                            rightPane
                        }
                        .padding(16)
                    } else {
                        VStack(alignment: .leading, spacing: 18) {
                            leftPane
                            rightPane
                        }
                        .padding(16)
                    }
                }
                footer
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .background(pageBackground.ignoresSafeArea())
        }
        .navigationTitle(titleKey)
        .navigationBarTitleDisplayMode(.inline)
        .toolbar(.visible, for: .navigationBar)
        .onAppear {
            if mode == .create && inviteCode.isEmpty {
                let snapshot = bridge.nearbyInviteSnapshot()
                let activeGeneration = snapshot["hostGeneration"]?.uint64Value ?? 0
                if (snapshot["hostPhase"]?.uint32Value ?? 0) == 1 && activeGeneration != 0 {
                    _ = bridge.nearbyHostCancelGeneration(activeGeneration)
                }
                publishInvite(regenerating: false)
            }
        }
        .onDisappear { cancelOwnedRouteState() }
        .onReceive(ticker) { _ in refreshInvite() }
    }

    private var pageBackground: Color {
        Color(red: 18 / 255, green: 19 / 255, blue: 22 / 255)
    }
    private var surface: Color {
        Color(red: 27 / 255, green: 29 / 255, blue: 34 / 255)
    }
    private var onSurface: Color {
        Color(red: 244 / 255, green: 239 / 255, blue: 230 / 255)
    }
    private var muted: Color {
        Color(red: 190 / 255, green: 184 / 255, blue: 174 / 255)
    }
    private var primary: Color {
        Color(red: 255 / 255, green: 107 / 255, blue: 94 / 255)
    }

    private var titleKey: LocalizedStringKey {
        switch mode {
        case .joinCode: return "nearby.screen.joinCode"
        case .scan: return "nearby.screen.scan"
        case .create: return "nearby.screen.invite"
        }
    }

    @ViewBuilder private var leftPane: some View {
        switch mode {
        case .create: inviteLeft
        case .joinCode: joinLeft
        case .scan: scanLeft
        }
    }

    @ViewBuilder private var rightPane: some View {
        switch mode {
        case .create: inviteRight
        case .joinCode: joinRight
        case .scan: scanRight
        }
    }

    @ViewBuilder private var inviteLeft: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("nearby.invite.kicker")
                .nearbyRole(NearbyTypography.kicker)
                .foregroundStyle(primary)
            Text("nearby.invite.headline")
                .nearbyRole(NearbyTypography.paneTitle)
                .foregroundStyle(onSurface)
                .accessibilityIdentifier("nearby_invite_headline")
            Text("nearby.invite.subtitle")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
                .accessibilityIdentifier("nearby_invite_subtitle")
            Text("nearby.invite.codeLabel")
                .nearbyRole(NearbyTypography.body)
                .foregroundStyle(onSurface)
            Text(inviteCode.isEmpty ? "· · · · · ·" : inviteCode)
                .nearbyRole(NearbyTypography.inviteCode)
                .foregroundStyle(onSurface)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 10)
                .background(Color(red: 41 / 255, green: 43 / 255, blue: 49 / 255))
                .clipShape(RoundedRectangle(cornerRadius: 10))
                .accessibilityIdentifier("nearby_invite_code_value")
            if !inviteCode.isEmpty {
                Text("\(NSLocalizedString("nearby.invite.validFor", comment: "")) \(inviteRemainingSeconds)s")
                    .nearbyRole(NearbyTypography.muted)
                    .foregroundStyle(muted)
            }
            Button("nearby.action.regenerate") {
                publishInvite(regenerating: true)
            }
            .nearbyRole(NearbyTypography.action)
            .frame(maxWidth: .infinity)
            .nearbyMinTap()
            .accessibilityIdentifier("nearby_invite_regenerate")
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    @ViewBuilder private var inviteRight: some View {
        VStack(spacing: 10) {
            ZStack {
                Color.white
                Text("nearby.invite.qrLabel")
                    .nearbyRole(NearbyTypography.muted)
                    .foregroundStyle(muted)
            }
            .frame(width: 170, height: 170)
            Text("nearby.invite.qrHint")
                .nearbyRole(NearbyTypography.sectionTitle)
                .foregroundStyle(onSurface)
            Text("nearby.invite.hostMustAccept")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity)
    }

    @ViewBuilder private var joinLeft: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("nearby.join.kicker")
                .nearbyRole(NearbyTypography.kicker)
                .foregroundStyle(primary)
            Text("nearby.join.headline")
                .nearbyRole(NearbyTypography.paneTitle)
                .foregroundStyle(onSurface)
            Text("nearby.join.subtitle")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
            Button("nearby.action.switchToScan") {
                mode = .scan
            }
            .nearbyRole(NearbyTypography.action)
            .frame(maxWidth: .infinity)
            .nearbyMinTap()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    @ViewBuilder private var joinRight: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("nearby.join.codeLabel")
                .nearbyRole(NearbyTypography.body)
                .foregroundStyle(onSurface)
            TextField("nearby.join.placeholder", text: $joinInput)
                .keyboardType(.numberPad)
                .multilineTextAlignment(.center)
                .nearbyRole(NearbyTypography.codeInput)
                .frame(minHeight: 60)
                .disabled(joinSubmitted)
                .accessibilityIdentifier("nearby_join_code_input")
                .onChange(of: joinInput) { _ in
                    if !joinSubmitted { joinError = false }
                }
            if joinError {
                Text((joinSubmitted || joinFailed)
                     ? "nearby.stage.discovery.reason"
                     : "nearby.reason.code.invalidFormat")
                    .nearbyRole(NearbyTypography.body)
                    .foregroundStyle(Color(red: 231 / 255, green: 183 / 255, blue: 117 / 255))
                    .accessibilityIdentifier("nearby_join_code_error")
            }
            Text("nearby.join.hint")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    @ViewBuilder private var scanLeft: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("nearby.scan.kicker")
                .nearbyRole(NearbyTypography.kicker)
                .foregroundStyle(primary)
            Text("nearby.scan.headline")
                .nearbyRole(NearbyTypography.paneTitle)
                .foregroundStyle(onSurface)
            Text("nearby.scan.subtitle")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
            Button("nearby.action.switchToJoinCode") {
                mode = .joinCode
            }
            .nearbyRole(NearbyTypography.action)
            .frame(maxWidth: .infinity)
            .nearbyMinTap()
            Text("nearby.scan.cameraNote")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    @ViewBuilder private var scanRight: some View {
        VStack(alignment: .leading, spacing: 10) {
            VStack(spacing: 14) {
                Text("nearby.scan.cameraHint")
                    .nearbyRole(NearbyTypography.sectionTitle)
                    .foregroundStyle(onSurface)
                    .multilineTextAlignment(.center)
            }
            .frame(maxWidth: .infinity, minHeight: 206)
            .padding(16)
            .background(surface)
            .overlay(
                RoundedRectangle(cornerRadius: 16)
                    .stroke(style: StrokeStyle(lineWidth: 2, dash: [6]))
                    .foregroundStyle(muted)
            )
            Text("nearby.scan.afterScan")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    @ViewBuilder private var footer: some View {
        ViewThatFits(in: .horizontal) {
            HStack(spacing: 10) {
                footerCopyView
                footerButton
            }
            VStack(alignment: .leading, spacing: 10) {
                footerCopyView
                footerButton
            }
        }
        .padding(.horizontal, 16)
        .frame(minHeight: 58)
        .background(pageBackground)
    }

    private var footerCopyView: some View {
        Text(footerCopy)
            .nearbyRole(NearbyTypography.muted)
            .foregroundStyle(muted)
            .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var footerButton: some View {
        HStack(spacing: 10) {
            if mode == .joinCode && (joinSubmitted || joinFailed) {
                Button(action: cancelJoinAttempt) {
                    Text("nearby.action.cancelRequest")
                        .nearbyRole(NearbyTypography.action)
                        .frame(minWidth: 120)
                        .nearbyMinTap()
                }
                .accessibilityIdentifier("nearby_join_cancel")
            }
            Button(action: footerAction) {
                Text(footerActionLabel)
                    .nearbyRole(mode == .joinCode ? NearbyTypography.primaryAction : NearbyTypography.action)
                    .frame(minWidth: 160)
                    .nearbyMinTap()
            }
            .disabled(joinSubmitDisabled)
            .accessibilityIdentifier(footerIdentifier)
        }
    }

    private var joinSubmitDisabled: Bool {
        guard mode == .joinCode else { return false }
        return joinSubmitted || normalizeInviteCode(joinInput) == nil
    }

    private var footerCopy: LocalizedStringKey {
        switch mode {
        case .joinCode: return "nearby.join.footer"
        case .scan: return "nearby.scan.footer"
        case .create: return "nearby.invite.footer"
        }
    }

    private var footerActionLabel: LocalizedStringKey {
        switch mode {
        case .joinCode: return "nearby.action.submitJoinCode"
        case .scan: return "nearby.action.cancel"
        case .create: return "nearby.action.cancelInvite"
        }
    }

    private var footerIdentifier: String {
        switch mode {
        case .joinCode: return "nearby_join_submit"
        case .scan: return "nearby_action_cancel"
        case .create: return "nearby_invite_cancel"
        }
    }

    private func footerAction() {
        switch mode {
        case .joinCode:
            submitJoin()
        case .scan:
            dismiss()
        case .create:
            if inviteGeneration != 0 {
                _ = bridge.nearbyHostCancelGeneration(inviteGeneration)
                clearInvite()
            }
            dismiss()
        }
    }

    private func submitJoin() {
        if joinSubmitted { return }
        if normalizeInviteCode(joinInput) == nil {
            joinError = true
            return
        }
        let attemptID = bridge.nearbyNextJoinAttemptID()
        guard attemptID != 0 else { joinError = true; return }
        joinSubmitted = true
        joinFailed = false
        nextJoinAttemptID = attemptID &+ 1
        _ = bridge.nearbySubmitCode(
            joinInput,
            attemptID: attemptID,
            nowNanoseconds: monotonicNanoseconds()
        )
        joinError = true
        DispatchQueue.main.async {
            joinSubmitted = false
            joinFailed = true
        }
    }

    private func cancelJoinAttempt() {
        if nextJoinAttemptID > 1 {
            _ = bridge.nearbyCancelAttempt(nextJoinAttemptID - 1)
        }
        joinSubmitted = false
        joinFailed = false
        dismiss()
    }

    private func publishInvite(regenerating: Bool) {
        let code = secureInviteCode()
        let generation = bridge.nearbyNextHostGeneration()
        guard generation != 0 else { clearInvite(); return }
        let now = monotonicNanoseconds()
        let snapshot = bridge.nearbyInviteSnapshot()
        let hostIsOwnedAndActive = (snapshot["hostPhase"]?.uint32Value ?? 0) == 1
            && (snapshot["hostGeneration"]?.uint64Value ?? 0) == inviteGeneration
        let accepted = regenerating && hostIsOwnedAndActive
            ? bridge.nearbyHostRegenerateCode(code, generation: generation, nowNanoseconds: now)
            : bridge.nearbyHostPublishCode(code, generation: generation, nowNanoseconds: now)
        guard accepted else { return }
        inviteCode = code
        inviteGeneration = generation
        inviteDeadlineNanoseconds = now &+ 60_000_000_000
        inviteRemainingSeconds = 60
    }

    private func refreshInvite() {
        guard inviteGeneration != 0 else { return }
        let now = monotonicNanoseconds()
        bridge.nearbyTickNanoseconds(now)
        let snapshot = bridge.nearbyInviteSnapshot()
        let active = (snapshot["hostPhase"]?.uint32Value ?? 0) == 1
            && (snapshot["hostGeneration"]?.uint64Value ?? 0) == inviteGeneration
        guard active else {
            clearInvite()
            return
        }
        let remaining = inviteDeadlineNanoseconds > now
            ? inviteDeadlineNanoseconds - now
            : 0
        inviteRemainingSeconds = Int((remaining + 999_999_999) / 1_000_000_000)
    }

    private func clearInvite() {
        inviteCode = ""
        inviteGeneration = 0
        inviteDeadlineNanoseconds = 0
        inviteRemainingSeconds = 0
    }

    private func cancelOwnedRouteState() {
        if inviteGeneration != 0 {
            _ = bridge.nearbyHostCancelGeneration(inviteGeneration)
        }
        if joinSubmitted && nextJoinAttemptID > 1 {
            _ = bridge.nearbyCancelAttempt(nextJoinAttemptID - 1)
        }
        clearInvite()
        joinSubmitted = false
    }

    private func monotonicNanoseconds() -> UInt64 {
        UInt64(ProcessInfo.processInfo.systemUptime * 1_000_000_000)
    }

    private func secureInviteCode() -> String {
        var value = ""
        for _ in 0..<6 { value += String(arc4random_uniform(10)) }
        return value
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
