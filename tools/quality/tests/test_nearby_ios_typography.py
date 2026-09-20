"""iOS nearby typography restoration (TYPO-03.I).

Static/host checks that NearbyTypography.swift exists, is compiled into the
product app, and that N00–N03 consume the same paneTitle/muted tokens with
Dynamic Type. FlyNESUITests still own on-simulator measurements.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
IOS_APP = REPO_ROOT / "ios" / "app"
IOS_TESTS = REPO_ROOT / "ios" / "tests"
TYPO_PATH = IOS_APP / "NearbyTypography.swift"
CMAKE_PATH = IOS_APP / "CMakeLists.txt"
XCTEST_PATH = IOS_TESTS / "NearbyUxRestorationTests.mm"

NEARBY_PAGES = (
    IOS_APP / "NearbyFriendsView.swift",
    IOS_APP / "NearbyPairingView.swift",
    IOS_APP / "NearbyLobbyView.swift",
    IOS_APP / "NearbyFriendsManageView.swift",
)

FRIENDS = IOS_APP / "NearbyFriendsView.swift"
PAIRING = IOS_APP / "NearbyPairingView.swift"
CATALOG = IOS_APP / "CatalogLibraryView.swift"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8") if path.is_file() else ""


def _cmake_product_swift(cmake: str) -> str:
    match = re.search(r"set\(FLYNES_PRODUCT_SWIFT\s*(.*?)\n\)", cmake, re.S)
    return match.group(1) if match else ""


def _cmake_ui_tests(cmake: str) -> str:
    match = re.search(r"add_library\(FlyNESUITests MODULE(.*?)\)", cmake, re.S)
    return match.group(1) if match else ""


def _headline_block(source: str, key: str, limit: int = 420) -> str:
    token = f'Text("{key}")'
    idx = source.find(token)
    if idx < 0:
        return ""
    return source[idx : idx + limit]


def _wide_branch(source: str) -> str:
    idx = source.find("if isWide")
    if idx < 0:
        return ""
    else_idx = source.find("else {", idx)
    if else_idx < 0:
        return source[idx : idx + 900]
    return source[idx:else_idx]


def _role_size(helper: str, role: str) -> str | None:
    match = re.search(rf"{role}\s*[:=][^\n]*size:\s*([0-9.]+)", helper)
    return match.group(1) if match else None


class NearbyIosTypographyTests(unittest.TestCase):
    def test_typo_i_roles_and_scaling(self):
        friends = _read(FRIENDS)
        pairing = _read(PAIRING)
        n00_headline = _headline_block(friends, "nearby.entry.headline")
        n00_subtitle = _headline_block(friends, "nearby.entry.subtitle")
        n01_headline = _headline_block(pairing, "nearby.invite.headline")
        n01_subtitle = _headline_block(pairing, "nearby.invite.subtitle")

        self.assertTrue(
            TYPO_PATH.is_file(),
            "NearbyTypography.swift missing; N00 still uses title3 vs N01 size 21/"
            f"subtitle 14 (n00={n00_headline[:80]!r} n01={n01_headline[:80]!r})",
        )
        helper = _read(TYPO_PATH)
        self.assertIn("ScaledMetric", helper)
        self.assertIn("UIFontMetrics", helper)
        self.assertEqual("21", _role_size(helper, "paneTitle"))
        self.assertRegex(helper, r"paneTitle[\s\S]{0,200}semibold|weight:\s*\.semibold")
        self.assertEqual("12", _role_size(helper, "muted"))
        self.assertEqual("14", _role_size(helper, "action"))
        self.assertEqual("14", _role_size(helper, "primaryAction"))
        self.assertEqual("29", _role_size(helper, "inviteCode"))
        self.assertEqual("11", _role_size(helper, "kicker"))
        self.assertEqual("28", _role_size(helper, "codeInput"))
        self.assertEqual("18", _role_size(helper, "pageTitle"))
        self.assertEqual("15", _role_size(helper, "sectionTitle"))
        self.assertEqual("14", _role_size(helper, "body"))
        self.assertRegex(helper, r"0\.17|trackingEm:\s*\.17")
        self.assertRegex(helper, r"0\.13|trackingEm:\s*\.13")
        self.assertRegex(helper, r"0\.16|trackingEm:\s*\.16")
        self.assertRegex(helper, r"4\.93")
        self.assertRegex(helper, r"1\.43")
        self.assertIn("48", helper)

        cmake = _read(CMAKE_PATH)
        swift_list = _cmake_product_swift(cmake)
        self.assertIn(
            "NearbyTypography.swift",
            swift_list,
            "NearbyTypography.swift must be in FLYNES_PRODUCT_SWIFT, not tests-only",
        )
        ui_tests = _cmake_ui_tests(cmake)
        self.assertIn("NearbyUiParityTests.mm", ui_tests)
        self.assertIn(
            "NearbyUxRestorationTests.mm",
            ui_tests,
            "register NearbyUxRestorationTests.mm in FlyNESUITests MODULE",
        )
        self.assertTrue(XCTEST_PATH.is_file())
        xctest = _read(XCTEST_PATH)
        self.assertIn("@interface NearbyUxRestorationTests", xctest)
        self.assertEqual(xctest.count("@interface "), 1, "do not create a second class name")
        self.assertIn("TYPO_I_roles_and_scaling", xctest)
        self.assertIn("TYPO_I_wide_scroll", xctest)
        self.assertIn("TYPO_I_single_header", xctest)

        for page in NEARBY_PAGES:
            text = _read(page)
            self.assertIn(
                "NearbyTypography",
                text,
                f"{page.name} must use NearbyTypography",
            )

        self.assertIn("NearbyTypography.paneTitle", n00_headline)
        self.assertIn("NearbyTypography.paneTitle", n01_headline)
        self.assertIn("NearbyTypography.muted", n00_subtitle)
        self.assertIn("NearbyTypography.muted", n01_subtitle)
        self.assertNotIn(".title3", n00_headline)
        self.assertNotRegex(n00_headline, r"\.font\(\.title3")
        self.assertNotRegex(
            n01_headline,
            r"\.system\(size:\s*21",
            "N01 must not keep unscaled system(size: 21); share paneTitle + Dynamic Type",
        )
        self.assertNotRegex(n01_subtitle, r"\.system\(size:\s*14")
        self.assertNotRegex(pairing, r"\.tracking\(4\)")
        self.assertNotRegex(pairing, r"\.tracking\(1\.6\)")
        self.assertNotRegex(friends, r"\.tracking\(1\.6\)")
        self.assertNotIn("NearbyTypography", _read(CATALOG))

    def test_typo_i_wide_scroll(self):
        friends = _read(FRIENDS)
        wide = _wide_branch(friends)
        idx_scroll = friends.find("ScrollView")
        idx_wide = friends.find("if isWide")
        wraps_both = 0 <= idx_scroll < idx_wide
        self.assertTrue(
            "ScrollView" in wide or wraps_both,
            "N00 wide branch has no ScrollView; last action is unreachable at "
            "640x360 large type because the footer clips the body",
        )
        self.assertIn("nearby_entry_footer", friends)
        xctest = _read(XCTEST_PATH)
        self.assertIn("TYPO_I_wide_scroll", xctest)
        self.assertRegex(xctest, r"640|LandscapeLeft|landscape")
        self.assertIn("en", xctest.lower())

    def test_typo_i_single_header(self):
        friends = _read(FRIENDS)
        self.assertIn('.navigationTitle("nearby.title")', friends)
        self.assertEqual(
            friends.count('Text("nearby.title")'),
            0,
            "N00 must not stack a custom HStack title with navigationTitle; "
            "keep system back semantics",
        )
        self.assertIn("toolbar(.visible", friends)
        xctest = _read(XCTEST_PATH)
        self.assertIn("TYPO_I_single_header", xctest)
        self.assertIn("navigationBars", xctest)

    def test_typo_i_narrow_224_and_underline_tabs(self):
        pairing = _read(PAIRING)
        friends = _read(FRIENDS)
        for match in re.finditer(r"\.frame\(width:\s*224", pairing):
            window = pairing[max(0, match.start() - 120) : match.end() + 40]
            self.assertRegex(
                window,
                r"wide|isWide",
                "narrow must cancel left pane frame(width: 224) lock; got "
                f"{window!r}",
            )
        self.assertNotIn(
            ".pickerStyle(.segmented)",
            friends,
            "restore underline tabs, not segmented Picker as equal",
        )
        self.assertNotIn("Picker(", friends)
        self.assertIn("nearby_tab_devices", friends)
        self.assertIn("nearby_tab_friends", friends)
        self.assertRegex(friends, r"frame\(height:\s*2\)|underline")
        self.assertIn("ViewThatFits", pairing)


if __name__ == "__main__":
    unittest.main()
