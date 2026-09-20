# Disposable M0a QUIC candidate probe

This standalone experiment measures Quinn **0.11.11**, rustls **0.23.43**
(ring provider), rcgen **0.14.10**, and x509-parser **0.18.1**. Exact direct
versions and Cargo.lock are committed. It does not link to the game, change
the shared ABI/schema, or implement multiplayer, BLE/QR pairing, ChannelBind,
Wi-Fi creation/join, or a certified platform support entry.

Source requirements: approved design
[`§26 M0a, §27 release blockers, §28 security review`](../../docs/archive/nearby-2026-09-21/docs/superpowers/specs/2026-09-04-cross-platform-nearby-multiplayer-design.md).
The `flynes-m0a-quic-v1` ALPN and probe messages are deliberately separate
from production wire messages.

## Run

Rust 1.96 was used on Windows. From the repository root:

```text
cargo build --locked --manifest-path tools/nearby-quic-spike/Cargo.toml
tools/nearby-quic-spike/target/debug/nearby-quic-spike server 127.0.0.1:0
```

The listener immediately flushes one line:

```text
READY address=127.0.0.1:54321 spki=<182 hexadecimal digits>
```

Within **20 seconds**, run a second terminal, replacing the endpoint and
pin with those exact values (Windows executable has `.exe` suffix):

```text
tools/nearby-quic-spike/target/debug/nearby-quic-spike client 127.0.0.1:0 127.0.0.1:54321 <182-hex-digit-pin>
```

For two devices already on a manually prepared local network, explicitly
bind each endpoint to its own private interface address and give the client
the listener's actual private address and port. This executable does not
choose a Wi-Fi route or modify permissions/firewalls. A successful existing
LAN run does not certify an approved no-router Wi-Fi plan.

Each successful process exits zero and prints:

```text
VERIFIED role=client stream=true datagram=true pin_checks=1 exporter_sha256=<64 hexadecimal digits>
VERIFIED role=server stream=true datagram=true pin_checks=0 exporter_sha256=<same 64 hexadecimal digits>
```

Each process prints its own role's line. Server pin_checks=0 is intentional:
this probe authenticates the listener only and does **not** use mutual TLS.
READY is readiness, not authentication or success. Invalid arguments and
failed/deadline probes exit nonzero and print `ERROR` on stderr.

## Security and bounds

- The full **public 91-byte DER-SPKI** is explicitly provisioned by the
  operator. This test-only trust input substitutes for future authenticated
  bootstrap solely within this probe. Copying it is not BLE authentication,
  a SAS check, a durable identity, or production ChannelBind.
- Each listener generates a fresh in-memory P-256 key and certificate;
  no private key, keylog, or raw TLS exporter is persisted or printed.
  TLS exporter bytes exist transiently in process memory to compute SHA-256.
- x509-parser parses complete certificates and SPKI. Exact canonical SPKI
  encoding, named P-256 and uncompressed point are required; ring validates
  the point. Empty/malformed/oversized/trailing pins or certificates and
  additional certificates are rejected. The full pin comparison happens
  **inside** rustls ServerCertVerifier, before application exchange.
- Trust is the explicit pin, not DNS/CA or certificate wall-clock validity.
  CertificateVerify is independently checked by rustls's ring signature
  verifier, restricted to ECDSA P-256 SHA-256. Both endpoints permit TLS1.3
  only. TLS1.2's defensive callback always rejects.
- Client resumption/cache and early data are disabled. Server session
  storage, ticket production, and early data are explicitly disabled.
  Migration is disabled; the spike does not call endpoint rebind.
- Numeric loopback/private/IPv4 link-local addresses only. Unspecified,
  public, multicast, mapped IPv6, zero peer port and family mismatch are
  rejected before socket creation. IPv6 loopback and ULA are accepted;
  IPv6 link-local, scope IDs and flowinfo are explicitly unsupported here.
- Default whole-probe deadline is 20 seconds including listener waiting,
  handshake, exchange and draining. The library permits nonzero deadlines
  up to 60 seconds. Idle timeout is at most 5 seconds; one CLI session,
  at most two sequential library sessions on one listener credential.
- Two bidirectional streams have FIN in each direction and 128-byte capped
  reads. Receive/send buffers and stream counts are capped. Client/server
  DATAGRAM messages have different prefixes and exporter digests; delivery
  is unreliable and retries are bounded (30 attempts, 100 ms intervals).
  After receipt, retransmission continues during reliable completion so
  one lost reverse DATAGRAM does not prematurely stop the peer's retries.
- Exporter label is `EXPORTER-flynes-m0a-quic-v1`, context is
  `probe-stream-datagram-v1`, output length is 32; only its SHA-256 digest
  crosses the probe wire. A distinct context is also checked to yield a
  different digest. This is evidence of exporter availability/agreement,
  not the approved production ChannelBind message/proof.
- The reliable completion stream reports both local validations. Client
  reads and validates the server response through FIN before sending an
  explicit completion close. Server waits for that exact close before
  reporting success, avoiding a premature close that discards final bytes.
  Reliable completion has priority over auxiliary DATAGRAM retries. If a
  retry observes closure between polls, the same completion future is resolved
  under the existing whole-probe deadline; invalid close codes/reasons fail.

## Library and checks

`start_server(bind).await` returns `Server { address, spki, handle }`;
`handle.await??` returns validated server reports. `run_client(bind, peer,
pin).await` returns one report only after all checks. The explicitly bounded
`start_server_sessions` and `run_client_deadline` variants support experiments.
Aborting the server handle ends the owned listener. Public `tls` configuration
factories support independent real TLS tests; they never expose private keys.

```text
cargo test --locked --manifest-path tools/nearby-quic-spike/Cargo.toml
cargo fmt --manifest-path tools/nearby-quic-spike/Cargo.toml -- --check
cargo check --locked --manifest-path tools/nearby-quic-spike/Cargo.toml
cargo clippy --locked --manifest-path tools/nearby-quic-spike/Cargo.toml --all-targets -- -D warnings
```

Tests use actual UDP/TLS and two CLI processes. They cover stream/DATAGRAM
and exporter agreement, pin callback count, two fresh connections using one
listener credential, wrong valid pin, malformed pin/certificate, intermediates,
wrong CertificateVerify signing key, exporter context mismatch, stream cap/FIN,
deadline, address and CLI rejection. Actual TCP/TLS additionally tests TLS1.2
negotiation rejection and the defensive TLS1.2 signature callback, because
QUIC itself does not negotiate TLS1.2. The dev-only rustls `tls12` feature
exists for those negative tests; normal builds omit it.

TDD evidence: the initial executable/library failed closed at runtime; six
transport/address tests failed after successful compilation. Implementation
made those tests pass. The positive two-process CLI test separately failed
with empty READY output against the CLI stub before the CLI was implemented.
Loopback and build results alone are not real phone or M0a completion evidence.

A completion/retry race regression deterministically makes completion ready
during the retry-error poll. It failed against the original arbitration and
passes after awaiting the pinned completion future. Separate checks cover
both-ready branches, invalid completion, the outer deadline, and actual QUIC
exchanges followed by wrong final close codes/reasons.

## Isolated Harmony foreground runner

This source template builds a separate `com.flynes.nearbyprobe` version 1 app,
API 12 compatible / API 20 target, arm64 only. It requests INTERNET only. It
does not contain the game core, ROMs, product bridge, lobby, or frame data.
The installed `com.flynes.emu` app is a different bundle and is not replaced.

From the repository root in PowerShell:

```powershell
& tools/nearby-quic-spike/build-mobile.ps1 -Platform all
& tools/nearby-quic-spike/stage-harmony.ps1
$env:DEVECO_STUDIO_HOME = 'D:/soft/DevEco Studio'
$env:DEVECO_SDK_HOME = "$env:DEVECO_STUDIO_HOME/sdk"
$env:NODE_HOME = "$env:DEVECO_STUDIO_HOME/tools/node"
Push-Location .artifacts/nearby-quic-harmony-app
try {
  & "$env:NODE_HOME/node.exe" "$env:DEVECO_STUDIO_HOME/tools/ohpm/bin/pm-cli.js" install --all
  & "$env:NODE_HOME/node.exe" "$env:DEVECO_STUDIO_HOME/tools/hvigor/bin/hvigorw.js" assembleHap -pproduct=default -pmodule=entry@default -pbuildMode=debug --no-daemon
} finally { Pop-Location }
```

`build-mobile.ps1` accepts `-DevEcoHome`, `-AndroidNdk`, `-Platform`, and
`-Release`; it restores compiler environment variables even when dot sourced
or a build fails. It builds both the CLI executable and actual Rust staticlib.
The default debug archive is `.artifacts/nearby-quic-ohos/aarch64-unknown-linux-ohos/debug/libnearby_quic_spike.a`.
For release staging, pass the corresponding release archive with `-Archive`.
No global Cargo/SDK configuration is modified.

`stage-harmony.ps1` copies only this template and the selected actual OHOS
archive/header into ignored `.artifacts/nearby-quic-harmony-app`; it refuses
an existing destination to preserve later local signing. `-Destination`
allows a fresh staging directory. Build only the staged project. Its CMake
fails when the archive/header are missing and links `libnearbyprobe.so` with
`--no-undefined` and `--exclude-libs,ALL`.

The unsigned package is
`.artifacts/nearby-quic-harmony-app/entry/build/default/outputs/default/entry-default-unsigned.hap`.
Unsigned build success does not make that HAP installable. Open the staged
project in DevEco Studio, select Project Structure > Project > Signing Configs,
choose the `default` product and automatic signing, and sign in with the
device owner's Huawei developer account. Select the attached test device as
required by DevEco. Keep generated signing configuration/certificates in the
ignored staging project; never copy them into this tracked template. Build
again after signing; use the resulting `entry-default-signed.hap`.

Install and run only after the owner has configured this new bundle's signing:

```powershell
hdc -t <Harmony-device-id> install .artifacts/nearby-quic-harmony-app/entry/build/default/outputs/default/entry-default-signed.hap
hdc -t <Harmony-device-id> shell aa start -b com.flynes.nearbyprobe -a EntryAbility --ps bind '<Harmony-LAN-IP>:0' --ps peer '<Android-LAN-IP>:45555' --ps pin '<full-182-character-SPKI-hex-from-READY>' --pb runProbe true
hdc -t <Harmony-device-id> shell hilog -T NearbyQuicProbe
```

Start the Android probe CLI listener first (`server <Android-LAN-IP>:45555`)
and feed its current READY SPKI into the explicit Want promptly: its entire
listener lifetime is bounded to 20 seconds. A new listener has a new pin.
Keep both apps foreground and phones on the same LAN. The page can also run
from entered fields. Without an explicit boolean `runProbe=true` plus three
string parameters, launch does not connect. Each explicit Want is consumed
once across page remounts. A new Ability launch invalidates pending old requests.
Malformed explicit Wants and Wants received while busy visibly report failure;
busy requests do not change the active inputs or queue a second probe.
Logs tagged `NearbyQuicProbe` contain the public result and exporter SHA-256
digest, never raw exporter bytes or private keys. Compare the actual server
and client VERIFIED digests; then repeat with another valid listener's SPKI
to verify the pin-mismatch error. Neither a build nor a loopback test proves
the two physical phones communicated.

The C ABI in `include/nearby_quic_spike.h` validates lengths, UTF-8, numeric
local addresses, pin and output capacity before runtime/socket creation.
The caller supplies valid immutable inputs and a disjoint writable buffer
of at least 1024 bytes; success is never truncated and nothing borrowed
escapes. Its statuses distinguish invalid arguments, output, transport, and
caught Rust panic. The fixed network deadline is 20 seconds. The NAPI wrapper
copies bounded strings on the JS thread and executes that blocking ABI on
one native async worker; only completion resolves/rejects the Promise.

Runner checks:

```powershell
python -m unittest discover -s tools/nearby-quic-spike/tests -p test_harmony_runner.py
node --test tools/nearby-quic-spike/tests/launch_gate.mjs
cargo test --locked --manifest-path tools/nearby-quic-spike/Cargo.toml --test ffi
```

FFI tests first failed for the missing export, then passed real UDP success
with server/client digest equality and real wrong-pin rejection, plus invalid
UTF-8/pin/address/NULL/capacity cases. Template contract tests first failed
for missing runner sources. Actual Hvigor compilation/package creation also
checks the NAPI implementation and ArkTS SDK types. SDK LLVM 15 inspection
must use `llvm-nm --no-llvm-bc --defined-only` on Rust 1.96 static archives:
the embedded newer LLVM bitcode is irrelevant to the successful native link.
