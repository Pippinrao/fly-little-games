#!/usr/bin/env python3
"""Run the hosted XCTest bundle through Xcode on an explicitly selected simulator."""
import copy
import datetime
import os
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
build = root / "build/ios-simulator"
project = build / "flynes_ios_product.xcodeproj"
scheme_name = sys.argv[2] if len(sys.argv) > 2 else "FlyNESRuntimeTests"
scheme = project / ("xcshareddata/xcschemes/" + scheme_name + ".xcscheme")
tree = ET.parse(scheme)
testables = tree.find("TestAction/Testables")
testables.clear()
testable = ET.SubElement(testables, "TestableReference", {"skipped": "NO"})
reference = tree.find("BuildAction/BuildActionEntries/BuildActionEntry/BuildableReference")
testable.append(copy.deepcopy(reference))
tree.write(scheme, encoding="utf-8", xml_declaration=True)
os.environ.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
result = build / "evidence" / ("tests-" + stamp + ".xcresult")
result.parent.mkdir(parents=True, exist_ok=True)
command = ["xcodebuild", "test-without-building", "-project", str(project),
           "-scheme", scheme_name, "-destination", "platform=iOS Simulator,id=" + sys.argv[1],
           "-parallel-testing-enabled", "NO", "-resultBundlePath", str(result)]
print("Result bundle:", result, flush=True)
sys.exit(subprocess.call(command))
