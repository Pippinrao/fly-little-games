#!/usr/bin/env python3
"""Run one Stage-1 CI command with a portable wall-clock timeout."""

from __future__ import annotations

import argparse
import subprocess
import sys


MAX_TIMEOUT_SECONDS = 120.0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout-seconds", required=True, type=float)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = list(args.command)
    if command and command[0] == "--":
        command.pop(0)
    if not 0.0 < args.timeout_seconds <= MAX_TIMEOUT_SECONDS:
        parser.error(f"--timeout-seconds must be in (0, {MAX_TIMEOUT_SECONDS:g}]")
    if not command:
        parser.error("a command is required after --")
    try:
        completed = subprocess.run(command, check=False, timeout=args.timeout_seconds)
    except subprocess.TimeoutExpired:
        print(
            f"run_with_timeout: command exceeded {args.timeout_seconds:g} seconds",
            file=sys.stderr,
        )
        return 124
    except OSError as error:
        print(f"run_with_timeout: could not execute command: {error}", file=sys.stderr)
        return 127
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
