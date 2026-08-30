# Display-quality acceptance index

This directory separates implementation evidence from physical-device certification. A green
JVM, host, or emulator suite proves contracts and fallback behavior only; it does not certify
120 Hz cadence, motion quality, touch latency, power, temperature, or long-play stability.

## Frozen and completed implementation evidence

- MMPX independent oracle commit: `a0d5540c7b21889da6a432a79c1553518a4d4d14`.
- Qualified MMPX/ScaleFX implementation commit: `aca6cb7`.
- [`mmpx-oracle-manifest.json`](mmpx-oracle-manifest.json) pins the independent CPU oracle,
  fixtures, source notice, and hashes used by GPU comparisons.
- [`spatial-reconstruction.md`](spatial-reconstruction.md) records supported algorithms,
  capability gates, GPU timing semantics, and emulator results.
- [`preflight.md`](preflight.md) records the repository/toolchain audit and release blockers.

## Native-time 120 Hz acceptance contract

The HZ120 + Native path selects only a same-resolution display mode in Java. The native render
thread is the sole writer of `ANativeWindow_setFrameRate` and votes the core-confirmed source
rate (NTSC approximately 60.0988 Hz or PAL 50 Hz). It remains event-driven: intervening 120 Hz
display scans retain the latest buffer instead of causing duplicate uploads or swaps.

Automated contract gates:

```powershell
.\gradlew.bat :app:testDebugUnitTest --tests com.flynes.emu.video.DisplayRequestLifecycleTest --tests com.flynes.emu.video.NativeTime120ContractTest --no-daemon
.\gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.notAnnotation=com.flynes.emu.test.DeviceCertification --no-daemon
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/quality/invoke_pester_gate.ps1 tools/quality/tests/CaptureDisplayEvidence.Tests.ps1
```

`NativeTime120CertificationTest` is deliberately excluded from ordinary runs. It is valid only
when `SingleDeviceCertificationRunner` verifies an explicitly authorized physical serial and
model. The test requires a system-reported 119–121 Hz mode, an applied approximately 60.099 Hz
native vote, zero synthesized/motion slots, and approximately 60 unique source submissions per
second.

QEMU/Goldfish/Ranchu rejects the Surface frame-rate request as `UNSUPPORTED`, then the rollback
path records the final state as `CLEARED`; it never invokes the emulated platform API. The
emulator advertises the symbol but cannot model a physical panel negotiation reliably and can
stall its compositor. This is an expected truthful AVD fallback; only the authorized physical
certification test may require `APPLIED`.

Uncertified 120 Hz is hidden from production settings and falls back to 60 Hz in the resolver.
The only pre-certificate 120 Hz route is the debuggable, explicit instrumentation/evidence hook;
Release builds ignore that intent extra.

## Physical evidence transaction

`tools/quality/capture_display_evidence.ps1` requires one non-emulator device, exact `ro.serialno`
and `ro.product.model`, and an already-installed APK whose bytes, package/version, and signing
certificate match the supplied APK. It creates a new directory under
`.artifacts/evidence/display/`, refuses overwrite, identifies exactly one FlyNES SurfaceView,
captures a bounded Perfetto trace plus SurfaceFlinger/display/gfx/log/screenshot/UI evidence,
and writes `manifest.json` only after success. Partial directories have no completion manifest
and are invalid evidence.

The transaction launches the public game center, selects the built-in category and invokes the
same `launch_selected` entry used by players by stable Android resource IDs. It then waits for the
unique game SurfaceView; translated text and fixed screen coordinates are not used.

Example for one 30-minute trial:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/quality/capture_display_evidence.ps1 `
  -Serial '<ro.serialno>' -ExpectedModel '<ro.product.model>' `
  -ApkPath '<absolute-release-apk>' -DurationSeconds 1800
```

Task 6 paired native-time trials use the same debuggable APK, authorized physical device, built-in
NTSC sample, and pair identifier. Each run fails before tracing unless the system-reported mode
settles at the exact app-confirmed mode id, resolution, and refresh target. The capture samples
that complete mode identity and the exact Perfetto PID throughout the trace, rejects an early or
overlong trace, and continues sampling until Perfetto actually exits. A unique remote wrapper must
then report exact exit status `0`; a merely non-empty partial trace is not accepted:

```powershell
$pair = 'native-time-pair-01'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/quality/capture_display_evidence.ps1 `
  -Serial '<ro.serialno>' -ExpectedModel '<ro.product.model>' -ApkPath '<absolute-debug-apk>' `
  -Mode 60 -PairId $pair -DurationSeconds 1800
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/quality/capture_display_evidence.ps1 `
  -Serial '<ro.serialno>' -ExpectedModel '<ro.product.model>' -ApkPath '<absolute-debug-apk>' `
  -Mode 120 -PairId $pair -DurationSeconds 1800
```

The completion manifest records `trialMode`, `pairId`, requested and actual refresh, and
`sourceTiming`. Source timing comes from the loaded emulator core (`NTSC_60_0988` or `PAL_50`),
not a script constant. It also records requested/active mode identity, observed trace runtime, and
display sample count. A `PUBLIC` run continues to exercise the translated public game-center path.

Release certification still requires at least three interleaved 60 Hz Native / 120 Hz Native
paired 30-minute trials at fixed scene, brightness, and battery band, followed by the Task 8
signed candidate/evidence transaction. Until those files exist and verify, the acceptance state
is **implementation complete, physical certification pending**, not “120 Hz passed”.
