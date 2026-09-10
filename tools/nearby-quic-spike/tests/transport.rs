use nearby_quic_spike::{
    run_client, run_client_deadline, start_server, start_server_sessions, tls::validate_pin,
    validate_address,
};
use std::{
    net::SocketAddr,
    time::{Duration, Instant},
};

fn local() -> SocketAddr {
    "127.0.0.1:0".parse().unwrap()
}

#[tokio::test]
async fn real_quic_stream_datagram_and_exporter_agree() {
    let server = start_server(local())
        .await
        .expect("real listener must start");
    validate_pin(&server.spki).expect("listener supplies exact P256 SPKI");
    let client = run_client(local(), server.address, &server.spki)
        .await
        .unwrap();
    let reports = server.handle.await.unwrap().unwrap();
    assert_eq!(reports.len(), 1);
    let listener = &reports[0];
    assert!(client.stream_verified && listener.stream_verified);
    assert!(client.datagram_verified && listener.datagram_verified);
    assert_eq!(client.pin_checks, 1);
    assert_eq!(client.exporter_digest, listener.exporter_digest);
    assert_ne!(client.exporter_digest, client.alternate_context_digest);
    assert_eq!(
        client.alternate_context_digest,
        listener.alternate_context_digest
    );
}

#[tokio::test]
async fn second_fresh_connection_repins_same_listener_and_changes_exporter() {
    let server = start_server_sessions(local(), 2, Duration::from_secs(10))
        .await
        .unwrap();
    let first = run_client(local(), server.address, &server.spki)
        .await
        .unwrap();
    let second = run_client(local(), server.address, &server.spki)
        .await
        .unwrap();
    let reports = server.handle.await.unwrap().unwrap();
    assert_eq!((first.pin_checks, second.pin_checks), (1, 1));
    assert_ne!(first.exporter_digest, second.exporter_digest);
    assert_eq!(reports.len(), 2);
    assert_eq!(first.exporter_digest, reports[0].exporter_digest);
    assert_eq!(second.exporter_digest, reports[1].exporter_digest);
}

#[tokio::test]
async fn wrong_valid_pin_fails_tls_before_application() {
    let server = start_server(local()).await.unwrap();
    let other = start_server(local()).await.unwrap();
    let error = run_client(local(), server.address, &other.spki)
        .await
        .unwrap_err();
    assert!(error.to_string().contains("handshake"), "{error}");
    assert!(
        server.handle.await.unwrap().is_err(),
        "server must never produce validated application evidence"
    );
    other.handle.abort();
}

#[tokio::test]
async fn pins_reject_empty_malformed_trailing_and_oversized_der() {
    let server = start_server(local()).await.unwrap();
    let mut trailing = server.spki.clone();
    trailing.push(0);
    let mut bad_point = server.spki.clone();
    bad_point[26] = 2;
    for pin in [vec![], vec![0x30, 0], trailing, vec![0; 4097], bad_point] {
        assert!(validate_pin(&pin).is_err());
        assert!(run_client(local(), server.address, &pin).await.is_err());
    }
    server.handle.abort();
}

#[tokio::test]
async fn unresponsive_peer_obeys_deadline() {
    let server = start_server(local()).await.unwrap();
    let silent = std::net::UdpSocket::bind(local()).unwrap();
    let began = Instant::now();
    let result = run_client_deadline(
        local(),
        silent.local_addr().unwrap(),
        &server.spki,
        Duration::from_millis(200),
    )
    .await;
    assert!(result.unwrap_err().to_string().contains("deadline"));
    assert!(began.elapsed() < Duration::from_secs(2));
    server.handle.abort();
}

#[test]
fn addresses_are_explicit_and_local_only() {
    for good in [
        "127.0.0.1:0",
        "192.168.1.2:1234",
        "10.0.0.2:9",
        "172.16.0.2:9",
        "169.254.1.2:9",
        "[::1]:0",
        "[fd00::1]:9",
    ] {
        assert!(
            validate_address(good.parse().unwrap(), false).is_ok(),
            "{good}"
        );
    }
    for bad in [
        "0.0.0.0:9",
        "8.8.8.8:9",
        "224.0.0.1:9",
        "255.255.255.255:9",
        "[::]:9",
        "[ff02::1]:9",
        "[2001:4860:4860::8888]:9",
        "[fe80::1]:9",
        "[fe80::1%3]:9",
        "[::ffff:127.0.0.1]:9",
    ] {
        assert!(
            validate_address(bad.parse().unwrap(), false).is_err(),
            "{bad}"
        );
    }
    assert!(validate_address(local(), true).is_err());
}

#[test]
fn cli_rejects_invalid_arguments_before_ready() {
    for args in [
        vec![],
        vec!["server"],
        vec!["server", "0.0.0.0:9"],
        vec!["server", "example.com:9"],
        vec!["server", "127.0.0.1:0", "extra"],
        vec!["client", "127.0.0.1:0", "8.8.8.8:9", "00"],
        vec!["client", "127.0.0.1:0", "127.0.0.1:9", "zz"],
    ] {
        let output = std::process::Command::new(env!("CARGO_BIN_EXE_nearby-quic-spike"))
            .args(args)
            .output()
            .unwrap();
        assert!(!output.status.success());
        assert!(output.stdout.is_empty());
        assert!(!output.stderr.is_empty());
    }
}

#[test]
fn cli_two_process_probe_reports_only_verified_evidence() {
    use std::io::{BufRead, BufReader};
    use std::process::{Command, Stdio};
    let binary = env!("CARGO_BIN_EXE_nearby-quic-spike");
    let mut listener = Command::new(binary)
        .args(["server", "127.0.0.1:0"])
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    let mut stdout = BufReader::new(listener.stdout.take().unwrap());
    let mut ready = String::new();
    stdout.read_line(&mut ready).unwrap();
    assert!(ready.starts_with("READY "), "{ready:?}");
    let fields: Vec<_> = ready.split_whitespace().collect();
    assert_eq!(fields.len(), 3);
    let address = fields[1].strip_prefix("address=").unwrap();
    let pin = fields[2].strip_prefix("spki=").unwrap();
    let client = Command::new(binary)
        .args(["client", "127.0.0.1:0", address, pin])
        .output()
        .unwrap();
    assert!(
        client.status.success(),
        "{}",
        String::from_utf8_lossy(&client.stderr)
    );
    let output = String::from_utf8(client.stdout).unwrap();
    assert!(output.starts_with(
        "VERIFIED role=client stream=true datagram=true pin_checks=1 exporter_sha256="
    ));
    let mut completed = String::new();
    stdout.read_line(&mut completed).unwrap();
    assert!(
        completed.starts_with(
            "VERIFIED role=server stream=true datagram=true pin_checks=0 exporter_sha256="
        ),
        "{completed}"
    );
    assert!(listener.wait().unwrap().success());
    assert_eq!(
        output.split("exporter_sha256=").nth(1),
        completed.split("exporter_sha256=").nth(1)
    );
}

#[derive(Debug)]
struct ExactCertificate(std::sync::Arc<rustls::sign::CertifiedKey>);
impl rustls::server::ResolvesServerCert for ExactCertificate {
    fn resolve(
        &self,
        _hello: rustls::server::ClientHello<'_>,
    ) -> Option<std::sync::Arc<rustls::sign::CertifiedKey>> {
        Some(self.0.clone())
    }
}

#[tokio::test]
async fn real_tls_rejects_malformed_trailing_oversized_intermediate_and_wrong_signer_certificates()
{
    use rcgen::PublicKeyData;
    use rustls::pki_types::{CertificateDer, PrivatePkcs8KeyDer};
    use std::sync::{Arc, atomic::Ordering};
    let key = rcgen::KeyPair::generate_for(&rcgen::PKCS_ECDSA_P256_SHA256).unwrap();
    let pin = key.subject_public_key_info();
    let cert = rcgen::CertificateParams::new(vec!["probe.invalid".into()])
        .unwrap()
        .self_signed(&key)
        .unwrap();
    let valid = cert.der().clone();
    let mut trailing = valid.to_vec();
    trailing.push(0);
    let mut hidden_trailing = valid.to_vec();
    assert_eq!(&hidden_trailing[..2], &[0x30, 0x82]);
    let length = u16::from_be_bytes([hidden_trailing[2], hidden_trailing[3]]) + 2;
    hidden_trailing[2..4].copy_from_slice(&length.to_be_bytes());
    hidden_trailing.extend_from_slice(&[0x05, 0x00]);
    let other_key = rcgen::KeyPair::generate_for(&rcgen::PKCS_ECDSA_P256_SHA256).unwrap();
    let cases = [
        (vec![CertificateDer::from(vec![0x30, 0])], &key, "malformed"),
        (vec![CertificateDer::from(trailing)], &key, "trailing"),
        (
            vec![CertificateDer::from(hidden_trailing)],
            &key,
            "trailing inside certificate sequence",
        ),
        (vec![CertificateDer::from(vec![0; 4097])], &key, "oversized"),
        (
            vec![valid.clone(), valid.clone()],
            &key,
            "unexpected intermediate",
        ),
        (
            vec![valid.clone()],
            &other_key,
            "wrong CertificateVerify signer",
        ),
    ];
    for (chain, signer, label) in cases {
        let signing_key = rustls::crypto::ring::sign::any_supported_type(
            &PrivatePkcs8KeyDer::from(signer.serialize_der()).into(),
        )
        .unwrap();
        let certified = rustls::sign::CertifiedKey::new(chain, signing_key);
        let (mut crypto, _) = nearby_quic_spike::tls::server_config().unwrap();
        crypto.cert_resolver = Arc::new(ExactCertificate(Arc::new(certified)));
        let config = quinn::ServerConfig::with_crypto(Arc::new(
            quinn::crypto::rustls::QuicServerConfig::try_from(crypto).unwrap(),
        ));
        let server = quinn::Endpoint::server(config, local()).unwrap();
        let peer = server.local_addr().unwrap();
        let server_task = tokio::spawn(async move {
            let incoming = server.accept().await.unwrap();
            tokio::time::timeout(Duration::from_secs(2), incoming)
                .await
                .unwrap()
                .is_err()
        });
        let (crypto, checks) = nearby_quic_spike::tls::client_config(&pin).unwrap();
        let mut client = quinn::Endpoint::client(local()).unwrap();
        client.set_default_client_config(quinn::ClientConfig::new(Arc::new(
            quinn::crypto::rustls::QuicClientConfig::try_from(crypto).unwrap(),
        )));
        let result = tokio::time::timeout(
            Duration::from_secs(2),
            client.connect(peer, "probe.invalid").unwrap(),
        )
        .await
        .unwrap();
        assert!(result.is_err(), "{label} must fail the real handshake");
        assert_eq!(
            checks.load(Ordering::Relaxed),
            1,
            "{label} must reach the real verifier"
        );
        assert!(
            server_task.await.unwrap(),
            "{label} must fail before server application exchange"
        );
        client.close(1u32.into(), b"test-ended");
    }
}

#[tokio::test]
async fn whole_probe_requires_fin_caps_stream_and_rejects_wrong_exporter_context() {
    use std::sync::Arc;
    for case in ["oversized", "missing FIN", "wrong exporter context"] {
        let server = start_server_sessions(local(), 1, Duration::from_millis(400))
            .await
            .unwrap();
        let (crypto, _) = nearby_quic_spike::tls::client_config(&server.spki).unwrap();
        let mut endpoint = quinn::Endpoint::client(local()).unwrap();
        endpoint.set_default_client_config(quinn::ClientConfig::new(Arc::new(
            quinn::crypto::rustls::QuicClientConfig::try_from(crypto).unwrap(),
        )));
        let connection = endpoint
            .connect(server.address, "probe.invalid")
            .unwrap()
            .await
            .unwrap();
        let (mut send, _recv) = connection.open_bi().await.unwrap();
        let mut exporter = [0u8; 32];
        connection
            .export_keying_material(
                &mut exporter,
                b"EXPORTER-flynes-m0a-quic-v1",
                if case == "wrong exporter context" {
                    b"wrong-context"
                } else {
                    b"probe-stream-datagram-v1"
                },
            )
            .unwrap();
        let digest = ring::digest::digest(&ring::digest::SHA256, &exporter);
        let bytes = if case == "oversized" {
            vec![0; 129]
        } else {
            [b"client-stream-v1:".as_slice(), digest.as_ref()].concat()
        };
        send.write_all(&bytes).await.unwrap();
        if case != "missing FIN" {
            send.finish().unwrap();
        }
        let error = server.handle.await.unwrap().unwrap_err().to_string();
        assert!(
            if case == "missing FIN" {
                error.contains("deadline")
            } else {
                !error.contains("deadline")
            },
            "{case}: {error}"
        );
        endpoint.close(1u32.into(), b"test-ended");
    }
}

#[tokio::test]
async fn zero_excessive_deadlines_and_session_counts_fail_before_bind() {
    for deadline in [Duration::ZERO, Duration::from_secs(61)] {
        assert!(start_server_sessions(local(), 1, deadline).await.is_err());
        assert!(
            run_client_deadline(local(), "127.0.0.1:9".parse().unwrap(), &[], deadline)
                .await
                .is_err()
        );
    }
    for count in [0, 3, usize::MAX] {
        assert!(
            start_server_sessions(local(), count, Duration::from_secs(1))
                .await
                .is_err()
        );
    }
}
