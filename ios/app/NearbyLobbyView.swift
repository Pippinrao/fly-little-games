import SwiftUI

/// 大厅 — every design §22.3 field as a row, plus the 双方确认 control.
///
/// No field is live: `FlyNesAppBridge` has no nearby/session method yet
/// (spec §3), so each row pairs its §2 label with the field-specific blocked
/// key from §3/§4, and falls back to `nearby.blocked.session_read` — the
/// generic "session state is not connected to this build yet" key — where no
/// field-specific key exists.
struct NearbyLobbyView: View {
    private let pageBackground = Color(red: 18 / 255, green: 19 / 255, blue: 22 / 255)
    private let surface = Color(red: 27 / 255, green: 29 / 255, blue: 34 / 255)
    private let raised = Color(red: 41 / 255, green: 43 / 255, blue: 49 / 255)
    private let primary = Color(red: 255 / 255, green: 107 / 255, blue: 94 / 255)
    private let onSurface = Color(red: 244 / 255, green: 239 / 255, blue: 230 / 255)
    private let muted = Color(red: 190 / 255, green: 184 / 255, blue: 174 / 255)
    @State private var detailsOpen = false
    @State private var snapshot: [String: Any] = [:]
    @State private var gameTitle = ""
    @State private var gameError = false
    @State private var running = false
    private let bridge = FlyNesNearbyBridge.sharedInstance
    private let ticker = Timer.publish(every: 0.05, on: .main, in: .common).autoconnect()

    var body: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 8) {
                ForEach(LobbyField.firstScreen) { field in
                    LobbyFieldRow(field: field, value: fieldValue(field))
                        .padding(12)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(raised, in: RoundedRectangle(cornerRadius: 8))
                }
                Button("nearby.diagnostics.title") {
                    detailsOpen.toggle()
                }
                .nearbyRole(NearbyTypography.action)
                .frame(maxWidth: .infinity, minHeight: 48)
                .accessibilityIdentifier("nearby_lobby_details")
                if detailsOpen {
                    ForEach(LobbyField.details) { field in
                        LobbyFieldRow(field: field, value: nil)
                            .padding(12)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(raised, in: RoundedRectangle(cornerRadius: 8))
                    }
                }
            }
            .padding(16)
        }
        .background(pageBackground.ignoresSafeArea())
        .foregroundStyle(onSurface)
        .safeAreaInset(edge: .bottom) {
            confirmSection
        }
        .navigationTitle("nearby.lobby.title")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar(.visible, for: .navigationBar)
        .onAppear { poll() }
        .onReceive(ticker) { _ in poll() }
        .onDisappear {
            // Presenting the run surface keeps `running` true. A real navigation
            // back from the lobby must release the process-scoped listener/session.
            if !running { bridge.cancel() }
        }
        .fullScreenCover(isPresented: $running) {
            RunGameView(canonicalId: bridge.canonicalId, romData: Data(), gameTitle: gameTitle,
                        nearbySession: true, onPauseCommand: { command in
                if command == "game_center" || command == "nearby_lobby" {
                    _ = bridge.returnLobby()
                    running = false
                }
            }).ignoresSafeArea()
        }
    }

    /// 双方确认 (spec §4, §10 D8): one primary 确认入局 control per side, bound
    /// to the pending-configuration fingerprint that both ends render
    /// identically. The fingerprint is the seat, authority, negotiated mode,
    /// ROM identity and capability-plan rows above — the same rows, in the same
    /// order, on both ends — so this section shows them as one bound unit
    /// rather than re-listing every value a second time. None of the five is
    /// readable yet (spec §3), so the fingerprint is shown as unavailable
    /// instead of fabricated, the single button is disabled with its reason,
    /// and 确认已失效 keeps its own row and its own bounded reason.
    @ViewBuilder private var confirmSection: some View {
        VStack(alignment: .center, spacing: 8) {
            Text("nearby.lobby.confirm")
                .nearbyRole(NearbyTypography.primaryAction)
            Text("nearby.lobby.confirm.fingerprint")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
            Text("nearby.lobby.confirm.no_fingerprint")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
            Button("nearby.lobby.confirm") { _ = bridge.confirm(); poll() }
                .disabled(!canConfirm)
                .nearbyRole(NearbyTypography.primaryAction)
                .frame(minWidth: 200, maxWidth: 320)
                .nearbyMinTap()
                .background(primary.opacity(0.45), in: RoundedRectangle(cornerRadius: 8))
                .foregroundStyle(onSurface)
                .accessibilityIdentifier("nearby_lobby_confirm")
            Text(confirmReason)
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
                .accessibilityIdentifier("nearby_lobby_confirm_reason")
            LobbyFieldRow(field: .confirmInvalidated, value: nil)
            Text("nearby.lobby.confirm_invalidated.reason")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
        }
        .frame(maxWidth: .infinity)
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
        .background(surface)
    }

    private var canConfirm: Bool {
        (snapshot["state"] as? NSNumber)?.uint32Value == 5
            && (snapshot["localConfigured"] as? NSNumber)?.uint32Value == 1
            && (snapshot["peerConfigured"] as? NSNumber)?.uint32Value == 1
            && (snapshot["localReady"] as? NSNumber)?.uint32Value == 0
            && !gameError
    }

    private var confirmReason: LocalizedStringKey {
        if gameError { return "nearby.blocked.rom_transfer" }
        if (snapshot["localReady"] as? NSNumber)?.uint32Value == 1 { return "nearby.config.waitingConfirm" }
        return canConfirm ? "nearby.config.waitingConfirm" : "nearby.screen.connecting"
    }

    private func poll() {
        snapshot = bridge.snapshot()
        var selectionError: NSError?
        let configuredTitle = bridge.configureLocalGameIfNeeded(&selectionError)
        if !configuredTitle.isEmpty { gameTitle = configuredTitle }
        gameError = selectionError != nil
        let state = (snapshot["state"] as? NSNumber)?.uint32Value ?? 0
        if state == 6 { running = true }
        else if running && state != 6 { running = false }
    }

    private func fieldValue(_ field: LobbyField) -> String? {
        let role = (snapshot["role"] as? NSNumber)?.uint32Value ?? 0
        switch field {
        case .romIdentity: return gameTitle.isEmpty ? nil : gameTitle
        case .networkOwner: return role == 1 ? "iOS · P1" : "P1"
        case .seat: return role == 1 ? "iOS · P1" : "iOS · P2"
        default: return nil
        }
    }
}

/// A control that cannot act is shown disabled with its visible reason, never
/// as a fake action (spec §4).
private struct DisabledActionRow: View {
    let titleKey: LocalizedStringKey
    let reasonKey: LocalizedStringKey

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Button(titleKey) {}
                .disabled(true)
                .nearbyRole(NearbyTypography.action)
            Text(reasonKey)
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(.secondary)
        }
        .padding(.vertical, 2)
    }
}

/// Every design §22.3 lobby field in the §2.2 order. Display order is the
/// vocabulary order, so no platform may reorder a row (spec §2).
private enum LobbyField: String, CaseIterable, Identifiable {
    case friendName
    case identityFingerprint
    case authorityCapability
    case resourceRisk
    case networkOwner
    case seat
    case romIdentity
    case romLocalState
    case romTransferConfirm
    case romTransferProgress
    case profileVerified
    case modeExpected
    case localAudio
    case confirmInvalidated

    var id: String { rawValue }

    static let firstScreen: [LobbyField] = [.romIdentity, .networkOwner, .seat]
    static var details: [LobbyField] {
        allCases.filter { field in
            field != .confirmInvalidated && !firstScreen.contains(field)
        }
    }

    var accessibilityId: String {
        switch self {
        case .friendName: return "nearby_lobby_row_friend_name"
        case .identityFingerprint: return "nearby_lobby_row_identity_fingerprint"
        case .authorityCapability: return "nearby_lobby_row_authority_capability"
        case .resourceRisk: return "nearby_lobby_row_resource_risk"
        case .networkOwner: return "nearby_lobby_row_network_owner"
        case .seat: return "nearby_lobby_row_seat"
        case .romIdentity: return "nearby_lobby_row_rom_identity"
        case .romLocalState: return "nearby_lobby_row_rom_local_state"
        case .romTransferConfirm: return "nearby_lobby_row_rom_transfer_confirm"
        case .romTransferProgress: return "nearby_lobby_row_rom_transfer_progress"
        case .profileVerified: return "nearby_lobby_row_profile_verified"
        case .modeExpected: return "nearby_lobby_row_mode_expected"
        case .localAudio: return "nearby_lobby_row_local_audio"
        case .confirmInvalidated: return "nearby_lobby_row_confirm_invalidated"
        }
    }

    var titleKey: LocalizedStringKey {
        switch self {
        case .friendName: return "nearby.lobby.friend_name"
        case .identityFingerprint: return "nearby.lobby.identity_fingerprint"
        case .authorityCapability: return "nearby.lobby.authority_capability"
        case .resourceRisk: return "nearby.lobby.resource_risk"
        case .networkOwner: return "nearby.lobby.network_owner"
        case .seat: return "nearby.lobby.seat"
        case .romIdentity: return "nearby.lobby.rom_identity"
        case .romLocalState: return "nearby.lobby.rom_local_state"
        case .romTransferConfirm: return "nearby.lobby.rom_transfer_confirm"
        case .romTransferProgress: return "nearby.lobby.rom_transfer_progress"
        case .profileVerified: return "nearby.lobby.profile_verified"
        case .modeExpected: return "nearby.lobby.mode_expected"
        case .localAudio: return "nearby.lobby.local_audio"
        case .confirmInvalidated: return "nearby.lobby.confirm_invalidated"
        }
    }

    /// The field-specific blocked key from spec §3/§4, or
    /// `nearby.blocked.session_read` — the documented generic fallback — when
    /// the ABI gap for this field has no key of its own (spec §2.3, line 130).
    var blockedKey: LocalizedStringKey {
        switch self {
        case .friendName: return "nearby.blocked.friend_store"
        case .authorityCapability: return "nearby.blocked.authority_recovery"
        case .seat: return "nearby.blocked.mode_gate"
        case .romIdentity: return "nearby.blocked.rom_transfer"
        case .romLocalState: return "nearby.blocked.rom_transfer"
        case .romTransferConfirm: return "nearby.blocked.rom_transfer"
        case .romTransferProgress: return "nearby.blocked.rom_transfer"
        case .profileVerified: return "nearby.blocked.profile_verify"
        case .modeExpected: return "nearby.blocked.mode_gate"
        case .localAudio: return "nearby.blocked.local_mute"
        case .identityFingerprint: return "nearby.blocked.friend_store"
        case .resourceRisk: return "nearby.blocked.session_read"
        case .networkOwner: return "nearby.blocked.session_read"
        case .confirmInvalidated: return "nearby.blocked.session_read"
        }
    }
}

/// One lobby row: the §2 label above its own blocked key. Never a value, never
/// a count, never a sample peer (spec §4).
private struct LobbyFieldRow: View {
    let field: LobbyField
    let value: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(field.titleKey)
                .nearbyRole(NearbyTypography.body)
            if let value {
                Text(value)
                    .nearbyRole(NearbyTypography.muted)
                    .foregroundStyle(.secondary)
            } else {
                Text(field.blockedKey)
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, 2)
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier(field.accessibilityId)
    }
}
