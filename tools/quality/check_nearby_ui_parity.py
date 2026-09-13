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
  1. Grouping: records group by (caseId, locale, contentSize, fontScale) and
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
import sys
from collections import defaultdict
from pathlib import Path

PLATFORMS = ("android", "harmony", "ios")
SEMANTIC_FIELDS = ("screenId", "entryText", "category", "multiplayerOnly",
                   "visibleIds", "enabledActions", "reasons", "containerOrder")
REQUIRED_FIELDS = ("caseId", "platform", "locale", "screenId") + SEMANTIC_FIELDS[3:]
BOUNDS_TOLERANCE = 2.0


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


def load_records(evidence_dir: Path) -> list[dict]:
    records: list[dict] = []
    for platform in PLATFORMS:
        path = evidence_dir / f"nearby-parity-{platform}.json"
        if not path.exists():
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, list):
            raise ValueError(f"{path} must contain a list of records")
        for record in data:
            record["platform"] = platform
            records.append(record)
    return records


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
        records = load_records(evidence_dir)
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
        for field in REQUIRED_FIELDS:
            if field not in record:
                problems.add_semantic(f"{record.get('platform')}: record missing "
                                      f"required field '{field}'")
                break
        key = (record.get("caseId"), record.get("locale"),
               tuple(record.get("contentSize", ())), record.get("fontScale"))
        platform = record.get("platform")
        if platform in groups[key]:
            problems.add_semantic(f"{key}: duplicate record for {platform}")
        groups[key][platform] = record

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
            shared_keys = set.intersection(*(set(b) for b in bounds_maps))
            for bounds_key in sorted(shared_keys):
                per_platform = [b[bounds_key] for b in bounds_maps]
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
