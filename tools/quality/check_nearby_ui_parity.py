#!/usr/bin/env python3
"""Cross-platform parity checker for the nearby UI contract (plan P7).

Consumes ONLY the native evidence JSON files that each platform's UI test run
exports into the evidence directory. It never generates device results, never
fakes a missing platform, and never accepts HTML review artifacts as
substitutes for native runs.

Expected files, one JSON object per platform per case group, named:
    nearby-parity-<platform>.json      platform in {android, harmony, ios}
containing a list of records shaped like:

    {
      "caseId": "C09-connected",
      "platform": "android",
      "locale": "zh-CN",
      "contentSize": [736, 414],
      "fontScale": 1.0,
      "screenId": "G00",
      "surfaceId": "G00",
      "orientation": "landscape",
      "safeAreaVariant": "none",
      "entryText": "双人联机中",
      "category": "FAVORITES",
      "multiplayerOnly": true,
      "visibleIds": ["fixture-a", "fixture-d"],
      "enabledActions": ["selectGame", "manageConnection"],
      "reasons": [],
      "containerOrder": ["topBar", "statusRow", "detail", "grid"],
      "overflowOutsideGameGrid": false,
      "bounds": {"topBar": [0, 0, 736, 64]}
    }

Checks:
  1. Grouping: records group by case, surface, locale and the complete native
     layout variant (size, orientation, font, safe area and keyboard), and
     every group must contain exactly android, harmony and ios. A missing
     platform FAILs the group - it is never skipped.
  2. Semantics: screenId, entryText, category, multiplayerOnly, visibleIds,
     enabledActions (order-sensitive), reasons and containerOrder must match
     across the three platforms.
  3. Layout: overflowOutsideGameGrid must be false everywhere (only the lobby
     game grid may scroll horizontally). When the same bounds key is present on
     all three platforms, x/y/w/h may differ by at most 2 logical units.

Exit 0 only with 0 missing groups, 0 semantic mismatches and 0 layout
violations.
"""

from __future__ import annotations

import json
import math
import sys
from collections import defaultdict
from itertools import product
from pathlib import Path

PLATFORMS = ("android", "harmony", "ios")
CASE_IDS = tuple(f"C{index:02d}" for index in range(1, 19))
SEMANTIC_FIELDS = ("screenId", "entryText", "category", "multiplayerOnly",
                   "visibleIds", "enabledActions", "reasons", "containerOrder")
REQUIRED_FIELDS = (
    "caseId", "platform", "locale", "contentSize", "fontScale", "screenId", "surfaceId",
    "entryText", "category", "multiplayerOnly", "visibleIds", "enabledActions", "reasons",
    "containerOrder", "overflowOutsideGameGrid", "bounds", "screenshotPath",
    "evidenceKind", "safeInsets", "safeAreaVariant", "orientation", "keyboardVisible",
)
BOUNDS_TOLERANCE = 2.0
C17_WIDTHS = (320, 375, 550, 580, 640, 736, 900, 1024)
C18_SIZES = ((640, 360), (736, 414), (844, 390))
C18_FONT_SCALES = (1.0, 1.3, 2.0)
LOCALE_FAMILIES = ("zh", "en")
SAFE_AREA_VARIANTS = ("none", "horizontal-notch")
REQUIRED_SURFACES = (
    "G00", *(f"N{index:02d}" for index in range(13)),
    "dialog.game-ready", "dialog.diagnostics", "dialog.content-details",
    "dialog.file-consent", "dialog.file-wait", "dialog.file-progress",
    "dialog.file-import", "dialog.manage-friends", "dialog.friend-actions",
    "dialog.rename-friend", "dialog.delete-friend", "dialog.block-friend",
    "dialog.identity-reset", "dialog.renew-invite", "dialog.discover",
    "dialog.permission-denied", "dialog.connection", "dialog.pair",
    "dialog.disconnect", "dialog.retry", "dialog.launch",
)


class Problems:
    def __init__(self) -> None:
        self.missing = 0
        self.semantic = 0
        self.layout = 0

    def add_missing(self, message: str) -> None:
        self.missing += 1
        print(f"FAIL: missing: {message}")

    def add_semantic(self, message: str) -> None:
        self.semantic += 1
        print(f"FAIL: semantic: {message}")

    def add_layout(self, message: str) -> None:
        self.layout += 1
        print(f"FAIL: layout: {message}")

    def ok(self) -> bool:
        return self.missing == 0 and self.semantic == 0 and self.layout == 0


def load_records(evidence_dir: Path, problems: Problems) -> list[dict]:
    records: list[dict] = []
    for platform in PLATFORMS:
        path = evidence_dir / f"nearby-parity-{platform}.json"
        if not path.exists():
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, list):
            raise ValueError(f"{path} must contain a list of records")
        for record in data:
            if not isinstance(record, dict):
                problems.add_semantic(f"{path.name}: every record must be an object")
                continue
            declared = record.get("platform")
            if declared != platform:
                problems.add_semantic(
                    f"{path.name}: record declares platform {declared!r}, expected {platform!r}")
            record = dict(record)
            record["platform"] = platform
            records.append(record)
    return records


def _finite_number(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) \
        and math.isfinite(float(value))


def _locale_family(locale: object) -> str | None:
    if not isinstance(locale, str):
        return None
    lowered = locale.lower()
    return next((family for family in LOCALE_FAMILIES if lowered.startswith(family)), None)


def validate_record(record: dict, evidence_dir: Path, problems: Problems) -> None:
    platform = record.get("platform")
    for field in REQUIRED_FIELDS:
        if field not in record:
            problems.add_semantic(f"{platform}: record missing required field '{field}'")

    case_id = record.get("caseId")
    if not isinstance(case_id, str) or case_id[:3] not in CASE_IDS:
        problems.add_semantic(f"{platform}: invalid caseId {case_id!r}")
    if _locale_family(record.get("locale")) is None:
        problems.add_semantic(f"{platform}: locale must identify Chinese or English")
    surface = record.get("surfaceId")
    if surface not in REQUIRED_SURFACES:
        problems.add_semantic(f"{platform}: unknown surfaceId {surface!r}")
    if record.get("orientation") not in ("landscape", "portrait"):
        problems.add_semantic(f"{platform}: orientation must be landscape or portrait")
    if record.get("safeAreaVariant") not in SAFE_AREA_VARIANTS:
        problems.add_semantic(
            f"{platform}: safeAreaVariant must be one of {SAFE_AREA_VARIANTS}")

    size = record.get("contentSize")
    if not (isinstance(size, list) and len(size) == 2
            and all(_finite_number(value) and value > 0 for value in size)):
        problems.add_layout(f"{platform}: contentSize must be two positive finite numbers")
    scale = record.get("fontScale")
    if not _finite_number(scale) or scale <= 0:
        problems.add_layout(f"{platform}: fontScale must be a positive finite number")

    for field in ("multiplayerOnly", "overflowOutsideGameGrid", "keyboardVisible"):
        if field in record and not isinstance(record[field], bool):
            problems.add_semantic(f"{platform}: '{field}' must be boolean")
    for field in ("visibleIds", "enabledActions", "reasons", "containerOrder"):
        if field in record and not isinstance(record[field], list):
            problems.add_semantic(f"{platform}: '{field}' must be a list")

    insets = record.get("safeInsets")
    if not (isinstance(insets, list) and len(insets) == 4
            and all(_finite_number(value) and value >= 0 for value in insets)):
        problems.add_layout(f"{platform}: safeInsets must be four non-negative finite numbers")

    bounds = record.get("bounds")
    if not isinstance(bounds, dict) or not bounds:
        problems.add_layout(f"{platform}: bounds must be a non-empty object")
    else:
        for name, rect in bounds.items():
            if not (isinstance(name, str) and name and isinstance(rect, list)
                    and len(rect) == 4 and all(_finite_number(value) for value in rect)
                    and rect[2] >= 0 and rect[3] >= 0):
                problems.add_layout(f"{platform}: bounds '{name}' is not a valid [x,y,w,h] rect")

    if record.get("evidenceKind") != "native":
        problems.add_semantic(f"{platform}: evidenceKind must be 'native'")
    screenshot = record.get("screenshotPath")
    if not isinstance(screenshot, str) or not screenshot:
        problems.add_missing(f"{platform}: screenshotPath is empty")
    else:
        root = evidence_dir.resolve()
        candidate = (evidence_dir / screenshot).resolve()
        if candidate.suffix.lower() != ".png" or root not in candidate.parents:
            problems.add_semantic(
                f"{platform}: screenshotPath must be a relative PNG inside the evidence directory")
        elif not candidate.is_file():
            problems.add_missing(f"{platform}: screenshot does not exist: {screenshot}")


def validate_required_matrices(records: list[dict], problems: Problems) -> None:
    c17 = {
        (record.get("surfaceId"), _locale_family(record.get("locale")),
         int(record["contentSize"][0]), record.get("orientation"))
        for record in records
        if str(record.get("caseId", "")).startswith("C17")
        and isinstance(record.get("contentSize"), list)
        and len(record["contentSize"]) == 2
        and _finite_number(record["contentSize"][0])
    }
    for surface, locale, width in product(REQUIRED_SURFACES, LOCALE_FAMILIES, C17_WIDTHS):
        if (surface, locale, width, "landscape") not in c17:
            problems.add_missing(
                f"C17 required surface {surface}, width {width}, locale {locale}, "
                "orientation=landscape has no native evidence")

    c18 = {
        (record.get("surfaceId"), _locale_family(record.get("locale")),
         tuple(record["contentSize"]), float(record["fontScale"]),
         record.get("safeAreaVariant"), record.get("keyboardVisible"),
         record.get("orientation"))
        for record in records
        if str(record.get("caseId", "")).startswith("C18")
        and isinstance(record.get("contentSize"), list)
        and len(record["contentSize"]) == 2
        and _finite_number(record.get("fontScale"))
    }
    for surface, locale, size, scale, safe_area, keyboard in product(
            REQUIRED_SURFACES, LOCALE_FAMILIES, C18_SIZES, C18_FONT_SCALES,
            SAFE_AREA_VARIANTS, (False, True)):
        if (surface, locale, size, scale, safe_area, keyboard, "landscape") not in c18:
            problems.add_missing(
                "C18 required matrix "
                f"surface={surface}, locale={locale}, contentSize={list(size)}, "
                f"fontScale={scale}, safeAreaVariant={safe_area}, "
                f"keyboardVisible={keyboard}, orientation=landscape has no native evidence")


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: check_nearby_ui_parity.py <evidence-dir>")
        return 2
    evidence_dir = Path(argv[1])
    if not evidence_dir.is_dir():
        print(f"FAIL: evidence directory does not exist: {evidence_dir}")
        return 1

    problems = Problems()
    try:
        records = load_records(evidence_dir, problems)
    except (ValueError, json.JSONDecodeError) as error:
        print(f"FAIL: cannot read evidence: {error}")
        return 1

    if not records:
        for platform in PLATFORMS:
            problems.add_missing(f"no native evidence file for {platform} "
                                 f"(nearby-parity-{platform}.json)")
        print(f"{problems.missing} missing, 0 semantic mismatch, 0 layout violations")
        return 1

    groups: dict[tuple, dict[str, dict]] = defaultdict(dict)
    for record in records:
        validate_record(record, evidence_dir, problems)
        key = (record.get("caseId"), record.get("surfaceId"), record.get("locale"),
               tuple(record.get("contentSize", ())), record.get("orientation"),
               record.get("fontScale"), record.get("safeAreaVariant"),
               record.get("keyboardVisible"))
        platform = record.get("platform")
        if platform in groups[key]:
            problems.add_semantic(f"{key}: duplicate record for {platform}")
        groups[key][platform] = record

    present_case_ids = {
        str(record.get("caseId", ""))[:3]
        for record in records
        if str(record.get("caseId", ""))[:3] in CASE_IDS
    }
    for case_id in CASE_IDS:
        if case_id not in present_case_ids:
            problems.add_missing(f"required case {case_id} has no native evidence")
    validate_required_matrices(records, problems)

    for key, by_platform in sorted(groups.items(), key=lambda item: str(item[0])):
        case_id = key[0]
        for platform in PLATFORMS:
            if platform not in by_platform:
                problems.add_missing(f"case {key}: platform {platform} has no record")

        present = [p for p in PLATFORMS if p in by_platform]
        if len(present) < 2:
            continue
        reference = by_platform[present[0]]
        for field in SEMANTIC_FIELDS:
            values = {p: by_platform[p].get(field) for p in present}
            if len({json.dumps(v, sort_keys=True) for v in values.values()}) > 1:
                problems.add_semantic(f"case {key}: '{field}' differs: {values}")

        # Bounds: only compare when every platform reported the same key.
        if len(present) == len(PLATFORMS):
            bounds_maps = [by_platform[p].get("bounds", {}) for p in present]
            key_sets = [set(bounds) if isinstance(bounds, dict) else set()
                        for bounds in bounds_maps]
            if any(keys != key_sets[0] for keys in key_sets[1:]):
                problems.add_layout(
                    f"case {key}: bounds keys differ: "
                    f"{dict(zip(present, (sorted(keys) for keys in key_sets)))}")
            for bounds_key in sorted(set.intersection(*key_sets)):
                per_platform = [b[bounds_key] for b in bounds_maps]
                if not all(isinstance(value, list) and len(value) == 4
                           and all(_finite_number(axis) for axis in value)
                           for value in per_platform):
                    continue
                origin = per_platform[0]
                for platform, value in zip(present[1:], per_platform[1:]):
                    for axis in range(4):
                        if abs(origin[axis] - value[axis]) > BOUNDS_TOLERANCE:
                            problems.add_layout(
                                f"case {key}: bounds '{bounds_key}' axis {axis} differs "
                                f"by {abs(origin[axis] - value[axis]):.1f} > "
                                f"{BOUNDS_TOLERANCE} logical units "
                                f"({present[0]}={origin}, {platform}={value})")
                            break

        for platform, record in by_platform.items():
            if record.get("overflowOutsideGameGrid") is True:
                problems.add_layout(
                    f"case {key}: {platform} reports horizontal overflow outside "
                    f"the game grid")

    print(f"{problems.missing} missing, {problems.semantic} semantic mismatch, "
          f"{problems.layout} layout violations; {len(groups)} case group(s)")
    return 0 if problems.ok() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
