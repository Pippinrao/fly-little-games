"""REG-02.I host/static checks for iOS PAIR submit/cancel and N00 devices tool.

FlyNESUITests remain simulator-owned (NOT_RUN on Windows). Do not treat these
as screenshots. NearbyTypography tokens are owned by TYPO-03.I.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
IOS_APP = REPO_ROOT / "ios" / "app"
PAIRING = IOS_APP / "NearbyPairingView.swift"
FRIENDS = IOS_APP / "NearbyFriendsView.swift"
TYPO_PATH = IOS_APP / "NearbyTypography.swift"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8") if path.is_file() else ""


def _brace_end(source: str, open_idx: int) -> int:
    depth = 0
    i = open_idx
    while i < len(source):
        char = source[i]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def _member(source: str, name: str) -> str:
    for prefix in ("private var ", "private func ", "var ", "func "):
        token = f"{prefix}{name}"
        start = 0
        while True:
            idx = source.find(token, start)
            if idx < 0:
                break
            after = idx + len(token)
            if after < len(source) and (source[after].isalnum() or source[after] == "_"):
                start = after
                continue
            brace = source.find("{", idx)
            if brace < 0:
                return ""
            end = _brace_end(source, brace)
            if end < 0:
                return source[idx:]
            return source[idx : end + 1]
    return ""


def _skip_ws(source: str, idx: int) -> int:
    while idx < len(source) and source[idx] in " \t\r\n":
        idx += 1
    return idx


def _consume_parens(source: str, idx: int) -> int:
    if idx >= len(source) or source[idx] != "(":
        return idx
    depth = 0
    while idx < len(source):
        if source[idx] == "(":
            depth += 1
        elif source[idx] == ")":
            depth -= 1
            if depth == 0:
                return idx + 1
        idx += 1
    return idx


def _extend_view(source: str, cursor: int) -> int:
    while cursor < len(source):
        idx = _skip_ws(source, cursor)
        if idx >= len(source):
            break
        trailing = re.match(r"(label|action|destination)\s*:", source[idx:])
        if trailing:
            brace = source.find("{", idx)
            if brace < 0:
                break
            cursor = _brace_end(source, brace) + 1
            continue
        if source[idx] != ".":
            break
        name_end = idx + 1
        while name_end < len(source) and (
            source[name_end].isalnum() or source[name_end] == "_"
        ):
            name_end += 1
        name_end = _skip_ws(source, name_end)
        cursor = (
            _consume_parens(source, name_end)
            if name_end < len(source) and source[name_end] == "("
            else name_end
        )
    return cursor


def _view_with_modifiers(source: str, keyword: str) -> list[str]:
    """NavigationLink / Button expressions plus trailing closures and modifiers."""
    blocks: list[str] = []
    start = 0
    while True:
        idx = source.find(keyword, start)
        if idx < 0:
            break
        brace = source.find("{", idx)
        if brace < 0:
            break
        end = _brace_end(source, brace)
        if end < 0:
            break
        cursor = _extend_view(source, end + 1)
        blocks.append(source[idx:cursor])
        start = idx + len(keyword)
    return blocks


class NearbyIosPairingControlsTests(unittest.TestCase):
    def test_reg_02i_1_join_submit_disabled_and_one_attempt(self):
        pairing = _read(PAIRING)
        footer = _member(pairing, "footerButton")
        submit = _member(pairing, "submitJoin")
        self.assertTrue(footer, "footerButton missing")
        self.assertTrue(submit, "submitJoin missing")

        join_gate = _member(pairing, "joinSubmitDisabled") or _member(
            pairing, "isJoinSubmitEnabled"
        )
        gate_src = footer + join_gate
        self.assertIn(
            ".disabled",
            gate_src,
            "submit always enabled; 5-digit join must disable nearby_join_submit",
        )
        self.assertRegex(
            gate_src,
            r"normalizeInviteCode|joinInput",
            "5-digit / incomplete join must disable submit",
        )
        self.assertRegex(
            gate_src,
            r"joinSubmitted|joinInFlight|requestInFlight",
            "after a valid 6-digit submit the control must stay disabled in-flight",
        )

        attempt_idx = submit.find("nearbyNextJoinAttemptID")
        self.assertGreater(
            attempt_idx,
            0,
            "submitJoin must still allocate via nearbyNextJoinAttemptID",
        )
        guard = submit[:attempt_idx]
        self.assertRegex(
            guard,
            r"joinSubmitted|joinInFlight|requestInFlight",
            "two triggers must not allocate a second nearbyNextJoinAttemptID",
        )
        after_send = submit[submit.find("nearbySubmitCode") :]
        self.assertRegex(
            after_send,
            r"DispatchQueue\.main\.async",
            "discovery failure must restore on the next main turn, not stay submitting",
        )
        self.assertRegex(
            after_send,
            r"joinSubmitted\s*=\s*false",
            "after failure the same page must unlock submit for modify/retry",
        )
        self.assertIn("joinFailed = true", after_send)
        self.assertIn('"nearby_join_cancel"', pairing)
        self.assertIn("cancelJoinAttempt", pairing)

    def test_reg_02i_2_cancel_dismisses(self):
        pairing = _read(PAIRING)
        action = _member(pairing, "footerAction")
        self.assertTrue(action, "footerAction missing")
        self.assertIn(
            r"@Environment(\.dismiss)",
            pairing,
            "scan/create cancel must dismiss via Environment(\\.dismiss), not a new page",
        )
        self.assertNotRegex(
            action,
            r"case \.scan,\s*\.create",
            "scan cancel is a no-op when grouped with create without its own dismiss",
        )
        self.assertIn(
            "dismiss()",
            action,
            "scan cancel must dismiss to N00; create cancel must dismiss after revoke",
        )
        scan_case = re.search(r"case \.scan:\s*(.*?)case \.", action, re.S)
        self.assertIsNotNone(scan_case, "scan cancel needs its own footerAction branch")
        self.assertIn(
            "dismiss()",
            scan_case.group(1),
            "scan cancel must dismiss to N00",
        )
        create_case = re.search(r"case \.create:\s*(.*?)(?:case \.|}\s*$)", action, re.S)
        self.assertIsNotNone(create_case, "create cancel needs its own footerAction branch")
        create_body = create_case.group(1)
        cancel_idx = create_body.find("nearbyHostCancelGeneration")
        dismiss_idx = create_body.find("dismiss()")
        self.assertGreaterEqual(
            cancel_idx,
            0,
            "create cancel must revoke generation",
        )
        self.assertGreater(
            dismiss_idx,
            cancel_idx,
            "create cancel must revoke generation then dismiss; late callback must not re-show old invite",
        )

    def test_reg_02i_3_devices_tool_does_not_open_manage(self):
        friends = _read(FRIENDS)
        right = _member(friends, "rightPane")
        self.assertTrue(right, "rightPane missing")
        self.assertIn(
            "NearbyFriendsManageView",
            right,
            "friends tool must still open NearbyFriendsManageView",
        )
        self.assertIn(
            '"nearby_friends_manage"',
            friends,
            "friends tool keeps nearby_friends_manage",
        )

        manage_links = [
            block
            for block in _view_with_modifiers(right, "NavigationLink")
            if "NearbyFriendsManageView" in block
        ]
        self.assertTrue(manage_links, "friends NavigationLink to NearbyFriendsManageView missing")
        for block in manage_links:
            self.assertNotIn(
                "dot.radiowaves",
                block,
                "devices tool must not NavigationLink to NearbyFriendsManageView",
            )
            self.assertNotIn(
                "nearby.find_devices",
                block,
                "devices tool must not NavigationLink to NearbyFriendsManageView",
            )
            self.assertIn("nearby_friends_manage", block)

        self.assertIn(
            '"nearby_find_devices_tool"',
            friends,
            "devices tool needs a distinct accessibility id from nearby_friends_manage",
        )
        self.assertNotEqual("nearby_friends_manage", "nearby_find_devices_tool")
        devices_tool_blocks = [
            block
            for block in _view_with_modifiers(right, "Button")
            if "nearby_find_devices_tool" in block or "dot.radiowaves" in block
        ]
        self.assertTrue(
            devices_tool_blocks,
            "devices tool must trigger find-devices / unavailable reason only",
        )
        for block in devices_tool_blocks:
            self.assertNotIn("NearbyFriendsManageView", block)
            self.assertNotIn("NavigationLink", block)

        typo = _read(TYPO_PATH)
        self.assertIn("paneTitle", typo)
        self.assertIn("NearbyTypography", friends)
        self.assertIn("NearbyTypography", _read(PAIRING))


if __name__ == "__main__":
    unittest.main()
