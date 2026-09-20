import SwiftUI

/// N00 nearby() from the approved HTML mockup and design U06/U07.
/// Left: kicker, headline, subtitle, 创建联机 / 输入配对码 / 扫码加入.
/// Right: 附近设备 / 好友 and 寻找设备. Pairing stages are N07/N10, not this page.
/// Review-only sample friends/devices are not product data (design §4).
struct NearbyFriendsView: View {
    private enum PageTab: String, CaseIterable, Identifiable {
        case devices
        case friends

        var id: String { rawValue }

        var titleKey: LocalizedStringKey {
            switch self {
            case .friends: return "nearby.tab.friends"
            case .devices: return "nearby.tab.devices"
            }
        }

        var accessibilityId: String {
            switch self {
            case .friends: return "nearby_tab_friends"
            case .devices: return "nearby_tab_devices"
            }
        }
    }

    private static var savedFriendIds: [String] { [] }

    @State private var tab: PageTab =
        NearbyFriendsView.savedFriendIds.isEmpty ? .devices : .friends

    private let pageBackground = Color(red: 18 / 255, green: 19 / 255, blue: 22 / 255)
    private let onSurface = Color(red: 244 / 255, green: 239 / 255, blue: 230 / 255)
    private let muted = Color(red: 190 / 255, green: 184 / 255, blue: 174 / 255)
    private let primary = Color(red: 255 / 255, green: 107 / 255, blue: 94 / 255)

    var body: some View {
        GeometryReader { geometry in
            let isWide = geometry.size.width > 580
            VStack(spacing: 0) {
                ScrollView {
                    Group {
                        if isWide {
                            HStack(alignment: .top, spacing: 18) {
                                nearbyActions.frame(width: 224, alignment: .topLeading)
                                rightPane.frame(maxWidth: .infinity, alignment: .topLeading)
                            }
                        } else {
                            VStack(alignment: .leading, spacing: 18) {
                                nearbyActions
                                rightPane
                            }
                        }
                    }
                    .padding(16)
                }
                Text("nearby.entry.footer")
                    .nearbyRole(NearbyTypography.muted)
                    .foregroundStyle(muted)
                    .frame(maxWidth: .infinity, minHeight: 58, alignment: .leading)
                    .padding(.horizontal, 16)
                    .accessibilityIdentifier("nearby_entry_footer")
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .background(pageBackground.ignoresSafeArea())
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("nearby_root")
        .navigationTitle("nearby.title")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar(.visible, for: .navigationBar)
        .toolbar {
            ToolbarItem(placement: .navigationBarTrailing) {
                Text("nearby.entry.stageHint")
                    .nearbyRole(NearbyTypography.muted)
                    .foregroundStyle(muted)
                    .lineLimit(1)
            }
        }
    }

    @ViewBuilder private var nearbyActions: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("nearby.entry.kicker")
                .nearbyRole(NearbyTypography.kicker)
                .foregroundStyle(primary)
            Text("nearby.entry.headline")
                .nearbyRole(NearbyTypography.paneTitle)
                .foregroundStyle(onSurface)
                .fixedSize(horizontal: false, vertical: true)
                .accessibilityIdentifier("nearby_entry_headline")
            Text("nearby.entry.subtitle")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
                .accessibilityIdentifier("nearby_entry_subtitle")
            NavigationLink {
                NearbyPairingView(mode: .create)
            } label: {
                Text("nearby.action.create")
                    .nearbyRole(NearbyTypography.primaryAction)
                    .frame(maxWidth: .infinity)
                    .nearbyMinTap()
                    .accessibilityIdentifier("nearby_action_create_label")
            }
            .buttonStyle(.borderedProminent)
            .tint(primary)
            .accessibilityIdentifier("nearby_action_create")

            NavigationLink {
                NearbyPairingView(mode: .joinCode)
            } label: {
                Text("nearby.action.enterCode")
                    .nearbyRole(NearbyTypography.action)
                    .frame(maxWidth: .infinity)
                    .nearbyMinTap()
                    .accessibilityIdentifier("nearby_action_enter_code_label")
            }
            .buttonStyle(.bordered)
            .tint(primary)
            .accessibilityIdentifier("nearby_action_enter_code")

            NavigationLink {
                NearbyPairingView(mode: .scan)
            } label: {
                Text("nearby.action.scanQr")
                    .nearbyRole(NearbyTypography.action)
                    .frame(maxWidth: .infinity)
                    .nearbyMinTap()
                    .accessibilityIdentifier("nearby_action_scan_qr_label")
            }
            .buttonStyle(.bordered)
            .tint(primary)
            .accessibilityIdentifier("nearby_action_scan_qr")
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("nearby_action_pane")
    }

    @ViewBuilder private var rightPane: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                ForEach(PageTab.allCases) { item in
                    Button {
                        tab = item
                    } label: {
                        Text(item.titleKey)
                            .nearbyRole(tab == item ? NearbyTypography.primaryAction : NearbyTypography.action)
                            .foregroundStyle(tab == item ? onSurface : muted)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 12)
                            .overlay(alignment: .bottom) {
                                Rectangle()
                                    .fill(tab == item ? primary : Color.clear)
                                    .frame(height: 2)
                            }
                    }
                    .buttonStyle(.plain)
                    .accessibilityIdentifier(item.accessibilityId)
                }
                Spacer(minLength: 0)
                if tab == .friends {
                    NavigationLink {
                        NearbyFriendsManageView()
                    } label: {
                        Image(systemName: "person.crop.circle.badge.gearshape")
                            .frame(width: 48, height: 48)
                    }
                    .accessibilityIdentifier("nearby_friends_manage")
                    .accessibilityLabel(Text("nearby.friends.manage"))
                } else {
                    Button {
                    } label: {
                        Image(systemName: "dot.radiowaves.left.and.right")
                            .frame(width: 48, height: 48)
                    }
                    .disabled(true)
                    .accessibilityIdentifier("nearby_find_devices_tool")
                    .accessibilityLabel(Text("nearby.find_devices"))
                    .accessibilityValue(Text("nearby.blocked.discovery"))
                }
            }

            switch tab {
            case .friends:
                friendsTab
            case .devices:
                devicesTab
            }
        }
        .frame(maxWidth: .infinity, alignment: .topLeading)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("nearby_status_pane")
    }

    @ViewBuilder private var friendsTab: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("nearby.friends.empty")
                .nearbyRole(NearbyTypography.body)
                .foregroundStyle(onSurface)
                .accessibilityIdentifier("nearby_friends_empty")
            Text("nearby.blocked.friend_store")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
                .accessibilityIdentifier("nearby_friends_blocked")
            Text("nearby.friends.saved_after_auth")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
        }
    }

    @ViewBuilder private var devicesTab: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("nearby.devices.empty")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
                .accessibilityIdentifier("nearby_devices_empty")
            Button("nearby.find_devices") {}
                .nearbyRole(NearbyTypography.action)
                .nearbyMinTap()
                .disabled(true)
                .accessibilityIdentifier("nearby_find_devices")
            Text("nearby.blocked.discovery")
                .nearbyRole(NearbyTypography.muted)
                .foregroundStyle(muted)
                .accessibilityIdentifier("nearby_find_devices_reason")
        }
    }
}
