#!/usr/bin/env python3
"""Validate the nearby UI contract (shared/schema/nearby_ui_v1.json).

Checks, in order:
  1. Contract structure: status fields, screens, actions, stages, reasons,
     layout constants, capability tri-value, permission policy.
  2. Fixture coverage: semantic cases C01-C18 exist and reference only
     contract-known identifiers.
  3. Platform string coverage: every contract key exists in the Android,
     HarmonyOS NEXT and iOS default and Chinese resources, and the Chinese
     texts are exactly equal to the contract.

This is a static validator. It never generates successful network results,
device evidence, or pairing outcomes; absence of evidence stays absence.

Exit code 0 = all checks pass; 1 = at least one problem (all problems printed).
"""

from __future__ import annotations

import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

EXPECTED_CASE_IDS = [f"C{i:02d}" for i in range(1, 19)]
EXPECTED_STAGES = [
    "permissions", "discovery", "authentication",
    "wifi", "quic", "version", "codec",
]
EXPECTED_STATUS_FIELDS = [
    "connectionStatus", "screenId", "stage", "failureReason",
    "verifiedPeer", "actions", "pendingConfigId", "generation",
]
EXPECTED_CAPABILITY_VALUES = {"SUPPORTED", "UNSUPPORTED", "UNKNOWN"}


class Problems:
    def __init__(self) -> None:
        self.items: list[str] = []

    def add(self, message: str) -> None:
        self.items.append(message)

    def ok(self) -> bool:
        return not self.items


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


# ---------------------------------------------------------------------------
# Platform resource readers: each returns {key: zh_text}, {key: default_text}
# ---------------------------------------------------------------------------

def read_android(resources_dir: Path) -> tuple[dict, dict]:
    zh_path = resources_dir / "app/src/main/res/values-zh-rCN/strings.xml"
    default_path = resources_dir / "app/src/main/res/values/strings.xml"
    zh: dict[str, str] = {}
    for string_el in ET.parse(zh_path).getroot().findall("string"):
        name = string_el.get("name")
        if name:
            zh[name] = "".join(string_el.itertext())
    default: dict[str, str] = {}
    for string_el in ET.parse(default_path).getroot().findall("string"):
        name = string_el.get("name")
        if name:
            default[name] = "".join(string_el.itertext())
    return zh, default


def read_harmony(resources_dir: Path) -> tuple[dict, dict]:
    zh_path = resources_dir / "harmony/entry/src/main/resources/zh_CN/element/string.json"
    default_path = resources_dir / "harmony/entry/src/main/resources/base/element/string.json"

    def read(path: Path) -> dict:
        data = load_json(path)
        return {entry["name"]: entry["value"] for entry in data["string"]}

    return read(zh_path), read(default_path)


_IOS_LINE = re.compile(r'^\s*"((?:[^"\\]|\\.)*)"\s*=\s*"((?:[^"\\]|\\.)*)"\s*;\s*$')


def read_ios(resources_dir: Path) -> tuple[dict, dict]:
    def read(path: Path) -> dict:
        strings: dict[str, str] = {}
        for line in path.read_text(encoding="utf-8").splitlines():
            match = _IOS_LINE.match(line)
            if match:
                key, value = match.groups()
                strings[key.replace('\\"', '"')] = value.replace('\\"', '"')
        return strings

    zh = read(resources_dir / "ios/app/zh-Hans.lproj/Localizable.strings")
    default = read(resources_dir / "ios/app/en.lproj/Localizable.strings")
    return zh, default


# ---------------------------------------------------------------------------
# Contract checks
# ---------------------------------------------------------------------------

def referenced_strings(contract: dict) -> set[str]:
    """Every string key the contract itself references."""
    strings: dict[str, str] = contract["strings"]
    referenced: set[str] = set()

    for status in contract["connectionStatuses"].values():
        referenced.add(status["entryKey"])
    for screen in contract["screens"].values():
        referenced.add(screen["titleKey"])
        for key in screen.get("actions", []):
            action = contract["actions"][key]
            referenced.add(action["labelKey"])
        for key in screen.get("content", []) + screen.get("tabs", []):
            referenced.add(key)
        # categoryLabelAliases resolve against contract["categoryAliases"],
        # which is validated separately against the platform resources.
    referenced.add(contract["capability"]["unknownLabelKey"])
    referenced.add(contract["sas"]["labelKey"])
    for alias in contract["categoryAliases"].values():
        # aliases carry their zh text inline; the label itself is per-platform
        continue
    for permission in contract["permissions"].values():
        referenced.add(permission["deniedReasonKey"])
    return referenced


def check_contract(contract: dict, problems: Problems) -> None:
    strings: dict[str, str] = contract["strings"]

    if contract.get("statusFields") != EXPECTED_STATUS_FIELDS:
        problems.add(f"statusFields mismatch: {contract.get('statusFields')}")

    for key, text in strings.items():
        if not key.startswith("nearby.") and not key.startswith("category."):
            problems.add(f"string key outside the nearby namespace: {key}")
        if not text:
            problems.add(f"empty Chinese text for {key}")

    dangling = referenced_strings(contract) - set(strings)
    for key in sorted(dangling):
        problems.add(f"contract references missing string key: {key}")

    stages = contract["stages"]["order"]
    if stages != EXPECTED_STAGES:
        problems.add(f"stage order mismatch: {stages}")
    for stage in stages:
        if f"nearby.stage.{stage}" not in strings:
            problems.add(f"missing stage label: nearby.stage.{stage}")
        if f"nearby.stage.{stage}.failed" not in strings:
            problems.add(f"missing stage failure reason: nearby.stage.{stage}.failed")
    if "nearby.stage.timeline.invalid" not in strings:
        problems.add("missing timeline violation reason: nearby.stage.timeline.invalid")

    expected_screens = {"N00", "N01", "N02", "N03", "N04", "N05", "N06",
                        "N07", "N08", "G00", "N09", "N10", "N11", "N12"}
    if set(contract["screens"]) != expected_screens:
        problems.add(f"screen set mismatch: {sorted(contract['screens'])}")
    if contract["screens"]["G00"]["categories"] != ["RECENT", "FAVORITES", "ALL", "BUILTIN"]:
        problems.add("G00 must keep the four original categories (U03)")

    if set(contract["capability"]["values"]) != EXPECTED_CAPABILITY_VALUES:
        problems.add("capability must be the SUPPORTED/UNSUPPORTED/UNKNOWN tri-value")
    if not contract["capability"]["unknownIsNotUnsupported"]:
        problems.add("capability UNKNOWN must not equal UNSUPPORTED (design 3.2)")

    layout = contract["layout"]
    for constant in ("topBarBasisDp", "nearbySplitMinWidthDp", "nearbyLeftColumnWidthDp",
                     "nearbyColumnGutterDp", "actionMinHitDp", "bottomActionMinButtonWidthDp"):
        if constant not in layout or not isinstance(layout[constant], (int, float)):
            problems.add(f"layout constant missing: {constant}")
    if layout.get("bottomActionFullWidthForbidden") is not True:
        problems.add("layout must forbid full-width bottom action buttons (design 6)")
    if layout.get("horizontalScrollAllowedOnlyFor") != ["lobbyGameGrid"]:
        problems.add("horizontal scroll must be limited to the lobby game grid")
    if layout.get("fontScales") != [1.0, 1.3, 2.0]:
        problems.add("font scales must be 1.0/1.3/2.0")
    if abs(layout["lobbyDetailWidthRatio"] - 0.3) > 1e-9 or abs(layout["lobbyGridWidthRatio"] - 0.7) > 1e-9:
        problems.add("lobby split must stay 30% detail / 70% grid (U01)")

    if contract["invite"]["codeLength"] != 6 or contract["invite"]["validitySeconds"] != 60:
        problems.add("invite must be 6 digits with the 60s continuous clock")
    if not contract["invite"]["leadingZerosPreserved"]:
        problems.add("invite codes must preserve leading zeros (C05)")
    if not contract["sas"]["singleSideConfirmInsufficient"]:
        problems.add("single-side SAS confirmation must be insufficient (C07)")

    containers = contract.get("containers")
    if not isinstance(containers, dict):
        problems.add("UX-00.S containers table missing")
    else:
        expected_pair = ["N01", "N02", "N03", "N04", "N05", "N06", "N07"]
        if containers.get("PAIR") != expected_pair:
            problems.add("PAIR container must host N01–N07 and no other screens")
        seen: list[str] = []
        for name, screens in containers.items():
            if not isinstance(screens, list):
                problems.add(f"container {name} must list screens")
                continue
            seen.extend(screens)
        if sorted(seen) != sorted(expected_screens):
            problems.add(f"every N00–N12/G00 screen must belong to exactly one container, got {seen}")

    navigation = contract.get("navigation")
    if not isinstance(navigation, list) or not navigation:
        problems.add("UX-00.S navigation table missing")
    else:
        required = {
            ("N00", "createInvite", "N01"),
            ("N00", "enterInviteCode", "N02"),
            ("N00", "scanQr", "N03"),
            ("G00", "selectGame", "N09"),
            ("N09", "backToLobby", "G00"),
        }
        found = {(row.get("from"), row.get("action"), row.get("to")) for row in navigation}
        for edge in required:
            if edge not in found:
                problems.add(f"UX-00.S missing navigation {edge[0]}.{edge[1]} → {edge[2]}")
        for row in navigation:
            if row.get("from") not in expected_screens or row.get("to") not in expected_screens:
                problems.add(f"navigation row has unknown screen: {row}")
            if row.get("action") not in contract["actions"]:
                problems.add(f"navigation row has unknown action: {row.get('action')}")

    for name in ("camera", "nearbyDiscovery", "wifi"):
        permission = contract["permissions"].get(name)
        if permission is None:
            problems.add(f"missing permission policy: {name}")
        elif permission.get("repeatPrompt") is not False:
            problems.add(f"permission {name} must not repeat the system prompt (C10)")


def check_fixture(contract: dict, fixture: dict, problems: Problems) -> None:
    strings: dict[str, str] = contract["strings"]
    screens = set(contract["screens"])
    actions = set(contract["actions"])

    ids = [case["id"] for case in fixture["cases"]]
    if sorted(ids) != EXPECTED_CASE_IDS:
        problems.add(f"fixture case ids must be exactly C01-C18, got {sorted(ids)}")
        return

    for case in fixture["cases"]:
        case_id = case["id"]
        expected = case.get("expected", {})
        if "screenId" in expected and expected["screenId"] not in screens:
            problems.add(f"{case_id}: unknown screenId {expected['screenId']}")
        for key in ("entryKey", "reasonKey", "zeroResultActionLabelKey"):
            if key in expected and expected[key] not in strings:
                problems.add(f"{case_id}: {key} {expected[key]} not in contract strings")
        for key in ("actionsPresent",):
            for action in expected.get(key, []):
                if action not in actions:
                    problems.add(f"{case_id}: unknown action {action}")
        if expected.get("zeroResultAction") not in (None, "disableMultiplayerFilter"):
            problems.add(f"{case_id}: zero-result action must be disableMultiplayerFilter")
        if "entryTextZh" in expected:
            entry_key = expected.get("entryKey")
            if entry_key and strings.get(entry_key) != expected["entryTextZh"]:
                problems.add(
                    f"{case_id}: entryTextZh {expected['entryTextZh']!r} does not match "
                    f"contract text {strings.get(entry_key)!r} for {entry_key}")


def platform_resource_name(platform: str, key: str) -> str:
    """Canonical contract key -> platform resource entry name.

    Android resource names and HarmonyOS $r('string.*') names cannot contain
    dots, so both platforms map '.' to '_'. iOS .strings keys are opaque and
    keep the canonical form.
    """
    return key.replace(".", "_") if platform in ("android", "harmony") else key


def resource_name(contract: dict, platform: str, key: str) -> str:
    overrides = contract.get("platformResourceNameOverrides", {})
    override = overrides.get(key, {})
    return override.get(platform, platform_resource_name(platform, key))


def check_platform_strings(contract: dict, root: Path, problems: Problems) -> None:
    android_zh, android_default = read_android(root)
    harmony_zh, harmony_default = read_harmony(root)
    ios_zh, ios_default = read_ios(root)

    platforms = [
        ("android", android_zh, android_default),
        ("harmony", harmony_zh, harmony_default),
        ("ios", ios_zh, ios_default),
    ]

    for key, expected_zh in contract["strings"].items():
        for name, zh, default in platforms:
            resource_key = resource_name(contract, name, key)
            if resource_key not in default:
                problems.add(f"[{name}] missing default resource key: {resource_key}")
            if resource_key not in zh:
                problems.add(f"[{name}] missing zh resource key: {resource_key}")
            elif zh[resource_key] != expected_zh:
                problems.add(
                    f"[{name}] zh text mismatch for {resource_key}: "
                    f"{zh[resource_key]!r} != contract {expected_zh!r}")

    for alias_key, alias in contract["categoryAliases"].items():
        for name, zh, default in platforms:
            platform_key = alias[name]
            if platform_key not in default:
                problems.add(f"[{name}] missing default category alias: {platform_key}")
            if platform_key not in zh:
                problems.add(f"[{name}] missing zh category alias: {platform_key}")
            elif zh[platform_key] != alias["zh"]:
                problems.add(
                    f"[{name}] category alias {platform_key} is {zh[platform_key]!r}, "
                    f"expected {alias['zh']!r}")


def main(argv: list[str]) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else Path(__file__).resolve().parents[2]
    contract_path = root / "shared/schema/nearby_ui_v1.json"
    fixture_path = root / "shared/tests/fixtures/nearby_ui_v1/cases.json"

    problems = Problems()
    try:
        contract = load_json(contract_path)
    except (OSError, json.JSONDecodeError) as error:
        print(f"FAIL: cannot read contract {contract_path}: {error}")
        return 1

    if contract.get("contractId") != "flynes-nearby-ui-v1":
        problems.add("contractId must be flynes-nearby-ui-v1")

    check_contract(contract, problems)

    try:
        fixture = load_json(fixture_path)
    except (OSError, json.JSONDecodeError) as error:
        problems.add(f"cannot read fixture {fixture_path}: {error}")
    else:
        check_fixture(contract, fixture, problems)

    check_platform_strings(contract, root, problems)

    if problems.ok():
        keys = len(contract["strings"])
        print(f"OK: nearby UI contract valid; {keys} strings covered on android/harmony/ios; "
              f"C01-C18 fixture present; no network results generated.")
        return 0

    for problem in problems.items:
        print(f"FAIL: {problem}")
    print(f"{len(problems.items)} contract problem(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
