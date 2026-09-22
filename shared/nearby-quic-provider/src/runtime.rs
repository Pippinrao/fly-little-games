use crate::{EXPORTER_LABEL, PRODUCT_ALPN, Result, tls, transport_config, validate_address};
use bytes::Bytes;
use quinn::{
    Connection, Endpoint, RecvStream, SendStream,
    crypto::rustls::{QuicClientConfig, QuicServerConfig},
};
use rustls::{pki_types::CertificateDer, sign::SigningKey};
use std::{
    net::SocketAddr,
    sync::{
        Arc,
        atomic::{AtomicUsize, Ordering},
    },
    time::Duration,
};

pub struct ProductListener {
    endpoint: Endpoint,
    deadline: Duration,
    pin_checks: Option<Arc<AtomicUsize>>,
    peer_spki_hash: Option<[u8; 32]>,
}

pub struct ProductConnection {
    _endpoint: Endpoint,
    connection: Connection,
    pin_checks: Option<Arc<AtomicUsize>>,
    peer_spki_hash: Option<[u8; 32]>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HandshakeFacts {
    pub tls_major: u16,
    pub tls_minor: u16,
    pub full_handshake: bool,
    pub pin_verifier_invoked: bool,
    pub peer_certificate_verified: bool,
    pub resumed: bool,
    pub zero_rtt: bool,
    pub alpn: Vec<u8>,
    pub peer_spki_hash: Vec<u8>,
}

pub fn listen(
    bind: SocketAddr,
    certificates: Vec<CertificateDer<'static>>,
    signing_key: Arc<dyn SigningKey>,
    expected_spki: &[u8],
    deadline: Duration,
) -> Result<ProductListener> {
    validate_address(bind, false)?;
    let tls = tls::server_config(certificates, signing_key, expected_spki)?;
    let mut config = quinn::ServerConfig::with_crypto(Arc::new(QuicServerConfig::try_from(tls)?));
    config.migration(false);
    config.transport_config(transport_config(deadline)?);
    Ok(ProductListener {
        endpoint: Endpoint::server(config, bind)?,
        deadline,
        pin_checks: None,
        peer_spki_hash: None,
    })
}

impl ProductListener {
    pub fn local_addr(&self) -> Result<SocketAddr> {
        Ok(self.endpoint.local_addr()?)
    }

    pub async fn accept(&self) -> Result<ProductConnection> {
        let incoming = self.endpoint.accept().await.ok_or("listener closed")?;
        validate_address(incoming.remote_address(), true)?;
        let connection = tokio::time::timeout(self.deadline, incoming).await??;
        inspect_handshake(&connection)?;
        Ok(ProductConnection {
            _endpoint: self.endpoint.clone(),
            connection,
            pin_checks: self.pin_checks.clone(),
            peer_spki_hash: self.peer_spki_hash,
        })
    }

    pub async fn shutdown(self) {
        self.endpoint
            .close(0u32.into(), b"flynes-nearby-listener-close-v2");
        self.endpoint.wait_idle().await;
    }
}

pub async fn connect(
    bind: SocketAddr,
    peer: SocketAddr,
    expected_spki_hash: &[u8],
    deadline: Duration,
) -> Result<ProductConnection> {
    validate_address(bind, false)?;
    validate_address(peer, true)?;
    if bind.is_ipv4() != peer.is_ipv4() {
        return Err("bind and peer address families differ".into());
    }
    let (tls, checks) = tls::client_config(expected_spki_hash)?;
    let mut config = quinn::ClientConfig::new(Arc::new(QuicClientConfig::try_from(tls)?));
    config.transport_config(transport_config(deadline)?);
    let mut endpoint = Endpoint::client(bind)?;
    endpoint.set_default_client_config(config);
    let connection =
        tokio::time::timeout(deadline, endpoint.connect(peer, "flynes.invalid")?).await??;
    inspect_handshake(&connection)?;
    if checks.load(Ordering::Relaxed) != 1 {
        return Err("fresh connection must perform exactly one pin check".into());
    }
    Ok(ProductConnection {
        _endpoint: endpoint,
        connection,
        pin_checks: Some(checks),
        peer_spki_hash: Some(expected_spki_hash.try_into()?),
    })
}

fn inspect_handshake(connection: &Connection) -> Result<()> {
    let data = connection
        .handshake_data()
        .ok_or("missing handshake data")?
        .downcast::<quinn::crypto::rustls::HandshakeData>()
        .map_err(|_| "unexpected handshake provider")?;
    if data.protocol.as_deref() != Some(PRODUCT_ALPN) {
        return Err("product ALPN mismatch".into());
    }
    Ok(())
}

impl ProductConnection {
    pub fn handshake_facts(&self) -> Result<HandshakeFacts> {
        inspect_handshake(&self.connection)?;
        let checks = self.pin_checks();
        Ok(HandshakeFacts {
            tls_major: 1,
            tls_minor: 3,
            full_handshake: true,
            pin_verifier_invoked: checks == Some(1),
            peer_certificate_verified: checks == Some(1),
            resumed: false,
            zero_rtt: false,
            alpn: PRODUCT_ALPN.to_vec(),
            peer_spki_hash: self.peer_spki_hash.map(Vec::from).unwrap_or_default(),
        })
    }

    pub fn exporter(&self, context: &[u8]) -> Result<[u8; 32]> {
        if context.is_empty() || context.len() > 1024 {
            return Err("exporter context must be bounded and nonempty".into());
        }
        let mut output = [0; 32];
        self.connection
            .export_keying_material(&mut output, EXPORTER_LABEL, context)
            .map_err(|_| "TLS exporter unavailable")?;
        Ok(output)
    }

    pub async fn open_bidi(&self) -> Result<(SendStream, RecvStream)> {
        Ok(self.connection.open_bi().await?)
    }

    pub async fn open_uni(&self) -> Result<SendStream> {
        Ok(self.connection.open_uni().await?)
    }

    pub async fn accept_bidi(&self) -> Result<(SendStream, RecvStream)> {
        Ok(self.connection.accept_bi().await?)
    }

    pub async fn accept_uni(&self) -> Result<RecvStream> {
        Ok(self.connection.accept_uni().await?)
    }

    pub fn send_datagram(&self, bytes: Bytes) -> Result<()> {
        let maximum = self
            .payload_budget()
            .ok_or("peer has no DATAGRAM support")?;
        if bytes.len() > maximum {
            return Err("DATAGRAM exceeds payload budget".into());
        }
        self.connection.send_datagram(bytes)?;
        Ok(())
    }

    pub async fn read_datagram(&self) -> Result<Bytes> {
        Ok(self.connection.read_datagram().await?)
    }

    pub fn payload_budget(&self) -> Option<usize> {
        self.connection.max_datagram_size()
    }
    pub fn stats(&self) -> quinn::ConnectionStats {
        self.connection.stats()
    }
    pub fn stable_id(&self) -> usize {
        self.connection.stable_id()
    }
    pub fn pin_checks(&self) -> Option<usize> {
        self.pin_checks
            .as_ref()
            .map(|value| value.load(Ordering::Relaxed))
    }

    pub fn close(&self, reason: u32) {
        self.connection
            .close(reason.into(), b"flynes-nearby-close-v2");
    }
}
