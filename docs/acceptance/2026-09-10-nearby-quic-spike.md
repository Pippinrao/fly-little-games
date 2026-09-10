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

## Not established

No Android↔Harmony QUIC run yet; no iOS native run; no offline/routerless Wi-Fi group or single-confirmation qualification; no BLE/QR authentication, persistent friends, production ChannelBind, game input, picture/PCM sharing, latency qualification, reconnection, state recovery or mode switching. Do not convert these probe results into product support-matrix entries.
