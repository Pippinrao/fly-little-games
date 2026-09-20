use flynes_nearby_quic_provider::{
    EXPORTER_LABEL, PRODUCT_ALPN, Result, runtime, tls, transport_config,
};
use quinn::{
    Endpoint,
    crypto::rustls::{QuicClientConfig, QuicServerConfig},
};
use rcgen::{CertificateParams, KeyPair, PKCS_ECDSA_P256_SHA256, PublicKeyData};
use rustls::{
    crypto::ring::sign::any_supported_type,
    pki_types::{CertificateDer, PrivatePkcs8KeyDer},
};
use std::{ffi::c_void, sync::atomic::AtomicUsize};
use std::{
    net::SocketAddr,
    sync::{Arc, atomic::Ordering},
    time::Duration,
};

fn local() -> SocketAddr {
    "127.0.0.1:0".parse().unwrap()
}

fn pin_hash(spki: &[u8]) -> Vec<u8> {
    ring::digest::digest(&ring::digest::SHA256, spki)
        .as_ref()
        .to_vec()
}

fn material() -> (
    Vec<CertificateDer<'static>>,
    Arc<dyn rustls::sign::SigningKey>,
    Vec<u8>,
) {
    let key = KeyPair::generate_for(&PKCS_ECDSA_P256_SHA256).unwrap();
    let spki = key.subject_public_key_info();
    let cert = CertificateParams::new(vec!["flynes.invalid".into()])
        .unwrap()
        .self_signed(&key)
        .unwrap();
    let signing =
        any_supported_type(&PrivatePkcs8KeyDer::from(key.serialize_der()).into()).unwrap();
    (vec![cert.der().clone()], signing, spki)
}

struct SignerContext {
    key: KeyPair,
    retains: AtomicUsize,
    releases: AtomicUsize,
    signs: AtomicUsize,
}

unsafe extern "C" fn retain_signer(context: *mut c_void) {
    let context = unsafe { &*(context as *const SignerContext) };
    context.retains.fetch_add(1, Ordering::Relaxed);
}

unsafe extern "C" fn release_signer(context: *mut c_void) {
    let context = unsafe { &*(context as *const SignerContext) };
    context.releases.fetch_add(1, Ordering::Relaxed);
}

unsafe extern "C" fn sign(
    context: *mut c_void,
    message: *const u8,
    message_size: usize,
    signature: *mut u8,
    signature_capacity: usize,
) -> usize {
    let context = unsafe { &*(context as *const SignerContext) };
    let message = unsafe { std::slice::from_raw_parts(message, message_size) };
    let Ok(value) = rcgen::SigningKey::sign(&context.key, message) else {
        return 0;
    };
    if value.len() > signature_capacity {
        return 0;
    }
    unsafe { std::ptr::copy_nonoverlapping(value.as_ptr(), signature, value.len()) };
    context.signs.fetch_add(1, Ordering::Relaxed);
    value.len()
}

#[tokio::test]
async fn product_runtime_uses_opaque_tls_signer_without_private_key_export() -> Result<()> {
    let key = KeyPair::generate_for(&PKCS_ECDSA_P256_SHA256)?;
    let spki = key.subject_public_key_info();
    let cert = CertificateParams::new(vec!["flynes.invalid".into()])?.self_signed(&key)?;
    let context = Box::new(SignerContext {
        key,
        retains: AtomicUsize::new(0),
        releases: AtomicUsize::new(0),
        signs: AtomicUsize::new(0),
    });
    let callbacks = tls::TlsSignerCallbacks {
        context: (&*context as *const SignerContext).cast_mut().cast(),
        retain: Some(retain_signer),
        release: Some(release_signer),
        sign: Some(sign),
    };
    let signing = Arc::new(tls::OpaqueTlsSigningKey::new(callbacks, &spki)?);
    let listener = Arc::new(runtime::listen(
        local(),
        vec![cert.der().clone()],
        signing.clone(),
        &spki,
        Duration::from_secs(2),
    )?);
    let address = listener.local_addr()?;
    let server_listener = listener.clone();
    let server = tokio::spawn(async move {
        let connection = server_listener.accept().await?;
        let exporter = connection.exporter(b"opaque-material-test")?;
        connection.close(0);
        Ok::<_, Box<dyn std::error::Error + Send + Sync>>(exporter)
    });
    let client =
        runtime::connect(local(), address, &pin_hash(&spki), Duration::from_secs(2)).await?;
    let client_facts = client.handshake_facts()?;
    assert_eq!(client_facts.peer_spki_hash, pin_hash(&spki));
    assert!(client_facts.peer_certificate_verified);
    let exporter = client.exporter(b"opaque-material-test")?;
    assert_eq!(client.pin_checks(), Some(1));
    assert_eq!(server.await??, exporter);
    client.close(0);
    assert_eq!(context.retains.load(Ordering::Relaxed), 1);
    assert!(context.signs.load(Ordering::Relaxed) > 0);
    drop(client);
    let listener = Arc::try_unwrap(listener).map_err(|_| "listener still referenced")?;
    listener.shutdown().await;
    drop(signing);
    let until = std::time::Instant::now() + Duration::from_secs(1);
    while context.releases.load(Ordering::Relaxed) == 0 && std::time::Instant::now() < until {
        tokio::task::yield_now().await;
    }
    assert_eq!(context.releases.load(Ordering::Relaxed), 1);
    Ok(())
}

#[tokio::test]
async fn product_alpn_exact_pin_exporter_streams_datagram_and_fin_work() -> Result<()> {
    assert_eq!(PRODUCT_ALPN, b"flynes-nearby/2");
    assert_eq!(EXPORTER_LABEL, b"EXPORTER-flynes-nearby-v1");
    let (certificates, signing, spki) = material();
    let server_tls = tls::server_config(certificates, signing, &spki)?;
    assert_eq!(server_tls.alpn_protocols, vec![PRODUCT_ALPN.to_vec()]);
    assert!(!server_tls.session_storage.can_cache());
    assert!(!server_tls.ticketer.enabled());
    assert_eq!(server_tls.send_tls13_tickets, 0);
    assert_eq!(server_tls.max_early_data_size, 0);

    let mut server_config =
        quinn::ServerConfig::with_crypto(Arc::new(QuicServerConfig::try_from(server_tls)?));
    server_config.transport_config(transport_config(Duration::from_secs(2))?);
    let server = Endpoint::server(server_config, local())?;
    let address = server.local_addr()?;
    let server_task = tokio::spawn(async move {
        let connection = tokio::time::timeout(Duration::from_secs(2), server.accept())
            .await?
            .ok_or("listener closed")?
            .await?;
        assert_eq!(
            connection
                .handshake_data()
                .unwrap()
                .downcast::<quinn::crypto::rustls::HandshakeData>()
                .unwrap()
                .protocol
                .as_deref(),
            Some(PRODUCT_ALPN)
        );
        let mut exporter = [0; 32];
        connection
            .export_keying_material(&mut exporter, EXPORTER_LABEL, b"link-1")
            .map_err(|_| "server exporter unavailable")?;
        let (mut send, mut recv) = connection.accept_bi().await?;
        assert_eq!(recv.read_to_end(64).await?, b"hello");
        send.write_all(&exporter).await?;
        send.finish()?;
        assert_eq!(connection.read_datagram().await?.as_ref(), b"dgram");
        connection.send_datagram(bytes::Bytes::from_static(b"ack"))?;
        let _ = connection.closed().await;
        Ok::<_, Box<dyn std::error::Error + Send + Sync>>(exporter)
    });

    let (client_tls, checks) = tls::client_config(&pin_hash(&spki))?;
    assert!(!client_tls.enable_early_data);
    let mut client_config =
        quinn::ClientConfig::new(Arc::new(QuicClientConfig::try_from(client_tls)?));
    client_config.transport_config(transport_config(Duration::from_secs(2))?);
    let mut client = Endpoint::client(local())?;
    client.set_default_client_config(client_config);
    let connection = client.connect(address, "flynes.invalid")?.await?;
    let mut exporter = [0; 32];
    connection
        .export_keying_material(&mut exporter, EXPORTER_LABEL, b"link-1")
        .map_err(|_| "client exporter unavailable")?;
    let mut alternate = [0; 32];
    connection
        .export_keying_material(&mut alternate, EXPORTER_LABEL, b"link-2")
        .map_err(|_| "alternate exporter unavailable")?;
    assert_ne!(exporter, alternate);
    let (mut send, mut recv) = connection.open_bi().await?;
    send.write_all(b"hello").await?;
    send.finish()?;
    assert_eq!(recv.read_to_end(64).await?, exporter);
    connection.send_datagram(bytes::Bytes::from_static(b"dgram"))?;
    assert_eq!(connection.read_datagram().await?.as_ref(), b"ack");
    assert!(connection.max_datagram_size().is_some());
    assert_eq!(checks.load(Ordering::Relaxed), 1);
    connection.close(0u32.into(), b"done");
    assert_eq!(server_task.await??, exporter);
    Ok(())
}

#[tokio::test]
async fn wrong_pin_and_deadline_cancel_before_application_data() -> Result<()> {
    let (certificates, signing, spki) = material();
    let (other_certificates, other_signing, other_spki) = material();
    drop((other_certificates, other_signing));
    let server_tls = tls::server_config(certificates, signing, &spki)?;
    let mut config =
        quinn::ServerConfig::with_crypto(Arc::new(QuicServerConfig::try_from(server_tls)?));
    config.transport_config(transport_config(Duration::from_millis(200))?);
    let server = Endpoint::server(config, local())?;
    let address = server.local_addr()?;
    let server_task = tokio::spawn(async move {
        let incoming = server.accept().await.ok_or("listener closed")?;
        Ok::<_, Box<dyn std::error::Error + Send + Sync>>(incoming.await.is_err())
    });
    let (client_tls, checks) = tls::client_config(&pin_hash(&other_spki))?;
    let mut client_config =
        quinn::ClientConfig::new(Arc::new(QuicClientConfig::try_from(client_tls)?));
    client_config.transport_config(transport_config(Duration::from_millis(200))?);
    let mut client = Endpoint::client(local())?;
    client.set_default_client_config(client_config);
    assert!(
        tokio::time::timeout(
            Duration::from_secs(2),
            client.connect(address, "flynes.invalid")?
        )
        .await?
        .is_err()
    );
    assert_eq!(checks.load(Ordering::Relaxed), 1);
    assert!(server_task.await??);

    assert!(transport_config(Duration::ZERO).is_err());
    assert!(transport_config(Duration::from_secs(61)).is_err());
    Ok(())
}
