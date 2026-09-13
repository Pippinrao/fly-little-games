# Nearby QUIC candidate-backend experiment — 2026-09-10

This records an M0a transport experiment, **not multiplayer acceptance or a certified bearer plan**. The approved design and schema are unchanged. Probe ALPN/messages are intentionally separate from production session wire; public listener SPKI is provisioned by the test operator, not by implemented BLE/QR pairing.

## Actual Android ↔ Windows result

- Android: connected V2324A, existing local Wi-Fi IPv4 `192.168.3.15`.
- Windows: local Wi-Fi IPv4 `192.168.3.18`.
- Backend: Quinn 0.11.11 / rustls 0.23.43 / ring 0.17.14, exact resolved dependency set in the probe's Cargo.lock.
- Android compiler: installed NDK 27.0.12077973, `aarch64-linux-android26`; Rust target `aarch64-linux-android`.
- Temporary executable: `/data/local/tmp/flynes-nearby-quic-m0a-20260910/probe`; no Android APK installed or replaced.
- Executable SHA-256 at this experiment: `46e98295b624d8592f179447cbf4c1f8622c6651d04f347ff8a7834768d6dd8f` (matched on host and phone).

Positive run: Android was the QUIC listener on `192.168.3.15:41668`; Windows connected with its exact public DER-SPKI. Actual output:

```text
VERIFIED role=client stream=true datagram=true pin_checks=1 exporter_sha256=99d1cb50c228bf64ef02d1bff5bfd72441230769c6025f37f1cffdf19a3a6c2a
VERIFIED role=server stream=true datagram=true pin_checks=0 exporter_sha256=99d1cb50c228bf64ef02d1bff5bfd72441230769c6025f37f1cffdf19a3a6c2a
CLIENT_EXIT=0 SERVER_EXIT=0
```

Negative run: a second independent listener generated another **valid** P-256 SPKI; giving that wrong pin to the Android connection was rejected inside TLS, not after application exchange:

```text
ERROR client handshake failed: ... listener SPKI pin mismatch
ERROR server handshake failed: ... listener SPKI pin mismatch
WRONG_PIN_CLIENT_EXIT=1 ANDROID_SERVER_EXIT=1
```

The Android listener did not report VERIFIED. No raw TLS exporter, private certificate key, ticket or other session secret was printed or written. Endpoint credentials are ephemeral.

Reverse-role experiment was **not successful**: Windows listener `192.168.3.18:64915`, Android connector, TLS handshake timed out. No success is recorded for that direction. Windows firewall profiles were enabled and no probe-specific application rule was found, but packet-level attribution was not established; this is not proof of the cause. No firewall rule or security policy was changed. The successful Android-listener direction already exercised application payloads in both directions on that connection.

## Harmony status

- HDC sees the Harmony device; current local Wi-Fi IPv4 is `192.168.3.8`.
- Actual Android↔Harmony bidirectional ICMP replies were received, but ICMP is not QUIC or gameplay evidence.
- Exact backend dependency graph and probe built and linked for `aarch64-unknown-linux-ohos` using installed OpenHarmony SDK Clang/sysroot. ELF is AArch64, dynamic dependency `libc.so`. Build success is not phone execution.
- Existing installed product bundle `com.flynes.emu` version 1000001 must not be overwritten. The independent `com.flynes.nearbyprobe` bundle was not found in a read-only query.
- Supported execution path is a normally signed, isolated foreground HAP with N-API worker. Existing local signing files are not evidence that a new bundle is provisioned. No signing material has been inspected, copied into Git, or reused.

## Follow-up — 2026-09-11

`63dc22b` adds the real OHOS Rust static-library/C ABI, asynchronous N-API bridge, isolated foreground HAP template, and safe build/staging scripts. `b1a4487` fixes latest-Want precedence: new plain or malformed launches invalidate an older pending launch without cancelling an active worker. The independent specification follow-up passed. Code-quality review found no Critical/Important issues for disposable test-harness use; one uncorrected Minor is that dot-sourcing `build-mobile.ps1` leaves the caller's `$ErrorActionPreference` as `Stop`. Run in a separate PowerShell process until preference restoration is fixed. Review is not device or production acceptance.

Fresh parent verification: 20 Rust tests, 6 Python runner/script tests, and 5 actual TypeScript launch-gate tests passed. Cargo fmt and strict clippy passed. Both exact locked Android and OHOS native builds succeeded. The staged unsigned HAP builds with the actual AArch64 library, not a mock. Its SHA-256 is `6d3e3c0e4c798e69cdcf3010fa7ed0941cf1ab6be23f5d6dfb98a5a0d4c80fa7`. Signing credentials are neither source nor evidence to publish.

The updated Android executable was pushed only to the same owned temporary path. Host and device SHA-256 match: `6fc143e5d189713e75397b646297a3af5b49c0cb54ef5d2fc0f0dea9dca8f365`. Fresh Android-listener/Windows-client physical retest on `192.168.3.15:48517`:

```text
VERIFIED role=client stream=true datagram=true pin_checks=1 exporter_sha256=f0ef740f782c1b36ea26968fd24b9d4bdb396d97bf4a6a9a6448a3ae33073bd4
VERIFIED role=server stream=true datagram=true pin_checks=0 exporter_sha256=f0ef740f782c1b36ea26968fd24b9d4bdb396d97bf4a6a9a6448a3ae33073bd4
CLIENT_EXIT=0 SERVER_EXIT=0
```

The owner reported automatic signing completed, but the subsequent SDK build of `.artifacts/nearby-quic-harmony-app` still reported `No signingConfig found for product default`. There is no `entry-default-signed.hap` in that project's output. The owner was asked to verify the exact isolated project and save its signing settings. No product app, signing file, firewall or device security policy was changed by this work. Android and Harmony remain attached. Installation and physical phone-to-phone verification await a correctly signed isolated package; this is not a successful Harmony run.

## Not established

No Android↔Harmony QUIC run yet; no iOS native run; no offline/routerless Wi-Fi group or single-confirmation qualification; no BLE/QR authentication, persistent friends, production ChannelBind, game input, picture/PCM sharing, latency qualification, reconnection, state recovery or mode switching. Do not convert these probe results into product support-matrix entries.
