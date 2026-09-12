#!/usr/bin/env python3
"""Compatibility entry point: all platforms use the shared approved master."""

from pathlib import Path
import runpy

if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).resolve().parents[1] / "branding/generate_app_icons.py"),
                  run_name="__main__")
