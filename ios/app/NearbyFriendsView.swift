import SwiftUI

/// 好友 / 附近设备 — one page with two tabs (design §22.1; spec §10 D13).
///
/// This page invents nothing and reads no session value: `FlyNesAppBridge` has
/// no nearby/session method yet (spec §3), so the friend list stays empty, the
/// pipeline marks the stage that is knowable without a session, and the
/// discovery controls are shown disabled with a visible reason (spec §4).
/// Entering this page is real local navigation, so the entry itself is enabled.
struct NearbyFriendsView: View {
    private enum PageTab: String, CaseIterable, Identifiable {
        case friends
        case devices

        var id: String { rawValue }

        var titleKey: LocalizedStringKey {
            switch self {
            case .friends: return "nearby.tab.friends"
            case .devices: return "nearby.tab.devices"
            }
        }
    }

    /// Saved friends, read from the local friend store. That store is not in
    /// the shared ABI yet (`FlyNesAppBridge` has no friend method, spec §3), so
    /// no build can list one: this stays empty and is never filled with sample
    /// rows. When the store lands, point this at it — the initial tab below
    /// then follows with no further change.
    private static var savedFriendIds: [String] { [] }

    /// Initial tab rule (spec §4): open on 附近设备 while no friend is saved,
    /// on 好友 once at least one is. The last-used tab is never persisted, and
    /// the seed reads the friend list instead of a fixed tab index, so it
    /// becomes 好友 by itself when the friend store lands — do not "fix" it
    /// back to the first tab.
    @State private var tab: PageTab =
        NearbyFriendsView.savedFriendIds.isEmpty ? .devices : .friends

    private let pageBackground = Color(red: 18 / 255, green: 19 / 255, blue: 22 / 255)
    private let surface = Color(red: 27 / 255, green: 29 / 255, blue: 34 / 255)
    private let raised = Color(red: 41 / 255, green: 43 / 255, blue: 49 / 255)
    private let primary = Color(red: 255 / 255, green: 107 / 255, blue: 94 / 255)

    var body: some View {
        GeometryReader { geometry in
            let isWide = geometry.size.width > 580
            Group {
                if isWide {
                    HStack(spacing: 18) {
                        ScrollView {
                            nearbyActions
                        }
                        .frame(width: 224)

                        ScrollView {
                            statusPane
                        }
                        .frame(maxWidth: .infinity, alignment: .topLeading)
                    }
                } else {
                    ScrollView {
                        VStack(alignment: .leading, spacing: 18) {
                            nearbyActions
                            statusPane
                        }
                    }
                }
            }
            .padding(16)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .background(pageBackground.ignoresSafeArea())
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("nearby_root")
        .navigationTitle("nearby.title")
        .navigationBarTitleDisplayMode(.inline)
    }

    /// The action pane is outside the tab switch so all three primary actions
    /// remain available on both 好友 and 附近设备 (C04).
    @ViewBuilder private var nearbyActions: some View {
        VStack(alignment: .leading, spacing: 12) {
            NavigationLink {
                NearbyPairingView(mode: .create)
            } label: {
                Text("nearby.action.create")
                    .frame(maxWidth: .infinity, minHeight: 48)
                    .accessibilityIdentifier("nearby_action_create_label")
            }
            .buttonStyle(.borderedProminent)
            .tint(primary)
            .accessibilityIdentifier("nearby_action_create")

            NavigationLink {
                NearbyPairingView(mode: .joinCode)
            } label: {
                Text("nearby.action.enterCode")
                    .frame(maxWidth: .infinity, minHeight: 48)
                    .accessibilityIdentifier("nearby_action_enter_code_label")
            }
            .buttonStyle(.bordered)
            .tint(primary)
            .accessibilityIdentifier("nearby_action_enter_code")

            NavigationLink {
                NearbyPairingView(mode: .scan)
            } label: {
                Text("nearby.action.scanQr")
                    .frame(maxWidth: .infinity, minHeight: 48)
                    .accessibilityIdentifier("nearby_action_scan_qr_label")
            }
            .buttonStyle(.bordered)
            .tint(primary)
            .accessibilityIdentifier("nearby_action_scan_qr")
        }
        .padding(16)
        .background(surface, in: RoundedRectangle(cornerRadius: 12))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("nearby_action_pane")
    }

    @ViewBuilder private var statusPane: some View {
        VStack(alignment: .leading, spacing: 16) {
            Picker("nearby.title", selection: $tab) {
                ForEach(PageTab.allCases) { item in
                    Text(item.titleKey).tag(item)
                }
            }
            .pickerStyle(.segmented)

            switch tab {
            case .friends:
                friendsTab
            case .devices:
                devicesTab
            }
        }
        .padding(16)
        .frame(maxWidth: .infinity, alignment: .topLeading)
        .background(surface, in: RoundedRectangle(cornerRadius: 12))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("nearby_status_pane")
    }

    /// 好友 tab: the empty state plus the blocked friend-store key. Never a
    /// sample row and never a count — no saved identity exists locally.
    ///
    /// 好友管理 is real local navigation into a page that exists to display its
    /// blocking keys, which spec §4 permits only for the 好友/附近设备 and 配对
    /// entries — so the row stays enabled and the *actions* inside it are the
    /// disabled ones (spec §4, §10 D2).
    @ViewBuilder private var friendsTab: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("nearby.friends.empty")
                .foregroundStyle(.secondary)
                .accessibilityIdentifier("nearby_friends_empty")
            Text("nearby.blocked.friend_store")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .accessibilityIdentifier("nearby_friends_blocked")
            NavigationLink {
                NearbyFriendsManageView()
            } label: {
                Text("nearby.friends.manage")
                    .frame(maxWidth: .infinity, minHeight: 48, alignment: .leading)
            }
            .padding(.horizontal, 12)
            .background(raised, in: RoundedRectangle(cornerRadius: 10))
            .accessibilityIdentifier("nearby_friends_manage")
        }
    }

    /// 附近设备 tab: the seven pairing stages in pipeline order, the device
    /// empty state, and the two discovery controls, each disabled with the
    /// discovery stage and its reason.
    @ViewBuilder private var devicesTab: some View {
        VStack(alignment: .leading, spacing: 12) {
            ForEach(PairingStage.allCases) { stage in
                PairingStageRow(stage: stage)
            }
            Divider()
            Text("nearby.devices.empty")
                .foregroundStyle(.secondary)
                .accessibilityIdentifier("nearby_devices_empty")
            Divider()
            Label {
                Text("nearby.stage.discovery")
            } icon: {
                Image(systemName: "antenna.radiowaves.left.and.right")
            }
            Text("nearby.stage.discovery.reason")
                .font(.footnote)
                .foregroundStyle(.secondary)
            DisabledActionRow(
                titleKey: "nearby.find_devices",
                reasonKey: "nearby.blocked.discovery",
                identifier: "nearby_find_devices"
            )
            DisabledActionRow(
                titleKey: "nearby.scan_host_qr",
                reasonKey: "nearby.blocked.discovery",
                identifier: "nearby_scan_host_qr"
            )
            Divider()
            NavigationLink {
                NearbyPairingView(mode: .create)
            } label: {
                Text("nearby.pairing.title")
                    .frame(maxWidth: .infinity, minHeight: 48, alignment: .leading)
            }
            .padding(.horizontal, 12)
            .background(raised, in: RoundedRectangle(cornerRadius: 10))
            .accessibilityIdentifier("nearby_open_pairing")
        }
    }
}

/// A control that cannot act is shown disabled with its visible reason, never
/// as a fake action (spec §4). The identifier is on the control and its reason
/// carries the `_reason` suffix, matching the Android resource ids so one test
/// vocabulary describes all three platforms.
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
/// pipeline order (spec §2.1, §10 D5).
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
