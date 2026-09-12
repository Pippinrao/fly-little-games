import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HARMONY = ROOT / 'harmony'

class RunnerContract(unittest.TestCase):
    def test_isolated_bundle_and_permission(self):
        app = json.loads((HARMONY / 'AppScope/app.json5').read_text())['app']
        self.assertEqual(app['bundleName'], 'com.flynes.nearbyprobe')
        self.assertEqual(app['versionCode'], 1)
        module = json.loads((HARMONY / 'entry/src/main/module.json5').read_text())['module']
        self.assertEqual(module['requestPermissions'], [{'name': 'ohos.permission.INTERNET'}])
        profile = json.loads((HARMONY / 'build-profile.json5').read_text())
        self.assertEqual(profile['app']['signingConfigs'], [])
        self.assertNotIn('signingConfig', profile['app']['products'][0])

    def test_native_worker_and_real_archive(self):
        native = (HARMONY / 'entry/src/main/cpp/napi_init.cpp').read_text()
        self.assertIn('napi_create_async_work', native)
        self.assertIn('nearby_quic_probe_client(', native)
        self.assertIn('napi_queue_async_work', native)
        cmake = (HARMONY / 'entry/src/main/cpp/CMakeLists.txt').read_text()
        for token in ['IMPORTED', 'libnearby_quic_spike.a', 'FATAL_ERROR', '--no-undefined', '--exclude-libs,ALL']:
            self.assertIn(token, cmake)
        self.assertNotIn('core/', cmake)

    def test_explicit_want_and_ui(self):
        ability = (HARMONY / 'entry/src/main/ets/entryability/EntryAbility.ets').read_text()
        for token in ['onCreate', 'onNewWant', 'runProbe === true', "typeof parameters.bind === 'string'", "typeof parameters.peer === 'string'", "typeof parameters.pin === 'string'"]:
            self.assertIn(token, ability)
        page = (HARMONY / 'entry/src/main/ets/pages/Index.ets').read_text()
        self.assertIn('.enabled(!this.busy)', page)
        self.assertIn('TextSelectableMode.SELECTABLE_FOCUSABLE', page)
        self.assertIn('transport experiment', page)

    def test_every_want_invalidates_pending_request_before_parsing(self):
        ability = (HARMONY / 'entry/src/main/ets/entryability/EntryAbility.ets').read_text()
        self.assertIn('onCreate(want: Want): void { this.beginLaunch(want); }', ability)
        self.assertIn('onNewWant(want: Want): void { this.beginLaunch(want); }', ability)
        initialization = ability.split('private beginLaunch(want: Want): void {', 1)[1]
        self.assertLess(initialization.index('probeLaunchGate.beginLaunch()'), initialization.index('this.acceptWant(want)'))
        self.assertLess(initialization.index("AppStorage.setOrCreate('probeRequest', 0)"), initialization.index('this.acceptWant(want)'))

    def test_stage_refuses_existing_directory(self):
        with tempfile.TemporaryDirectory(prefix='nearby-stage-test-') as directory:
            result = subprocess.run(['pwsh', '-NoProfile', '-File', str(ROOT / 'stage-harmony.ps1'), '-Destination', directory], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('refusing overwrite', result.stderr)
            self.assertEqual(list(pathlib.Path(directory).iterdir()), [])

    def test_dot_sourced_mobile_failure_restores_environment(self):
        script = str(ROOT / 'build-mobile.ps1').replace("'", "''")
        command = "$ErrorActionPreference='Continue'; $env:CC_aarch64_unknown_linux_ohos='preserved-compiler'; $env:CARGO_ENCODED_RUSTFLAGS='preserved-flags'; try { . '" + script + "' -Platform ohos -DevEcoHome 'Z:/nonexistent-probe-sdk' } catch { }; if ($env:CC_aarch64_unknown_linux_ohos -ne 'preserved-compiler' -or $env:CARGO_ENCODED_RUSTFLAGS -ne 'preserved-flags' -or $ErrorActionPreference -ne 'Continue') { exit 8 }"
        result = subprocess.run(['pwsh', '-NoProfile', '-Command', command + '; exit 0'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

if __name__ == '__main__':
    unittest.main()
