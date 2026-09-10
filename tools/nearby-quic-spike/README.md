# Disposable M0a QUIC candidate probe

This standalone experiment measures Quinn **0.11.11**, rustls **0.23.43**
(ring provider), rcgen **0.14.10**, and x509-parser **0.18.1**. Exact direct
versions and Cargo.lock are committed. It does not link to the game, change
the shared ABI/schema, or implement multiplayer, BLE/QR pairing, ChannelBind,
Wi-Fi creation/join, or a certified platform support entry.

Source requirements: approved design
[`§26 M0a, §27 release blockers, §28 security review`](../../docs/superpowers/specs/2026-09-04-cross-platform-nearby-multiplayer-design.md).
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
