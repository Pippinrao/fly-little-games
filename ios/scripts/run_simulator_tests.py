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
os.environ.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
if scheme_name == "FlyNESUITests":
    subprocess.run(["xcrun", "simctl", "install", sys.argv[1],
                    str(build / "Debug-iphonesimulator/FlyNES.app")], check=True)
    container = subprocess.check_output(
        ["xcrun", "simctl", "get_app_container", sys.argv[1], "com.flynes.app", "data"],
        text=True).strip()
    if not container or not Path(container).is_dir():
        raise RuntimeError("simctl did not return an existing FlyNES data container")
    action = tree.find("TestAction")
    action.set("shouldUseLaunchSchemeArgsEnv", "NO")
    environment = action.find("EnvironmentVariables")
    if environment is None:
        environment = ET.SubElement(action, "EnvironmentVariables")
    for entry in list(environment):
        if entry.get("key") in {"FLYNES_TEST_APP_CONTAINER", "FLYNES_TEST_APPLICATION_CONTAINERS"}:
            environment.remove(entry)
    ET.SubElement(environment, "EnvironmentVariable", {
        "key": "FLYNES_TEST_APPLICATION_CONTAINERS", "value": str(Path(container).parent), "isEnabled": "YES"})
tree.write(scheme, encoding="utf-8", xml_declaration=True)
stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
result = build / "evidence" / ("tests-" + stamp + ".xcresult")
result.parent.mkdir(parents=True, exist_ok=True)
command = ["xcodebuild", "test-without-building", "-project", str(project),
           "-scheme", scheme_name, "-destination", "platform=iOS Simulator,id=" + sys.argv[1],
           "-parallel-testing-enabled", "NO", "-resultBundlePath", str(result)]
command.extend(sys.argv[3:])
print("Result bundle:", result, flush=True)
sys.exit(subprocess.call(command))
