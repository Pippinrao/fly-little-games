use crate::{DEADLINE, Report, Result, Server, tls, validate_address};
use bytes::Bytes;
use quinn::{
    Connection, Endpoint,
    crypto::rustls::{QuicClientConfig, QuicServerConfig},
};
use std::{
    net::SocketAddr,
    sync::{Arc, atomic::Ordering},
    time::Duration,
};
use tokio::time::{Instant, timeout, timeout_at};

const LIMIT: usize = 128;
const LABEL: &[u8] = b"EXPORTER-flynes-m0a-quic-v1";
const CONTEXT: &[u8] = b"probe-stream-datagram-v1";
const ALTERNATE_CONTEXT: &[u8] = b"probe-stream-datagram-v1-different";
const CLIENT_STREAM: &[u8] = b"client-stream-v1:";
const SERVER_STREAM: &[u8] = b"server-stream-v1:";
const CLIENT_DATAGRAM: &[u8] = b"client-datagram-v1:";
const SERVER_DATAGRAM: &[u8] = b"server-datagram-v1:";
const CLIENT_DONE: &[u8] = b"client-verified-v1:";
const SERVER_DONE: &[u8] = b"server-verified-v1:";
const VERIFIED_CLOSE: &[u8] = b"probe-client-validated-final-v1";

fn validate_deadline(deadline: Duration) -> Result<()> {
    if deadline.is_zero() || deadline > Duration::from_secs(60) {
        return Err("deadline must be nonzero and at most 60 seconds".into());
    }
    Ok(())
}

fn transport(deadline: Duration) -> Result<Arc<quinn::TransportConfig>> {
    let mut config = quinn::TransportConfig::default();
    config.max_idle_timeout(Some(deadline.min(Duration::from_secs(5)).try_into()?));
    config.max_concurrent_bidi_streams(2u32.into());
    config.max_concurrent_uni_streams(0u32.into());
    config.stream_receive_window(1024u32.into());
    config.receive_window(4096u32.into());
    config.send_window(4096);
    config.datagram_receive_buffer_size(Some(4096));
    config.datagram_send_buffer_size(4096);
    Ok(Arc::new(config))
}

pub async fn start_server(bind: SocketAddr) -> Result<Server> {
    start_server_sessions(bind, 1, DEADLINE).await
}

/// One ephemeral credential, bounded to one or two sequential probes (two is for repin evidence).
pub async fn start_server_sessions(
    bind: SocketAddr,
    sessions: usize,
    deadline: Duration,
) -> Result<Server> {
    validate_address(bind, false)?;
    validate_deadline(deadline)?;
    if !(1..=2).contains(&sessions) {
        return Err("session count must be 1 or 2".into());
    }
    let end = Instant::now() + deadline;
    let (crypto, spki) = tls::server_config()?;
    let mut config =
        quinn::ServerConfig::with_crypto(Arc::new(QuicServerConfig::try_from(crypto)?));
    config.migration(false);
    config.transport_config(transport(deadline)?);
    let endpoint = Endpoint::server(config, bind)?;
    let address = endpoint.local_addr()?;
    let handle = tokio::spawn(async move {
        let result = timeout_at(end, async {
            let mut reports = Vec::new();
            for _ in 0..sessions {
                let incoming = endpoint.accept().await.ok_or("listener closed")?;
                validate_address(incoming.remote_address(), true)?;
                let connection = incoming
                    .await
                    .map_err(|e| format!("server handshake failed: {e}"))?;
                reports.push(server_exchange(&connection).await?);
            }
            Ok(reports)
        })
        .await
        .map_err(|_| "server whole-probe deadline exceeded")?;
        endpoint.close(0u32.into(), b"probe-listener-finished");
        // After peer's explicit validated close, endpoint draining sends no new application data.
        timeout_at(end, endpoint.wait_idle())
            .await
            .map_err(|_| "server drain deadline exceeded")?;
        result
    });
    Ok(Server {
        address,
        spki,
        handle,
    })
}

pub async fn run_client(bind: SocketAddr, peer: SocketAddr, pin: &[u8]) -> Result<Report> {
    run_client_deadline(bind, peer, pin, DEADLINE).await
}

pub async fn run_client_deadline(
    bind: SocketAddr,
    peer: SocketAddr,
    pin: &[u8],
    deadline: Duration,
) -> Result<Report> {
    validate_address(bind, false)?;
    validate_address(peer, true)?;
    validate_deadline(deadline)?;
    if bind.is_ipv4() != peer.is_ipv4() {
        return Err("bind and peer address families differ".into());
    }
    let end = Instant::now() + deadline;
    let (crypto, checks) = tls::client_config(pin)?; // Validate pin before binding any socket.
    let mut config = quinn::ClientConfig::new(Arc::new(QuicClientConfig::try_from(crypto)?));
    config.transport_config(transport(deadline)?);
    let mut endpoint = Endpoint::client(bind)?;
    endpoint.set_default_client_config(config);
    let result = timeout_at(end, async {
        let connection = endpoint
            .connect(peer, "probe.invalid")?
            .await
            .map_err(|e| format!("client handshake failed: {e}"))?;
        let mut report = client_exchange(&connection).await?;
        report.pin_checks = checks.load(Ordering::Relaxed);
        if report.pin_checks != 1 {
            return Err("fresh connection did not perform exactly one pin check".into());
        }
        // Final response has been read through FIN and validated before sending this close.
        connection.close(0u32.into(), VERIFIED_CLOSE);
        Ok(report)
    })
    .await
    .map_err(|_| "client whole-probe deadline exceeded")?;
    if result.is_err() {
        endpoint.close(1u32.into(), b"probe-failed");
    }
    timeout_at(end, endpoint.wait_idle())
        .await
        .map_err(|_| "client drain deadline exceeded")?;
    result
}

fn digest(connection: &Connection, context: &[u8]) -> Result<[u8; 32]> {
    let mut secret = [0u8; 32];
    connection
        .export_keying_material(&mut secret, LABEL, context)
        .map_err(|_| "TLS exporter unavailable")?;
    let digest = ring::digest::digest(&ring::digest::SHA256, &secret);
    Ok(digest.as_ref().try_into()?)
}

fn payload(prefix: &[u8], digest: &[u8; 32]) -> Vec<u8> {
    [prefix, digest.as_slice()].concat()
}

async fn write_fin(send: &mut quinn::SendStream, prefix: &[u8], digest: &[u8; 32]) -> Result<()> {
    send.write_all(&payload(prefix, digest)).await?;
    send.finish()?;
    Ok(())
}

async fn read_fin(recv: &mut quinn::RecvStream, prefix: &[u8], digest: &[u8; 32]) -> Result<()> {
    if recv.read_to_end(LIMIT).await? != payload(prefix, digest) {
        return Err("stream payload/exporter digest mismatch".into());
    }
    Ok(())
}

async fn datagrams(
    connection: &Connection,
    outgoing: &[u8],
    incoming: &[u8],
    digest: &[u8; 32],
) -> Result<()> {
    let outgoing = Bytes::from(payload(outgoing, digest));
    let expected = payload(incoming, digest);
    if connection
        .max_datagram_size()
        .is_none_or(|max| max < outgoing.len())
    {
        return Err("peer has no usable QUIC DATAGRAM support".into());
    }
    // Continue answering duplicate peer datagrams after receiving ours until the reliable
    // completion stage: a separate bounded sender runs during that stage (see exchange).
    for _ in 0..30 {
        connection.send_datagram(outgoing.clone())?;
        match timeout(Duration::from_millis(100), connection.read_datagram()).await {
            Ok(Ok(value)) if value.len() <= LIMIT && value.as_ref() == expected => return Ok(()),
            Ok(Ok(_)) => return Err("QUIC DATAGRAM payload/exporter digest mismatch".into()),
            Ok(Err(error)) => return Err(error.into()),
            Err(_) => {}
        }
    }
    Err("QUIC DATAGRAM bounded retries exhausted".into())
}

async fn repeat_datagram(connection: &Connection, prefix: &[u8], digest: &[u8; 32]) -> Result<()> {
    let bytes = Bytes::from(payload(prefix, digest));
    for _ in 0..30 {
        connection.send_datagram(bytes.clone())?;
        tokio::time::sleep(Duration::from_millis(100)).await;
    }
    Err("peer completion deadline after DATAGRAM retries".into())
}

fn report(connection: &Connection, digest: [u8; 32]) -> Result<Report> {
    let alternate_context_digest = self::digest(connection, ALTERNATE_CONTEXT)?;
    if digest == alternate_context_digest {
        return Err("exporter context separation failed".into());
    }
    Ok(Report {
        stream_verified: true,
        datagram_verified: true,
        pin_checks: 0,
        exporter_digest: digest,
        alternate_context_digest,
    })
}

async fn finish_with_retries<T>(
    complete: impl std::future::Future<Output = Result<T>>,
    retries: impl std::future::Future<Output = Result<()>>,
) -> Result<T> {
    tokio::pin!(complete);
    tokio::select! {
        biased;
        result = &mut complete => result,
        // Auxiliary DATAGRAM delivery may observe the close between polling these
        // branches. Resolve the same authoritative stream/close validation future,
        // including its error, even in that race. Callers retain the whole-probe
        // timeout around this helper; exhausted retries cannot make it unbounded.
        _ = retries => complete.await,
    }
}

async fn client_exchange(connection: &Connection) -> Result<Report> {
    let digest = digest(connection, CONTEXT)?;
    let (mut send, mut recv) = connection.open_bi().await?;
    write_fin(&mut send, CLIENT_STREAM, &digest).await?;
    read_fin(&mut recv, SERVER_STREAM, &digest).await?;
    datagrams(connection, CLIENT_DATAGRAM, SERVER_DATAGRAM, &digest).await?;
    let complete = async {
        let (mut send, mut recv) = connection.open_bi().await?;
        write_fin(&mut send, CLIENT_DONE, &digest).await?;
        read_fin(&mut recv, SERVER_DONE, &digest).await?;
        report(connection, digest)
    };
    finish_with_retries(
        complete,
        repeat_datagram(connection, CLIENT_DATAGRAM, &digest),
    )
    .await
}

async fn server_exchange(connection: &Connection) -> Result<Report> {
    let digest = digest(connection, CONTEXT)?;
    let (mut send, mut recv) = connection.accept_bi().await?;
    read_fin(&mut recv, CLIENT_STREAM, &digest).await?;
    write_fin(&mut send, SERVER_STREAM, &digest).await?;
    datagrams(connection, SERVER_DATAGRAM, CLIENT_DATAGRAM, &digest).await?;
    let complete = async {
        let (mut send, mut recv) = connection.accept_bi().await?;
        read_fin(&mut recv, CLIENT_DONE, &digest).await?;
        write_fin(&mut send, SERVER_DONE, &digest).await?;
        // The close reason is the client's explicit acknowledgement that our final bytes
        // AND FIN were consumed and checked. No side closes on merely queued stream data.
        match connection.closed().await {
            quinn::ConnectionError::ApplicationClosed(close)
                if close.error_code == 0u32.into() && close.reason.as_ref() == VERIFIED_CLOSE =>
            {
                report(connection, digest)
            }
            other => Err(format!("client did not acknowledge final validation: {other}").into()),
        }
    };
    finish_with_retries(
        complete,
        repeat_datagram(connection, SERVER_DATAGRAM, &digest),
    )
    .await
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{
        cell::Cell,
        future::{poll_fn, ready},
        task::Poll,
    };

    #[tokio::test]
    async fn completion_is_resolved_when_retry_failure_makes_it_ready_during_select() {
        // Deterministic scheduler regression, not a simulated TLS handshake. Whichever
        // branch select polls first, retries makes completion ready only during its poll.
        // This also catches a biased-select-only fix: completion may have polled Pending.
        let closed = Cell::new(false);
        let complete = poll_fn(|_| {
            if closed.get() {
                Poll::Ready(Ok(7))
            } else {
                Poll::Pending
            }
        });
        let retries = async {
            closed.set(true);
            Err("auxiliary send observed connection close".into())
        };
        assert_eq!(finish_with_retries(complete, retries).await.unwrap(), 7);
    }

    #[tokio::test]
    async fn completion_wins_when_both_branches_are_ready() {
        assert_eq!(
            finish_with_retries(ready(Ok(7)), ready(Err("auxiliary closed".into())))
                .await
                .unwrap(),
            7
        );
    }

    #[tokio::test]
    async fn invalid_completion_still_fails_after_auxiliary_failure() {
        let closed = Cell::new(false);
        let complete = poll_fn(|_| {
            if closed.get() {
                Poll::Ready(Err::<(), _>("invalid final close".into()))
            } else {
                Poll::Pending
            }
        });
        let retries = async {
            closed.set(true);
            Err("auxiliary send observed connection close".into())
        };
        assert_eq!(
            finish_with_retries(complete, retries)
                .await
                .unwrap_err()
                .to_string(),
            "invalid final close"
        );
    }

    #[tokio::test]
    async fn unresolved_completion_remains_bounded_by_outer_deadline() {
        let result = timeout(
            Duration::from_millis(30),
            finish_with_retries(
                std::future::pending::<Result<()>>(),
                ready(Err("retry budget exhausted".into())),
            ),
        )
        .await;
        assert!(
            result.is_err(),
            "the still-pending authoritative completion must hit the outer deadline"
        );
    }

    #[tokio::test]
    async fn real_quic_rejects_wrong_final_close_code_or_reason_after_valid_exchange() {
        for (code, reason) in [
            (1u32, VERIFIED_CLOSE),
            (0u32, b"unvalidated-close".as_slice()),
        ] {
            let bind: SocketAddr = "127.0.0.1:0".parse().unwrap();
            let server = start_server_sessions(bind, 1, Duration::from_secs(2))
                .await
                .unwrap();
            let (crypto, _) = tls::client_config(&server.spki).unwrap();
            let mut endpoint = Endpoint::client(bind).unwrap();
            endpoint.set_default_client_config(quinn::ClientConfig::new(Arc::new(
                QuicClientConfig::try_from(crypto).unwrap(),
            )));
            let connection = endpoint
                .connect(server.address, "probe.invalid")
                .unwrap()
                .await
                .unwrap();
            let report = timeout(Duration::from_secs(2), client_exchange(&connection))
                .await
                .unwrap()
                .unwrap();
            assert!(report.stream_verified && report.datagram_verified);
            connection.close(code.into(), reason);
            let error = server.handle.await.unwrap().unwrap_err().to_string();
            assert!(
                error.contains("client did not acknowledge final validation"),
                "{error}"
            );
            endpoint.close(1u32.into(), b"test-ended");
        }
    }
}
