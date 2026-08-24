# FlyNES Alpha acceptance record — 2026-08-25

## Build identity

- Git commit: `16b5043eaf3ca30eb227ce26e55a4089e161af6e`
- APK: `app/build/outputs/apk/release/app-release-local-test.apk`
- APK SHA-256: `8411039298A1033E36E9A63933760B9510242A4FAF1B0E1A181A9917C317B452`
- Package: `com.flynes.emu`, version `0.1.0` (`versionCode 1`)
- Local-test signature certificate SHA-256:
  `ad710f585bee2e26995939441773ebb38cdcea4d49bb9a694035889500f95bbf`
- APK Signature Scheme v2 and v3 verification: passed
- This local-test APK uses the Android debug certificate. It is suitable for the connected
  development phone, not for store distribution.

## Completed automated gates

- Android JVM unit tests: passed (`:app:testDebugUnitTest`).
- Android emulator instrumentation: 51/51 passed (`:app:connectedDebugAndroidTest`).
- Native host core smoke: 1/1 passed (`ctest --test-dir core/build/host-ninja`).
- Release build and lint-vital: passed (`:app:assembleRelease`).
- Release emulator cold launch and gameplay launch: passed; no fatal exception, shader
  compilation failure, or missing-cover decode error in the inspected log.

The automated set includes the pointer-ID input state machine, 10,000 randomized multitouch
sequences, opposing-direction exclusion, B-to-A roll combinations, true Select/Start holds,
joystick dead-zone and release hysteresis, settings migration, all GPU filter compilation,
frame publication, cover quality/capture, favorites, pause separation, control accessibility,
both landscape sensor directions, large text, and catalog launch paths.

## Simulator and visual evidence

- AVD: API 35, physical framebuffer 1080×2340, tested in 2340×1080 landscape.
- `.artifacts/gpu-presenter/edge-enhanced.png`: edge-enhanced GPU presenter.
- `.artifacts/gpu-presenter/home-favorite-cover.png`: captured cover and favorite action.
- `.artifacts/gpu-presenter/joystick-default.png`: initial 144dp joystick visual iteration.
- `.artifacts/gpu-presenter/release-smoke.png`: final 128dp joystick Release smoke.
- `.artifacts/gpu-presenter/main.xml` and related hierarchy dumps identify the matching page.

## Not yet passed — must remain open

At the end of this run, `adb devices -l` listed only `emulator-5554`; the vivo X100 Pro was no
longer connected. Consequently, none of the following is marked as passed:

- installing this new Release hash on the vivo X100 Pro;
- probing its vibrator waveform/amplitude capabilities and performing A/B/all-control blind tests;
- verifying requested and actual 120Hz with SurfaceFlinger/Perfetto on that phone;
- touch-to-visible latency and 30-minute thermal/audio/input gameplay on Release;
- the six-person ergonomic gate and the required external ROM gameplay sample.

The earlier APK installed on the phone has a different hash and does not contain this batch.
Final Release acceptance remains blocked until the target phone reconnects and these checks run.
