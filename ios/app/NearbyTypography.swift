import SwiftUI
import UIKit

/// Nearby text roles from `shared/schema/nearby_typography_v1.json`.
/// Base sizes are CSS px targets consumed as iOS pt; Dynamic Type scales them
/// via `ScaledMetric` / `UIFontMetrics`. CSS px, Android sp, Harmony fp, and
/// iOS pt are not the same physical unit.
struct NearbyTypeRole: Equatable {
    let size: CGFloat
    let weight: Font.Weight
    let lineHeight: CGFloat
    let trackingEm: CGFloat
    let textStyle: Font.TextStyle
}

enum NearbyTypography {
    static let pageTitle = NearbyTypeRole(size: 18, weight: .semibold, lineHeight: 23.4, trackingEm: 0, textStyle: .headline)
    static let paneTitle = NearbyTypeRole(size: 21, weight: .semibold, lineHeight: 27.3, trackingEm: 0, textStyle: .title3)
    static let sectionTitle = NearbyTypeRole(size: 15, weight: .semibold, lineHeight: 21.75, trackingEm: 0, textStyle: .subheadline)
    static let body = NearbyTypeRole(size: 14, weight: .regular, lineHeight: 20.3, trackingEm: 0, textStyle: .body)
    static let muted = NearbyTypeRole(size: 12, weight: .regular, lineHeight: 17.4, trackingEm: 0, textStyle: .caption)
    static let action = NearbyTypeRole(size: 14, weight: .regular, lineHeight: 20.3, trackingEm: 0, textStyle: .body)
    static let primaryAction = NearbyTypeRole(size: 14, weight: .semibold, lineHeight: 20.3, trackingEm: 0, textStyle: .body)
    static let kicker = NearbyTypeRole(size: 11, weight: .regular, lineHeight: 15.95, trackingEm: 0.13, textStyle: .caption2)
    static let inviteCode = NearbyTypeRole(size: 29, weight: .semibold, lineHeight: 40.6, trackingEm: 0.17, textStyle: .largeTitle)
    static let codeInput = NearbyTypeRole(size: 28, weight: .regular, lineHeight: 40.6, trackingEm: 0.16, textStyle: .largeTitle)

    static let minTapHeight: CGFloat = 48

    /// .17em of 29 → 4.93pt; .13em of 11 → 1.43pt; .16em of 28 → 4.48pt.
    static func letterSpacing(_ role: NearbyTypeRole) -> CGFloat {
        (role.size * role.trackingEm * 100).rounded() / 100
    }

    static func uiTextStyle(_ style: Font.TextStyle) -> UIFont.TextStyle {
        switch style {
        case .largeTitle: return .largeTitle
        case .title: return .title1
        case .title2: return .title2
        case .title3: return .title3
        case .headline: return .headline
        case .subheadline: return .subheadline
        case .body: return .body
        case .callout: return .callout
        case .footnote: return .footnote
        case .caption: return .caption1
        case .caption2: return .caption2
        default: return .body
        }
    }

    static func scaledSize(
        for role: NearbyTypeRole,
        category: UIContentSizeCategory = .large
    ) -> CGFloat {
        let metrics = UIFontMetrics(forTextStyle: uiTextStyle(role.textStyle))
        let traits = UITraitCollection(preferredContentSizeCategory: category)
        return metrics.scaledValue(for: role.size, compatibleWith: traits)
    }

    static func tapHeight(scaledLineHeight: CGFloat) -> CGFloat {
        max(minTapHeight, scaledLineHeight)
    }
}

private struct NearbyRoleModifier: ViewModifier {
    let role: NearbyTypeRole
    @ScaledMetric private var size: CGFloat
    @ScaledMetric private var lineHeight: CGFloat

    init(role: NearbyTypeRole) {
        self.role = role
        _size = ScaledMetric(wrappedValue: role.size, relativeTo: role.textStyle)
        _lineHeight = ScaledMetric(wrappedValue: role.lineHeight, relativeTo: role.textStyle)
    }

    func body(content: Content) -> some View {
        content
            .font(.system(size: size, weight: role.weight))
            .tracking(size * role.trackingEm)
            .lineSpacing(max(0, lineHeight - size))
    }
}

private struct NearbyMinTapModifier: ViewModifier {
    @ScaledMetric private var tap: CGFloat = NearbyTypography.minTapHeight

    func body(content: Content) -> some View {
        content.frame(minHeight: NearbyTypography.tapHeight(scaledLineHeight: tap))
    }
}

extension View {
    func nearbyRole(_ role: NearbyTypeRole) -> some View {
        modifier(NearbyRoleModifier(role: role))
    }

    func nearbyMinTap() -> some View {
        modifier(NearbyMinTapModifier())
    }
}
