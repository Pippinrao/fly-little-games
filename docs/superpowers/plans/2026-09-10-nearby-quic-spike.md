# Nearby real-transport M0a spike implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. This executes the already approved design, not a new product protocol.

**Goal:** Establish real QUIC/TLS 1.3 connections with exact listener SPKI pinning, TLS exporter agreement, bidirectional streams and DATAGRAM, then carry the same probe toward phone execution.

**Architecture:** Keep a disposable candidate-backend probe in `tools/nearby-quic-spike/`. It must not link into the product or advertise certified capabilities. Quinn/rustls/ring is a candidate being measured, not a frozen three-platform backend. A distinct probe ALPN and probe messages prevent confusion with approved FNB1/session wire. Public SPKI is provisioned explicitly by the operator for this experiment only; that is not a replacement for authenticated BLE/QR pairing.

**Tech stack:** Rust 1.96; pinned Quinn 0.11.11, rustls 0.23.43 (not the 0.24 development release), ring, rcgen 0.14.10, x509-parser 0.18.1; Cargo.lock; Windows and available Android/OHOS cross compilers.

## Evidence and bounds

- Approved design §27 M0a and §28 require this before device-pair certification. Existing 41 CTests do not prove transport.
- Quinn exposes `Connection::export_keying_material`; rustls exposes `ServerCertVerifier` and library TLS 1.3 signature verification. Sources: https://docs.rs/quinn/0.11.11/quinn/struct.Connection.html and https://docs.rs/rustls/0.23.43/rustls/client/danger/trait.ServerCertVerifier.html.
- Rust distributes `aarch64-unknown-linux-ohos`; this does not prove Quinn/ring/socket support on the actual phone. Source: https://doc.rust-lang.org/rustc/platform-support/openharmony.html.
- No plaintext fallback, no certificate accept-all, no system-CA fallback, no mTLS, no key logging, no private key persistence. Session resumption/cache/tickets and 0-RTT disabled explicitly. CertificateVerify delegated to rustls/ring, never hand-written crypto.
- The probe accepts only explicitly supplied numeric loopback/private/link-local addresses, never wildcard/public endpoints. It does not create Wi-Fi groups, alter firewall/security policy, install or overwrite the existing phone app, or claim interface binding/one-prompt qualification.

## Task 1 — executable candidate backend and real-socket regression

**Files:** create `tools/nearby-quic-spike/{Cargo.toml,Cargo.lock,.gitignore,README.md}`, `src/{lib.rs,tls.rs,probe.rs,main.rs}`, `tests/transport.rs`.

- [ ] Write failing tests against these public probe functions, with real loopback UDP sockets and bounded timeouts:

```rust
// Public API boundary; implementation may use small typed result structures.
// start_server binds synchronously and returns actual address/public SPKI + bounded task.
// run_client requires exact SPKI bytes before any socket connection is authorized.
#[tokio::test]
async fn real_quic_stream_datagram_and_exporter_agree() {
    let server = start_server("127.0.0.1:0".parse().unwrap()).await.unwrap();
    let client = run_client("127.0.0.1:0".parse().unwrap(), server.address(), server.spki()).await.unwrap();
    let listener = server.finish().await.unwrap();
    assert_eq!(client.exporter_digest, listener.exporter_digest);
    assert!(client.stream_verified && client.datagram_verified);
    assert!(listener.stream_verified && listener.datagram_verified);
    assert_eq!(client.pin_checks, 1);
}
```

Additional separate assertions: another valid certificate's SPKI fails the TLS handshake before application exchange; malformed/empty/oversized DER pins fail; a second fresh connection to the same listener credential invokes pin verification again and produces a different exporter; TLS 1.2 verifier rejects; an unresponsive peer hits a bounded deadline; CLI refuses wildcard/public addresses and malformed arguments. Use actual signatures and sockets, no fake handshake. Run `cargo test --manifest-path tools/nearby-quic-spike/Cargo.toml`; establish a behavioral RED with fail-closed implementation stubs, not only missing-symbol errors.

- [ ] Implement `tls.rs`: ephemeral P-256 listener certificate/key with rcgen; parse exact full DER-SPKI with x509-parser and require named P-256/uncompressed point. Exact byte compare during `verify_server_cert`; reject malformed/trailing certificate/pin input and unexpected intermediates. Delegate TLS 1.3 handshake signature validation to rustls provider's `verify_tls13_signature`. Restrict supported signature schemes to ECDSA P-256 SHA-256. TLS1.3 only, no client certificate. Use explicit disabled client resumption, empty server session storage, disabled ticket production and zero early data. Count pin verification for probe evidence; never log secrets.
- [ ] Implement `probe.rs`: bounded Tokio endpoint lifetime, capped stream/datagram receive, nonzero idle timeout, migration disabled, one explicit numeric bind/peer. Use separate `flynes-m0a-quic-v1` ALPN, fixed bounded probe payloads, both stream FINs, exchange a digest of the exporter for agreement (never print raw exporter), distinct context negative test. Send and validate both directions' QUIC DATAGRAM, with bounded retry suitable for unreliable datagrams. Report only after all validations. No FNB1/game/session messages and no production registry entries.
- [ ] Implement `main.rs`: `server <bind-ip:port>` prints only actual listening endpoint and public full SPKI hex before waiting; `client <bind-ip:port> <peer-ip:port> <spki-hex>` prints non-secret result and nonzero exit on failure. Server processes a bounded probe and exits. Validate args before effects. Flush ready output for two-process automation.
- [ ] Run behavioral tests, `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`; test two independent CLI processes, not only in-process endpoints. Lock all resolved dependencies. Document exact commands and mark phone/BLE/Wi-Fi/ChannelBind/gameplay unverified.
- [ ] Spec review then quality review; fix findings and rerun tests. Commit only this probe and its plan, preserving the unrelated untracked device audit.

## Task 2 — phone build and supported execution evidence

**Files:** add `tools/nearby-quic-spike/build-mobile.ps1` and update its README with observed results; only add a test-HAP/native bridge after inspecting signing and approved installation scope.

- [ ] Install standard Rust Android/OHOS target components if absent; configure only per-process build variables with installed NDK/DevEco compiler paths (no global SDK config edits).
- [ ] Cross-build exact locked probe for Android ARM64 and OHOS ARM64. Capture errors and fix supported portability issues; never relabel Linux/Android output as Harmony.
- [ ] Verify emitted ELF target/dependencies with SDK tools. Compile success is not execution success.
- [ ] Recheck ADB/HDC. Run only through an authorized supported device execution path; do not bypass HAP signing/SELinux or overwrite `com.flynes.emu`. Missing second device or signing credentials is a user/external gate, not grounds to claim success.
- [ ] Once two runnable endpoints exist, exchange exact public pin out of band for this experiment and run actual bidirectional QUIC probe on an explicitly selected local path. Record both endpoints/OS/backend versions and positive/negative pin results. This alone still does not certify routerless Wi-Fi, one prompt, full ChannelBind, video/audio or multiplayer.

## Completion accounting

Do not stop at a green probe if further safe work is possible. Overall multiplayer remains incomplete until authenticated pairing, selected local path, session input, picture/audio and lifecycle recovery work on the required phone pairs. Escalate concrete missing hardware/signing/permission choices, not generic claims that all coding is blocked.

## Task 3 — isolated Harmony foreground probe runner

**Files:** add `src/ffi.rs`, `include/nearby_quic_spike.h`, and `harmony/` under `tools/nearby-quic-spike/`. The isolated HAP owns its own AppScope, build profiles, module, resources, ArkTS Ability/page, native CMake/N-API bridge and declaration package. Do not modify the product `harmony/` directory or its installed bundle.

- [ ] Expose a bounded blocking C ABI client call for an asynchronous N-API worker. All input pointers have explicit byte lengths; validate null/length/UTF-8/address/pin before creating runtime or sockets. Return a status plus a caller-owned bounded text result; report truncation as failure. Catch Rust unwinding at the boundary, no borrowed pointers escape the call, no Tokio runtime or mutable handle shared between calls.
- [ ] Add tests first for null pointers, malformed pin/address, insufficient output capacity, and a real-loopback call that produces successful stream/datagram/exporter verification through the C ABI. Compile `rlib` and `staticlib`, link the actual OHOS archive rather than a fake result library.
- [ ] N-API `runClient(bind, peer, pinHex): Promise<string>` validates and copies three bounded strings, allocates one async work item, runs the blocking C function on its worker, and resolves/rejects only in completion on the JS thread. No UI-thread networking; clean up work data on every creation/queue/completion failure.
- [ ] Stage app `com.flynes.nearbyprobe`, version 1, API12-compatible/API20-target, arm64, INTERNET only, no product data or ROM. Page provides bind/peer/public-pin fields, a disabled-while-running Start button and selectable result text. Say explicitly that this is a transport experiment, not a game lobby.
- [ ] Support explicit debug launch arguments `--ps bind ... --ps peer ... --ps pin ... --pb runProbe true` through EntryAbility Want, with type checks and one start per launch; malformed input must produce visible failure, not fallback. Log only probe results/public metadata, never private keys or raw exporter.
- [ ] Use external Rust archive path CMake input and require it to exist. Build unsigned HAP with installed DevEco tools. Contract tests verify bundle isolation, INTERNET-only permission and asynchronous bridge; inspect real linked ELF and HAP entries.
- [ ] Obtain matching debug signing through the supported DevEco project signing workflow. Existing `com.flynes.emu` app is version 1000001 and must not be replaced. No signature/profile/token material in Git; a new bundle's profile cannot be assumed from existing credentials. If signing requires interactive login or issuance, ask the user for that exact action with the ready project path.
- [ ] Once signed, install only this distinct probe bundle and run Harmony client against Android server on the current local Wi-Fi. This proves encrypted device transport only; routerless group creation and product pairing remain separate acceptance items.
