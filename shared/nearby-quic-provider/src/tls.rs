use crate::{PRODUCT_ALPN, Result};
use rustls::{
    CertificateError, DigitallySignedStruct, Error, SignatureScheme,
    client::danger::{HandshakeSignatureValid, ServerCertVerified, ServerCertVerifier},
    crypto::{CryptoProvider, ring::default_provider, verify_tls13_signature},
    pki_types::{CertificateDer, ServerName, UnixTime},
    sign::{CertifiedKey, SigningKey},
};
use std::sync::{
    Arc,
    atomic::{AtomicUsize, Ordering},
};
use std::{
    ffi::c_void,
    fmt,
    panic::{AssertUnwindSafe, catch_unwind},
};
use x509_parser::{prelude::*, x509::SubjectPublicKeyInfo};

pub const PIN_LENGTH: usize = 91;
const MAX_CERT_LENGTH: usize = 4096;
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
        .and_then(|value| value.as_oid().ok())
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
    pin_hash: [u8; 32],
    checks: Arc<AtomicUsize>,
    provider: Arc<CryptoProvider>,
}

fn verify_exact_certificate(
    cert: &CertificateDer<'_>,
    intermediates: &[CertificateDer<'_>],
    pin_hash: &[u8; 32],
) -> std::result::Result<(), Error> {
    let bad = || Error::InvalidCertificate(CertificateError::BadEncoding);
    if !intermediates.is_empty() || cert.is_empty() || cert.len() > MAX_CERT_LENGTH {
        return Err(bad());
    }
    let (rest, parsed) = X509Certificate::from_der(cert.as_ref()).map_err(|_| bad())?;
    let spki = parsed.public_key().raw;
    if !rest.is_empty() || validate_pin(spki).is_err() {
        return Err(bad());
    }
    let actual_hash = ring::digest::digest(&ring::digest::SHA256, spki);
    if actual_hash.as_ref() != pin_hash {
        return Err(Error::General("peer DER-SPKI hash pin mismatch".into()));
    }
    Ok(())
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
        verify_exact_certificate(cert, intermediates, &self.pin_hash)?;
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

#[derive(Debug)]
struct ExactCertificate(Arc<CertifiedKey>);

impl rustls::server::ResolvesServerCert for ExactCertificate {
    fn resolve(&self, _hello: rustls::server::ClientHello<'_>) -> Option<Arc<CertifiedKey>> {
        Some(self.0.clone())
    }
}

pub fn client_config(pin_hash: &[u8]) -> Result<(rustls::ClientConfig, Arc<AtomicUsize>)> {
    let pin_hash: [u8; 32] = pin_hash
        .try_into()
        .map_err(|_| "pin must be one SHA-256 digest")?;
    if pin_hash.iter().all(|value| *value == 0) {
        return Err("pin hash must be nonzero".into());
    }
    let provider = Arc::new(default_provider());
    let checks = Arc::new(AtomicUsize::new(0));
    let verifier = Arc::new(PinnedVerifier {
        pin_hash,
        checks: checks.clone(),
        provider: provider.clone(),
    });
    let mut config = rustls::ClientConfig::builder_with_provider(provider)
        .with_protocol_versions(&[&rustls::version::TLS13])?
        .dangerous()
        .with_custom_certificate_verifier(verifier)
        .with_no_client_auth();
    config.alpn_protocols = vec![PRODUCT_ALPN.to_vec()];
    config.resumption = rustls::client::Resumption::disabled();
    config.enable_early_data = false;
    config.key_log = Arc::new(rustls::NoKeyLog);
    Ok((config, checks))
}

fn validate_material(
    certificates: &[CertificateDer<'static>],
    signing_key: &Arc<dyn SigningKey>,
    expected_spki: &[u8],
) -> Result<()> {
    validate_pin(expected_spki)?;
    if certificates.len() != 1
        || certificates[0].is_empty()
        || certificates[0].len() > MAX_CERT_LENGTH
    {
        return Err("exactly one bounded end-entity certificate is required".into());
    }
    let (rest, parsed) = X509Certificate::from_der(certificates[0].as_ref())?;
    if !rest.is_empty() || parsed.public_key().raw != expected_spki {
        return Err("TLS certificate SPKI does not match material pin".into());
    }
    if let Some(actual) = signing_key.public_key()
        && actual.as_ref() != expected_spki
    {
        return Err("TLS signing key does not match material pin".into());
    }
    Ok(())
}

pub fn server_config(
    certificates: Vec<CertificateDer<'static>>,
    signing_key: Arc<dyn SigningKey>,
    expected_spki: &[u8],
) -> Result<rustls::ServerConfig> {
    validate_material(&certificates, &signing_key, expected_spki)?;
    let certified = CertifiedKey::new(certificates, signing_key);
    let mut config = rustls::ServerConfig::builder_with_provider(Arc::new(default_provider()))
        .with_protocol_versions(&[&rustls::version::TLS13])?
        .with_no_client_auth()
        .with_cert_resolver(Arc::new(ExactCertificate(Arc::new(certified))));
    config.alpn_protocols = vec![PRODUCT_ALPN.to_vec()];
    config.session_storage = Arc::new(rustls::server::NoServerSessionStorage {});
    config.ticketer = Arc::new(NoTickets);
    config.send_tls13_tickets = 0;
    config.max_early_data_size = 0;
    config.key_log = Arc::new(rustls::NoKeyLog);
    Ok(config)
}

pub type TlsContextRetain = unsafe extern "C" fn(*mut c_void);
pub type TlsContextRelease = unsafe extern "C" fn(*mut c_void);
pub type TlsSign = unsafe extern "C" fn(
    context: *mut c_void,
    message: *const u8,
    message_size: usize,
    signature: *mut u8,
    signature_capacity: usize,
) -> usize;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct TlsSignerCallbacks {
    pub context: *mut c_void,
    pub retain: Option<TlsContextRetain>,
    pub release: Option<TlsContextRelease>,
    pub sign: Option<TlsSign>,
}

struct OpaqueSignerInner {
    callbacks: TlsSignerCallbacks,
    spki: rustls::pki_types::SubjectPublicKeyInfoDer<'static>,
}

unsafe impl Send for OpaqueSignerInner {}
unsafe impl Sync for OpaqueSignerInner {}

impl Drop for OpaqueSignerInner {
    fn drop(&mut self) {
        if let Some(release) = self.callbacks.release {
            // SAFETY: the callback table owns one retained context reference.
            unsafe { release(self.callbacks.context) };
        }
    }
}

#[derive(Clone)]
pub struct OpaqueTlsSigningKey(Arc<OpaqueSignerInner>);

impl fmt::Debug for OpaqueTlsSigningKey {
    fn fmt(&self, output: &mut fmt::Formatter<'_>) -> fmt::Result {
        output
            .debug_struct("OpaqueTlsSigningKey")
            .finish_non_exhaustive()
    }
}

impl OpaqueTlsSigningKey {
    pub fn new(callbacks: TlsSignerCallbacks, spki: &[u8]) -> Result<Self> {
        validate_pin(spki)?;
        if callbacks.retain.is_none() || callbacks.release.is_none() || callbacks.sign.is_none() {
            return Err("complete TLS signer callbacks are required".into());
        }
        if let Some(retain) = callbacks.retain {
            // SAFETY: the callback contract guarantees a valid context.
            unsafe { retain(callbacks.context) };
        }
        Ok(Self(Arc::new(OpaqueSignerInner {
            callbacks,
            spki: rustls::pki_types::SubjectPublicKeyInfoDer::from(spki.to_vec()),
        })))
    }
}

impl SigningKey for OpaqueTlsSigningKey {
    fn choose_scheme(&self, offered: &[SignatureScheme]) -> Option<Box<dyn rustls::sign::Signer>> {
        offered
            .contains(&SignatureScheme::ECDSA_NISTP256_SHA256)
            .then(|| Box::new(OpaqueMessageSigner(self.0.clone())) as Box<dyn rustls::sign::Signer>)
    }

    fn public_key(&self) -> Option<rustls::pki_types::SubjectPublicKeyInfoDer<'_>> {
        Some(self.0.spki.clone())
    }

    fn algorithm(&self) -> rustls::SignatureAlgorithm {
        rustls::SignatureAlgorithm::ECDSA
    }
}

struct OpaqueMessageSigner(Arc<OpaqueSignerInner>);

impl fmt::Debug for OpaqueMessageSigner {
    fn fmt(&self, output: &mut fmt::Formatter<'_>) -> fmt::Result {
        output
            .debug_struct("OpaqueMessageSigner")
            .finish_non_exhaustive()
    }
}

impl rustls::sign::Signer for OpaqueMessageSigner {
    fn sign(&self, message: &[u8]) -> std::result::Result<Vec<u8>, Error> {
        let callback = self
            .0
            .callbacks
            .sign
            .ok_or_else(|| Error::General("TLS signer callback unavailable".into()))?;
        let mut signature = [0u8; 80];
        let result = catch_unwind(AssertUnwindSafe(|| {
            // SAFETY: inputs remain valid for the synchronous callback and output is bounded.
            unsafe {
                callback(
                    self.0.callbacks.context,
                    message.as_ptr(),
                    message.len(),
                    signature.as_mut_ptr(),
                    signature.len(),
                )
            }
        }))
        .map_err(|_| Error::General("TLS signer callback panicked".into()))?;
        if !(8..=signature.len()).contains(&result) {
            return Err(Error::General("TLS signer callback failed".into()));
        }
        signature[result..].fill(0);
        Ok(signature[..result].to_vec())
    }

    fn scheme(&self) -> SignatureScheme {
        SignatureScheme::ECDSA_NISTP256_SHA256
    }
}
