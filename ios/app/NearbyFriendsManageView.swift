import SwiftUI

/// 好友管理 — rename / delete / block / 身份重置.
///
/// Every entry is present and disabled with `nearby.blocked.friend_store`: no
/// friend store is in the shared ABI (`FlyNesAppBridge` has no friend method,
/// spec §3), so no saved identity can be renamed, deleted, blocked or reset,
/// and a control that cannot act is shown disabled with a visible reason rather
/// than offered as a fake action (spec §4).
///
/// Reachable from 好友/附近设备; opened from the 好友 tab's 好友管理 row and
/// from the 好友管理 row inside Settings (spec §10 D2 — a row inside the
/// existing Settings page, not a sixth settings root).
struct NearbyFriendsManageView: View {
    var body: some View {
        List {
            Section {
                Text("nearby.friends.saved_after_auth")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                ForEach(ManageAction.allCases) { action in
                    DisabledActionRow(
                        titleKey: action.titleKey,
                        reasonKey: "nearby.blocked.friend_store"
                    )
                }
            }
        }
        .navigationTitle("nearby.friends.manage")
        .navigationBarTitleDisplayMode(.inline)
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
            Text(reasonKey)
                .font(.footnote)
                .foregroundStyle(.secondary)
        }
        .padding(.vertical, 2)
    }
}

/// The friend-management entries in the §2.3 order; 本机身份重置 is last
/// because it is the destructive whole-device action, not a per-friend one
/// (spec §4, §2.3).
private enum ManageAction: String, CaseIterable, Identifiable {
    case rename
    case delete
    case block
    case identityReset

    var id: String { rawValue }

    var titleKey: LocalizedStringKey {
        switch self {
        case .rename: return "nearby.friends.rename"
        case .delete: return "nearby.friends.delete"
        case .block: return "nearby.friends.block"
        case .identityReset: return "nearby.friends.identity_reset"
        }
    }
}
