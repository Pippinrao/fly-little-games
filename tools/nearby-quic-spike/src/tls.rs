//! Test-only, manually provisioned listener SPKI authentication. No BLE identity binding.
use crate::Result;
use rcgen::{CertificateParams, KeyPair, PKCS_ECDSA_P256_SHA256, PublicKeyData};
use rustls::{
    CertificateError, DigitallySignedStruct, Error, SignatureScheme,
    client::danger::{HandshakeSignatureValid, ServerCertVerified, ServerCertVerifier},
    crypto::{CryptoProvider, ring::default_provider, verify_tls13_signature},
    pki_types::{CertificateDer, PrivatePkcs8KeyDer, ServerName, UnixTime},
};
use std::sync::{
    Arc,
    atomic::{AtomicUsize, Ordering},
};
use x509_parser::{prelude::*, x509::SubjectPublicKeyInfo};

pub const ALPN: &[u8] = b"flynes-m0a-quic-v1";
pub const PIN_LENGTH: usize = 91;
const MAX_CERT_LENGTH: usize = 4096;
// Canonical DER encoding of id-ecPublicKey, named prime256v1 and a 65-byte BIT STRING.
// x509-parser still parses and validates the structure/OIDs below; this additionally
// excludes non-minimal encodings and hidden fields from the exact pin format.
const SPKI_PREFIX: &[u8] = &[
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a,
    0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00,
];

pub fn validate_pin(pin: &[u8]) -> Result<()> {
    if pin.len() != PIN_LENGTH || !pin.starts_with(SPKI_PREFIX) {
        return Err("pin must be exact canonical 91-byte P-256 DER-SPKI".into());
    }
    let (rest, spki) = SubjectPublicKeyInfo::from_der(pin).map_err(|_| "malformed DER-SPKI")?;
    let curve = spki
        .algorithm
        .parameters
        .as_ref()
        .and_then(|p| p.as_oid().ok())
        .ok_or("missing named curve")?;
    let point = &spki.subject_public_key;
    if !rest.is_empty()
        || spki.raw != pin
        || spki.algorithm.algorithm.to_id_string() != "1.2.840.10045.2.1"
        || curve.to_id_string() != "1.2.840.10045.3.1.7"
        || point.unused_bits != 0
        || point.data.len() != 65
        || point.data[0] != 4
    {
        return Err("pin must contain one named P-256 uncompressed point".into());
    }
    // ring's P-256 agreement validates the public point (including on-curve).
    // The throwaway shared secret is neither retained nor exposed.
    let private = ring::agreement::EphemeralPrivateKey::generate(
        &ring::agreement::ECDH_P256,
        &ring::rand::SystemRandom::new(),
    )
    .map_err(|_| "P-256 validation key generation failed")?;
    ring::agreement::agree_ephemeral(
        private,
        &ring::agreement::UnparsedPublicKey::new(&ring::agreement::ECDH_P256, &point.data),
        |_| (),
    )
    .map_err(|_| "invalid P-256 public point")?;
    Ok(())
}

#[derive(Debug)]
struct PinnedVerifier {
    pin: Vec<u8>,
    checks: Arc<AtomicUsize>,
    provider: Arc<CryptoProvider>,
}

impl ServerCertVerifier for PinnedVerifier {
    fn verify_server_cert(
        &self,
        cert: &CertificateDer<'_>,
        intermediates: &[CertificateDer<'_>],
        _name: &ServerName<'_>,
        _ocsp: &[u8],
        _now: UnixTime,
    ) -> std::result::Result<ServerCertVerified, Error> {
        self.checks.fetch_add(1, Ordering::Relaxed);
        let bad = || Error::InvalidCertificate(CertificateError::BadEncoding);
        if !intermediates.is_empty() || cert.is_empty() || cert.len() > MAX_CERT_LENGTH {
            return Err(bad());
        }
        let (rest, parsed) = X509Certificate::from_der(cert.as_ref()).map_err(|_| bad())?;
        let spki = parsed.public_key().raw;
        if !rest.is_empty() || validate_pin(spki).is_err() {
            return Err(bad());
        }
        // Full PUBLIC DER-SPKI comparison, during handshake, before any application data.
        // Hostname/CA/time trust is deliberately replaced by the operator's exact pin.
        if spki != self.pin {
            return Err(Error::General("listener SPKI pin mismatch".into()));
        }
        Ok(ServerCertVerified::assertion())
    }

    fn verify_tls12_signature(
        &self,
        _message: &[u8],
        _cert: &CertificateDer<'_>,
        _dss: &DigitallySignedStruct,
    ) -> std::result::Result<HandshakeSignatureValid, Error> {
        Err(Error::General("TLS 1.2 is forbidden".into()))
    }

    fn verify_tls13_signature(
        &self,
        message: &[u8],
        cert: &CertificateDer<'_>,
        dss: &DigitallySignedStruct,
    ) -> std::result::Result<HandshakeSignatureValid, Error> {
        if dss.scheme != SignatureScheme::ECDSA_NISTP256_SHA256 {
            return Err(Error::General("only ECDSA P-256 SHA-256 is allowed".into()));
        }
        verify_tls13_signature(
            message,
            cert,
            dss,
            &self.provider.signature_verification_algorithms,
        )
    }

    fn supported_verify_schemes(&self) -> Vec<SignatureScheme> {
        vec![SignatureScheme::ECDSA_NISTP256_SHA256]
    }
}

#[derive(Debug)]
struct NoTickets;
impl rustls::server::ProducesTickets for NoTickets {
    fn enabled(&self) -> bool {
        false
    }
    fn lifetime(&self) -> u32 {
        0
    }
    fn encrypt(&self, _plain: &[u8]) -> Option<Vec<u8>> {
        None
    }
    fn decrypt(&self, _cipher: &[u8]) -> Option<Vec<u8>> {
        None
    }
}

pub fn client_config(pin: &[u8]) -> Result<(rustls::ClientConfig, Arc<AtomicUsize>)> {
    validate_pin(pin)?;
    let provider = Arc::new(default_provider());
    let checks = Arc::new(AtomicUsize::new(0));
    let verifier = Arc::new(PinnedVerifier {
        pin: pin.to_vec(),
        checks: checks.clone(),
        provider: provider.clone(),
    });
    let mut config = rustls::ClientConfig::builder_with_provider(provider)
        .with_protocol_versions(&[&rustls::version::TLS13])?
        .dangerous()
        .with_custom_certificate_verifier(verifier)
        .with_no_client_auth();
    config.alpn_protocols = vec![ALPN.to_vec()];
    config.resumption = rustls::client::Resumption::disabled();
    config.enable_early_data = false;
    config.key_log = Arc::new(rustls::NoKeyLog);
    Ok((config, checks))
}

pub fn server_config() -> Result<(rustls::ServerConfig, Vec<u8>)> {
    let key = KeyPair::generate_for(&PKCS_ECDSA_P256_SHA256)?;
    let spki = key.subject_public_key_info();
    validate_pin(&spki)?;
    let cert = CertificateParams::new(vec!["probe.invalid".into()])?.self_signed(&key)?;
    let mut config = rustls::ServerConfig::builder_with_provider(Arc::new(default_provider()))
        .with_protocol_versions(&[&rustls::version::TLS13])?
        .with_no_client_auth()
        .with_single_cert(
            vec![cert.der().clone()],
            PrivatePkcs8KeyDer::from(key.serialize_der()).into(),
        )?;
    config.alpn_protocols = vec![ALPN.to_vec()];
    config.session_storage = Arc::new(rustls::server::NoServerSessionStorage {});
    config.ticketer = Arc::new(NoTickets);
    config.send_tls13_tickets = 0;
    config.max_early_data_size = 0;
    config.key_log = Arc::new(rustls::NoKeyLog);
    Ok((config, spki))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{
        net::{TcpListener, TcpStream},
        time::Duration,
    };

    // QUIC itself cannot negotiate TLS 1.2. Exercise the exact rustls policy and its
    // defensive TLS 1.2 signature callback over real loopback TCP, with actual signatures.
    fn tcp_handshake(
        client: rustls::ClientConfig,
        server: rustls::ServerConfig,
    ) -> (Result<()>, Result<()>) {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server_task = std::thread::spawn(move || -> Result<()> {
            let (mut stream, _) = listener.accept()?;
            stream.set_read_timeout(Some(Duration::from_secs(2)))?;
            stream.set_write_timeout(Some(Duration::from_secs(2)))?;
            let mut connection = rustls::ServerConnection::new(Arc::new(server))?;
            while connection.is_handshaking() {
                connection.complete_io(&mut stream)?;
            }
            Ok(())
        });
        let client_result = (|| -> Result<()> {
            let mut stream = TcpStream::connect_timeout(&address, Duration::from_secs(2))?;
            stream.set_read_timeout(Some(Duration::from_secs(2)))?;
            stream.set_write_timeout(Some(Duration::from_secs(2)))?;
            let mut connection = rustls::ClientConnection::new(
                Arc::new(client),
                ServerName::try_from("probe.invalid")?,
            )?;
            while connection.is_handshaking() {
                connection.complete_io(&mut stream)?;
            }
            Ok(())
        })();
        (client_result, server_task.join().unwrap())
    }

    #[test]
    fn tls13_policy_accepts_real_tcp_handshake_and_disables_server_resumption() {
        let (server, pin) = server_config().unwrap();
        assert!(!server.session_storage.can_cache());
        assert!(!server.ticketer.enabled());
        assert_eq!(server.send_tls13_tickets, 0);
        assert_eq!(server.max_early_data_size, 0);
        let (client, checks) = client_config(&pin).unwrap();
        assert!(!client.enable_early_data);
        let (client_result, server_result) = tcp_handshake(client, server);
        client_result.unwrap();
        server_result.unwrap();
        assert_eq!(checks.load(Ordering::Relaxed), 1);
    }

    #[test]
    fn tls12_negotiation_and_defensive_signature_callback_both_reject_real_handshakes() {
        let key = KeyPair::generate_for(&PKCS_ECDSA_P256_SHA256).unwrap();
        let pin = key.subject_public_key_info();
        let cert = CertificateParams::new(vec!["probe.invalid".into()])
            .unwrap()
            .self_signed(&key)
            .unwrap();
        let mut tls12_server =
            rustls::ServerConfig::builder_with_provider(Arc::new(default_provider()))
                .with_protocol_versions(&[&rustls::version::TLS12])
                .unwrap()
                .with_no_client_auth()
                .with_single_cert(
                    vec![cert.der().clone()],
                    PrivatePkcs8KeyDer::from(key.serialize_der()).into(),
                )
                .unwrap();
        tls12_server.alpn_protocols = vec![ALPN.to_vec()];
        let (normal_client, checks) = client_config(&pin).unwrap();
        let (client_result, server_result) = tcp_handshake(normal_client, tls12_server.clone());
        assert!(client_result.is_err() && server_result.is_err());
        assert_eq!(
            checks.load(Ordering::Relaxed),
            0,
            "TLS1.2 rejected before certificate exchange"
        );

        // Test-only TLS1.2-capable builder. Production config never enables this version.
        let provider = Arc::new(default_provider());
        let verifier = Arc::new(PinnedVerifier {
            pin,
            checks: checks.clone(),
            provider: provider.clone(),
        });
        let mut test_client = rustls::ClientConfig::builder_with_provider(provider)
            .with_protocol_versions(&[&rustls::version::TLS12])
            .unwrap()
            .dangerous()
            .with_custom_certificate_verifier(verifier)
            .with_no_client_auth();
        test_client.alpn_protocols = vec![ALPN.to_vec()];
        let (client_result, server_result) = tcp_handshake(test_client.clone(), tls12_server);
        assert!(
            client_result
                .unwrap_err()
                .to_string()
                .contains("TLS 1.2 is forbidden")
        );
        assert!(server_result.is_err());
        assert_eq!(
            checks.load(Ordering::Relaxed),
            1,
            "actual TLS1.2 signature path after exact pin check"
        );

        let (normal_server, _) = server_config().unwrap();
        let (client_result, server_result) = tcp_handshake(test_client, normal_server);
        assert!(
            client_result.is_err() && server_result.is_err(),
            "production server refuses TLS1.2 client"
        );
    }
}
