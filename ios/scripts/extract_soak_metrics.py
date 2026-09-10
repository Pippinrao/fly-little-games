#!/usr/bin/env python3
"""Print the measured soak metrics recorded by PlaybackSoakTests.

Xcode's simulator test runners drop test stdout, so the numbers live in the
.xcresult activity log. This filters them out by running the container tool and
writing the extracted lines next to the result bundle.
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

PATTERN = re.compile(r"[\w./:= -]*soak[\w.:=, -]*", re.IGNORECASE)


def main() -> int:
    bundle = Path(sys.argv[1]).resolve()
    if not bundle.is_dir():
        raise SystemExit(f"result bundle not found: {bundle}")
    for arguments in (
        ["xcrun", "xcresulttool", "get", "object", "--legacy", "--format", "json",
         "--path", str(bundle)],
        ["xcrun", "xcresulttool", "get", "--format", "json", "--path", str(bundle)],
        ["xcrun", "xcresulttool", "get", "--legacy", "--format", "json",
         "--path", str(bundle)],
    ):
        completed = subprocess.run(arguments, stdout=subprocess.PIPE,
                                   stderr=subprocess.DEVNULL, text=True)
        if completed.returncode == 0 and completed.stdout:
            output = completed.stdout
            break
    else:
        raise SystemExit("xcresulttool could not read the result bundle")
    matches = sorted({match.strip() for match in PATTERN.findall(output)})
    if not matches:
        raise SystemExit("no soak metrics found in the result bundle")
    report = bundle.with_suffix(".soak.txt")
    report.write_text("\n".join(matches) + "\n", encoding="utf-8")
    print(report.read_text(encoding="utf-8"), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
