"""Harmony nearby typography restoration (TYPO-01.H).

Static/host checks that the four Nearby pages import NearbyTypography.ets
and do not hardcode N00 muted 14 / action 16. Hypium still owns the token
and scale-parser assertions.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
HARMONY_ETS = REPO_ROOT / "harmony" / "entry" / "src" / "main" / "ets"
TYPO_PATH = HARMONY_ETS / "ui" / "NearbyTypography.ets"
SCALE_PATH = HARMONY_ETS / "ui" / "NearbyTestFontScale.ets"
ENTRY_ABILITY = HARMONY_ETS / "entryability" / "EntryAbility.ets"
GAME_CENTER = HARMONY_ETS / "pages" / "GameCenter.ets"
HYPUM_TEST = (
    REPO_ROOT
    / "harmony"
    / "entry"
    / "src"
    / "ohosTest"
    / "ets"
    / "test"
    / "NearbyUxRestoration.test.ets"
)
LIST_TEST = (
    REPO_ROOT
    / "harmony"
    / "entry"
    / "src"
    / "ohosTest"
    / "ets"
    / "test"
    / "List.test.ets"
)

NEARBY_PAGES = (
    HARMONY_ETS / "pages" / "NearbyFriends.ets",
    HARMONY_ETS / "pages" / "NearbyPairing.ets",
    HARMONY_ETS / "pages" / "NearbyLobby.ets",
    HARMONY_ETS / "pages" / "NearbyFriendsManage.ets",
)

N00_MUTED_MARKERS = (
    "nearby_entry_subtitle",
    "nearby_invite_subtitle",
    "nearby_join_subtitle",
    "nearby_scan_subtitle",
)

N00_ACTION_MARKERS = (
    "nearby_action_create",
    "nearby_action_enterCode",
    "nearby_action_scanQr",
    "nearby_action_regenerate",
    "nearby_action_switchToScan",
    "nearby_action_switchToJoinCode",
    "nearby_action_submitJoinCode",
    "nearby_action_cancelInvite",
    "nearby_tab_devices",
    "nearby_tab_friends",
    "nearby_lobby_confirm",
)

ICON_FONT_MARKERS = (
    ("Text('‹')", "26"),
    ("Text('i')", "16"),
    ("Text(this.tab === TAB_FRIENDS ? '⚙' : '⌕')", "18"),
)


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _widget_block(source: str, marker: str, limit: int = 700) -> str:
    token = f"'{marker}'" if marker.startswith("nearby_") else marker
    idx = source.find(token)
    if idx < 0:
        idx = source.find(marker)
    if idx < 0:
        return ""
    return source[idx : idx + limit]


def _first_font_size(block: str) -> str | None:
    match = re.search(r"\.fontSize\(([^)]+)\)", block)
    return match.group(1).strip() if match else None


def _role_object(source: str, role: str) -> str:
    match = re.search(rf"{role}\s*:\s*\{{([^}}]+)\}}", source)
    return match.group(1) if match else ""


def _role_number(source: str, role: str, field: str) -> float | None:
    blob = _role_object(source, role)
    match = re.search(rf"{field}\s*:\s*([0-9.]+)", blob)
    return float(match.group(1)) if match else None


def _scale_sources() -> str:
    parts: list[str] = []
    for path in (ENTRY_ABILITY, SCALE_PATH, TYPO_PATH):
        if path.is_file():
            parts.append(_read(path))
    return "\n".join(parts)


class NearbyHarmonyTypographyTests(unittest.TestCase):
    def test_hypium_restoration_file_is_registered(self):
        self.assertTrue(HYPUM_TEST.is_file(), "NearbyUxRestoration.test.ets missing")
        listing = _read(LIST_TEST)
        self.assertIn("NearbyUxRestoration.test", listing)
        self.assertIn("nearbyUxRestorationTest", listing)

    def test_typo_h_n00_roles_helper_and_pages(self):
        friends = _read(HARMONY_ETS / "pages" / "NearbyFriends.ets")
        subtitle = _first_font_size(_widget_block(friends, "nearby_entry_subtitle"))
        create = _first_font_size(_widget_block(friends, "nearby_action_create"))
        self.assertTrue(
            TYPO_PATH.is_file(),
            f"NearbyTypography.ets missing; N00 still uses subtitle {subtitle} / "
            f"button {create} (must become muted 12 / action 14)",
        )
        helper = _read(TYPO_PATH)
        self.assertEqual(21, _role_number(helper, "paneTitle", "size"))
        self.assertEqual(600, _role_number(helper, "paneTitle", "weight"))
        self.assertEqual(27.3, _role_number(helper, "paneTitle", "lineHeight"))
        self.assertEqual(12, _role_number(helper, "muted", "size"))
        self.assertEqual(14, _role_number(helper, "action", "size"))
        self.assertEqual(14, _role_number(helper, "primaryAction", "size"))
        self.assertEqual(600, _role_number(helper, "primaryAction", "weight"))
        self.assertEqual(29, _role_number(helper, "inviteCode", "size"))
        self.assertEqual(600, _role_number(helper, "inviteCode", "weight"))
        self.assertEqual(11, _role_number(helper, "kicker", "size"))

        for page in NEARBY_PAGES:
            text = _read(page)
            self.assertIn(
                "NearbyTypography",
                text,
                f"{page.name} must import NearbyTypography.ets",
            )
            self.assertRegex(
                text,
                r"NEARBY_TYPE",
                f"{page.name} must consume NEARBY_TYPE tokens",
            )

        self.assertNotEqual(
            "14",
            subtitle,
            "N00 subtitle must not hardcode muted 14; use muted 12",
        )
        self.assertNotEqual(
            "16",
            create,
            "N00 action button must not hardcode 16; use action/primaryAction 14",
        )
        self.assertTrue(
            subtitle is not None and ("NEARBY_TYPE.muted" in subtitle or subtitle == "12"),
            f"N00 subtitle fontSize should be muted 12 via helper, got {subtitle}",
        )
        self.assertTrue(
            create is not None
            and (
                "NEARBY_TYPE.primaryAction" in create
                or "NEARBY_TYPE.action" in create
                or create == "14"
            ),
            f"N00 create button fontSize should be 14 via helper, got {create}",
        )

        pairing = _read(HARMONY_ETS / "pages" / "NearbyPairing.ets")
        invite_code = _widget_block(pairing, "this.inviteCode === ''")
        self.assertIn("NEARBY_TYPE.inviteCode", invite_code)
        self.assertNotIn("FontWeight.Bold", invite_code)
        self.assertNotRegex(invite_code, r"\.letterSpacing\(4\)")
        kicker = _widget_block(pairing, "nearby_invite_kicker")
        self.assertNotRegex(kicker, r"\.letterSpacing\(1\.6\)")
        self.assertIn("nearbyLetterSpacingFp", pairing)

        for page in NEARBY_PAGES:
            text = _read(page)
            for marker in N00_MUTED_MARKERS:
                if marker not in text:
                    continue
                block = _widget_block(text, marker)
                size = _first_font_size(block)
                self.assertNotEqual("14", size, f"{page.name} {marker} still hardcoded 14")
                self.assertNotIn("TextOverflow.Ellipsis", block)
            for marker in N00_ACTION_MARKERS:
                if marker not in text:
                    continue
                block = _widget_block(text, marker)
                size = _first_font_size(block)
                self.assertNotEqual("16", size, f"{page.name} {marker} still hardcoded 16")

        game_center = _read(GAME_CENTER)
        self.assertNotIn("NearbyTypography", game_center)

    def test_typo_h_scale13_want_is_accepted_and_logged_on_reject(self):
        blob = _scale_sources()
        self.assertTrue(
            SCALE_PATH.is_file() or "1.3" in blob,
            "debug Want 1.3 is ignored; allow {1, 1.3, 1.5, 2}",
        )
        self.assertRegex(blob, r"1\.3")
        self.assertRegex(blob, r"1\.5")
        self.assertRegex(blob, r"rejected", "invalid Want values must log rejection")
        entry = _read(ENTRY_ABILITY)
        self.assertIn("applicationInfo.debug", entry)
        self.assertNotIn("setFontFollowSystem(false)", blob)
        self.assertIn("followSystem", blob)
        self.assertTrue(SCALE_PATH.is_file(), "extract parse/apply so 1.3 is unit-testable")
        scale = _read(SCALE_PATH)
        self.assertIn("parseTestFontSizeScale", scale)
        self.assertIn("resolveTestFontSizeScale", scale)
        self.assertIn("1.3", scale)
        self.assertIn("1.2", _read(HYPUM_TEST))
        hypium_scale = _read(HYPUM_TEST)
        self.assertIn("headline13 > headline1", hypium_scale)
        self.assertIn("getBounds", hypium_scale)
        self.assertIn("px2vp", hypium_scale)
        self.assertNotIn("getTextSize", hypium_scale)
        self.assertNotIn("applied === 1.3", hypium_scale,
                         "1.3 must be proven by measured page text, not Want/config parse")

    def test_typo_h_scale2_min_tap_grows_and_pages_scroll(self):
        self.assertTrue(TYPO_PATH.is_file(), "scale-2 layout helpers live in NearbyTypography.ets")
        helper = _read(TYPO_PATH)
        self.assertIn("nearbyTapHeightVp", helper)
        self.assertIn("nearbyShouldScrollVertically", helper)
        self.assertIn("nearbyRoleFontSizeFp", helper)
        self.assertIn("nearbyAllowsEllipsisToFit", helper)
        friends = _read(HARMONY_ETS / "pages" / "NearbyFriends.ets")
        create = _widget_block(friends, "nearby_action_create")
        self.assertIn("constraintSize", create)
        self.assertIn("minHeight", create)
        self.assertNotRegex(
            create,
            r"\.height\(48\)",
            "48vp is a minimum tap height and may grow; do not clip with fixed height 48",
        )
        subtitle = _widget_block(friends, "nearby_entry_subtitle")
        self.assertNotIn(".maxLines(2)", subtitle)
        self.assertNotIn("TextOverflow.Ellipsis", subtitle)
        for page in NEARBY_PAGES:
            text = _read(page)
            self.assertIn("Scroll()", text, f"{page.name} must keep vertical scroll")
        pairing = _read(HARMONY_ETS / "pages" / "NearbyPairing.ets")
        footer_btn = _widget_block(pairing, "this.footerActionLabel()")
        self.assertNotRegex(footer_btn, r"\.height\(48\)")
        self.assertIn("constraintSize", footer_btn)

        # Icon glyph sizes stay put.
        self.assertIn(".fontSize(26)", friends)
        self.assertIn(".fontSize(16)", _widget_block(friends, "Text('i')"))

        hypium = _read(HYPUM_TEST)
        self.assertIn("TYPO_H_scale2_reachable", hypium)
        self.assertIn("@ohos.UiTest", hypium)
        self.assertIn("nearby_entry_headline", hypium)
        self.assertNotIn("getTextSize", hypium)
        self.assertIn("getBounds", hypium)
        self.assertIn("px2vp", hypium)
        self.assertIn(".click()", hypium)
        self.assertIn("headline13 > headline1", hypium)
        self.assertIn("headline1 > subtitle1", hypium)
        self.assertIn("createHeightVp >= 48", hypium)
        self.assertNotRegex(hypium, r"createHeight >= 48")
        self.assertIn("nearby_invite_headline", hypium)
        self.assertIn("it('JOIN_submit_fail_cancel',", hypium)
        self.assertIn("it('JOIN_modify_failed_code_retry_cancel',", hypium)
        # The join flow is split to fit Hypium's per-case timeout; its hooks
        # still submit for real and verify editable input before cancelling.
        self.assertIn("beforeEach(async () => {", hypium)
        self.assertIn("await input.inputText('123456');", hypium)
        self.assertIn("await submit.click();", hypium)
        self.assertIn("expectedCode = '654321';", hypium)
        self.assertIn("await inputAfterFail.inputText(expectedCode, { paste: true });", hypium)
        self.assertIn("await retry.click();", hypium)
        self.assertIn("afterEach(async () => {", hypium)
        self.assertIn("expect(await inputAfterSubmit.getText()).assertEqual(expectedCode);", hypium)
        self.assertIn("expect(await inputAfterSubmit.isEnabled()).assertTrue();", hypium)
        self.assertIn("expect(await submitAfterRetry.isEnabled()).assertTrue();", hypium)
        self.assertIn("await cancel.click();", hypium)
        self.assertIn("expect(await componentExists(driver, 'nearby_entry_headline')).assertTrue();", hypium)
        self.assertIn("nearby_join_cancel", hypium)
        self.assertIn("N09_first_screen_is_game_host_seat_confirm", hypium)
        self.assertIn("nearby_lobby_identity_fingerprint", hypium)
        self.assertNotIn("tap2 > NEARBY_MIN_TAP_HEIGHT_VP", hypium)
        self.assertNotRegex(hypium, r"tap2 > NEARBY_MIN_TAP_HEIGHT")
        self.assertNotRegex(
            hypium,
            r"nearbyTapHeightVp\(",
            "scale-2 reachability must measure the rendered create button, not helper math",
        )


if __name__ == "__main__":
    unittest.main()
