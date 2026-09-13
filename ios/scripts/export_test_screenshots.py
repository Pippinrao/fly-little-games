#!/usr/bin/env python3
import json
import os
from pathlib import Path
import subprocess
import sys
os.environ.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
result = sys.argv[1]
output = Path(sys.argv[2]); output.mkdir(parents=True, exist_ok=True)
seen = set()
def fetch(identifier=None):
    args = ["xcrun", "xcresulttool", "get", "--path", result, "--format", "json"]
    if identifier: args += ["--id", identifier]
    return json.loads(subprocess.check_output(args))
def walk(node):
    if isinstance(node, list):
        for value in node: walk(value)
    elif isinstance(node, dict):
        name = node.get("name", {}).get("_value", "")
        if name == "gameplay" and "payloadRef" in node:
            identifier = node["payloadRef"]["id"]["_value"]
            path = output / "gameplay.png"
            subprocess.check_call(["xcrun", "xcresulttool", "export", "--path", result,
                "--id", identifier, "--type", "file", "--output-path", str(path)])
            print(path)
        for key, value in node.items():
            if key in ("testsRef", "summaryRef"):
                identifier = value["id"]["_value"]
                if identifier not in seen:
                    seen.add(identifier); walk(fetch(identifier))
            else: walk(value)
walk(fetch())
