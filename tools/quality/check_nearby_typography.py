#!/usr/bin/env python3
"""Validate the nearby typography freeze (shared/schema/nearby_typography_v1.json).

Numeric values are CSS target line boxes at default scale. CSS px, Android sp,
Harmony fp, and iOS pt are not the same physical unit. This checker does not
globally scale sizes and does not treat copied JSON constants as native
measurements.

Exit code 0 = all checks pass; 1 = at least one problem (all problems printed).
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

CONTRACT_RELATIVE = Path("shared/schema/nearby_typography_v1.json")
CONTRACT_ID = "flynes-nearby-typography-v1"

# Approved CSS px at default scale. Do not collapse title/muted/button into body.
APPROVED_ROLES = {
    "pageTitle": {
        "size": 18,
        "weight": 600,
        "lineHeight": 23.4,
        "tracking": 0,
        "aliases": ("pageTitle",),
    },
    "paneTitle": {
        "size": 21,
        "weight": 600,
        "lineHeight": 27.3,
        "tracking": 0,
        "aliases": ("paneTitle", "headline"),
    },
    "sectionTitle": {
        "size": 15,
        "weight": 600,
        "lineHeight": 21.75,
        "tracking": 0,
        "aliases": ("sectionTitle",),
    },
    "body": {
        "size": 14,
        "weight": 400,
        "lineHeight": 20.3,
        "tracking": 0,
        "aliases": ("body",),
    },
    "muted": {
        "size": 12,
        "weight": 400,
        "lineHeight": 17.4,
        "tracking": 0,
        "aliases": ("muted", "subtitle"),
    },
    "action": {
        "size": 14,
        "weight": 400,
        "lineHeight": 20.3,
        "tracking": 0,
        "aliases": ("action", "button"),
    },
    "primaryAction": {
        "size": 14,
        "weight": 600,
        "lineHeight": 20.3,
        "tracking": 0,
        "aliases": ("primaryAction",),
    },
    "kicker": {
        "size": 11,
        "weight": 400,
        "lineHeight": 15.95,
        "tracking": 0.13,
        "aliases": ("kicker",),
    },
    "inviteCode": {
        "size": 29,
        "weight": 600,
        "lineHeight": 40.6,
        "tracking": 0.17,
        "aliases": ("inviteCode",),
    },
    "codeInput": {
        "size": 28,
        "weight": 400,
        "lineHeight": 40.6,
        "tracking": 0.16,
        "aliases": ("codeInput",),
    },
}

ROLE_FIELDS = ("size", "weight", "lineHeight", "tracking", "unit", "selector", "nodes")

# Local mockup overrides keyed by selector. Weight is recorded only when the
# mockup sets it; missing weight is not guessed.
APPROVED_OVERRIDES = {
    "#fly-existing .fe-detail-title h3": {"fontSize": 20},
    "#fly-existing .fe-filter": {"fontSize": 13},
    "#fly-existing .fe-game-copy strong": {"fontSize": 14, "weight": 600},
    "#fly-existing .fe-game-copy small": {"fontSize": 11},
    "#fly-existing .fe-thumb": {"fontSize": 11, "weight": 600},
    "#fly-existing .fe-count": {"fontSize": 12},
    "#fly-existing .fe-list-hint": {"fontSize": 11},
    "#fly-existing .fe-search input": {"fontSize": 16},
}

MEASUREMENT_REQUIRED = (
    "platform",
    "scaleMode",
    "requestedFontScale",
    "appliedFontScale",
    "effectiveFontSizeLogical",
    "sourceFingerprint",
)

NATIVE_MEASUREMENT_REQUIRED = (
    "osVersion",
    "density",
    "displayScale",
    "packageHash",
    "nativeBounds",
    "screenshotPath",
    "fontFamily",
    "contentSize",
    "safeArea",
)

SIZE_EPS = 1e-9


class Problems:
    def __init__(self) -> None:
        self.items: list[str] = []

    def add(self, message: str) -> None:
        self.items.append(message)

    def ok(self) -> bool:
        return not self.items


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def parse_tracking(value) -> float | None:
    if value is None:
        return None
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return float(value)
    text = str(value).strip().lower().replace("em", "").strip()
    try:
        return float(text)
    except ValueError:
        return None


def format_tracking(value: float) -> str:
    if abs(value) < SIZE_EPS:
        return "0"
    return f".{str(value).split('.', 1)[1]}em" if 0 < value < 1 else f"{value}em"


def numbers_equal(actual, expected: float) -> bool:
    parsed = parse_tracking(actual) if not isinstance(actual, (int, float)) else float(actual)
    if parsed is None:
        return False
    return abs(parsed - expected) < SIZE_EPS


def role_label(name: str) -> str:
    aliases = APPROVED_ROLES[name]["aliases"]
    if len(aliases) == 1:
        return name
    extra = [alias for alias in aliases if alias != name]
    return f"{name} ({'/'.join(extra)})"


def check_units(contract: dict, problems: Problems) -> None:
    blob = json.dumps(contract.get("units", {})).lower()
    required_tokens = ("css-px", "android-sp", "harmony-fp", "ios-pt")
    missing = [token for token in required_tokens if token not in blob.replace("_", "-")]
    if missing:
        problems.add(
            "units must record that CSS px, Android sp, Harmony fp, and iOS pt "
            f"are not the same physical unit; missing {missing}"
        )


def check_role(name: str, role: dict, problems: Problems) -> None:
    approved = APPROVED_ROLES[name]
    label = role_label(name)
    if not isinstance(role, dict):
        problems.add(f"{label} must be an object")
        return
    for field in ROLE_FIELDS:
        if field not in role or role[field] in (None, "", []):
            problems.add(f"{label} missing {field}")
    if "size" in role and not numbers_equal(role["size"], approved["size"]):
        problems.add(
            f"{label} size {role['size']!r} does not match approved "
            f"{approved['size']} css-px"
        )
    if "weight" in role and role["weight"] != approved["weight"]:
        problems.add(
            f"{label} weight {role['weight']!r} does not match approved "
            f"{approved['weight']}"
        )
    if "lineHeight" in role and not numbers_equal(role["lineHeight"], approved["lineHeight"]):
        problems.add(
            f"{label} lineHeight {role['lineHeight']!r} does not match approved "
            f"{approved['lineHeight']} css-px line box"
        )
    if "tracking" in role:
        actual = parse_tracking(role["tracking"])
        expected = approved["tracking"]
        if actual is None or abs(actual - expected) >= SIZE_EPS:
            problems.add(
                f"{label} tracking {role['tracking']!r} does not match approved "
                f"{format_tracking(expected)}"
            )


def check_overrides(contract: dict, problems: Problems) -> None:
    overrides = contract.get("overrides")
    if not isinstance(overrides, dict):
        problems.add("overrides must be an object keyed by CSS selector")
        return
    for selector, expected in APPROVED_OVERRIDES.items():
        actual = overrides.get(selector)
        if not isinstance(actual, dict):
            problems.add(f"override missing for {selector}")
            continue
        actual_size = actual.get("fontSize", actual.get("size"))
        if not numbers_equal(actual_size, expected["fontSize"]):
            problems.add(
                f"override {selector} fontSize {actual_size!r} does not match "
                f"approved {expected['fontSize']}"
            )
        if "weight" in expected:
            if actual.get("weight") != expected["weight"]:
                problems.add(
                    f"override {selector} weight {actual.get('weight')!r} does not "
                    f"match approved {expected['weight']}"
                )
        elif "weight" in actual:
            problems.add(
                f"override {selector} must not guess weight {actual['weight']!r}"
            )


def check_contract(contract: dict, problems: Problems) -> None:
    if contract.get("contractId") != CONTRACT_ID:
        problems.add(f"contractId must be {CONTRACT_ID}")
    check_units(contract, problems)
    roles = contract.get("roles")
    if not isinstance(roles, dict):
        problems.add("roles must be an object with separately named tokens")
        return
    if set(roles.keys()) == {"body"}:
        problems.add("do not use one body token for title/muted/button")
    for name in APPROVED_ROLES:
        if name not in roles:
            problems.add(f"missing role {role_label(name)}")
            continue
        check_role(name, roles[name], problems)
    check_overrides(contract, problems)
    measurements = contract.get("measurements")
    if measurements:
        if not isinstance(measurements, list):
            problems.add("measurements must be a list when supplied")
        else:
            for index, record in enumerate(measurements):
                prefix = Problems()
                check_measurement(record, prefix)
                for item in prefix.items:
                    problems.add(f"measurements[{index}]: {item}")


def _empty(value) -> bool:
    return value in (None, "", [], {}, ())


def check_measurement(record: dict, problems: Problems) -> None:
    """Reject copied contract numbers presented as native measurements."""
    if not isinstance(record, dict):
        problems.add("measurement must be an object")
        return
    for field in MEASUREMENT_REQUIRED:
        if field not in record or _empty(record[field]):
            problems.add(f"measurement missing {field}")
    for field in NATIVE_MEASUREMENT_REQUIRED:
        if field not in record or _empty(record[field]):
            problems.add(f"measurement missing native {field}")
    if record.get("platform") == "ios" and _empty(record.get("iosDynamicTypeCategory")):
        problems.add("measurement missing native iosDynamicTypeCategory")

    role_name = record.get("role")
    approved = APPROVED_ROLES.get(role_name) if isinstance(role_name, str) else None
    copied_size = (
        approved is not None
        and numbers_equal(record.get("baseFontSize"), approved["size"])
        and numbers_equal(record.get("effectiveFontSizeLogical"), approved["size"])
        and record.get("weight") == approved["weight"]
    )
    missing_native = any(
        field not in record or _empty(record[field])
        for field in ("platform", "nativeBounds", "screenshotPath", "sourceFingerprint")
    )
    if copied_size and missing_native:
        problems.add(
            "copied JSON constants are not a native measurement; require platform "
            "metadata, native bounds, screenshotPath, and source fingerprint"
        )


def main(argv: list[str]) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else Path(__file__).resolve().parents[2]
    contract_path = root / CONTRACT_RELATIVE
    problems = Problems()
    if not contract_path.is_file():
        print(f"FAIL: typography contract missing: {CONTRACT_RELATIVE.as_posix()}")
        return 1
    try:
        contract = load_json(contract_path)
    except (OSError, json.JSONDecodeError) as error:
        print(f"FAIL: cannot read typography contract {contract_path}: {error}")
        return 1

    check_contract(contract, problems)

    if problems.ok():
        print(
            f"OK: nearby typography contract valid; {len(APPROVED_ROLES)} roles "
            "frozen at CSS px default scale; no native measurements claimed."
        )
        return 0

    for problem in problems.items:
        print(f"FAIL: {problem}")
    print(f"{len(problems.items)} typography problem(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
