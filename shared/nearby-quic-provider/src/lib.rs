use std::{net::SocketAddr, sync::Arc, time::Duration};

pub mod ffi;
pub mod runtime;
pub mod tls;

pub type Result<T> = std::result::Result<T, Box<dyn std::error::Error + Send + Sync>>;

pub const PRODUCT_ALPN: &[u8] = b"flynes-nearby/2";
pub const EXPORTER_LABEL: &[u8] = b"EXPORTER-flynes-nearby-v1";

pub fn validate_address(address: SocketAddr, peer: bool) -> Result<()> {
    let local = match address {
        SocketAddr::V4(value) => {
            value.ip().is_loopback() || value.ip().is_private() || value.ip().is_link_local()
        }
        SocketAddr::V6(value) => {
            value.scope_id() == 0
                && value.flowinfo() == 0
                && (value.ip().is_loopback() || value.ip().is_unique_local())
        }
    };
    if !local || (peer && address.port() == 0) {
        return Err(
            "address must be numeric loopback/private/link-local and peer port nonzero".into(),
        );
    }
    Ok(())
}

pub fn transport_config(deadline: Duration) -> Result<Arc<quinn::TransportConfig>> {
    if deadline.is_zero() || deadline > Duration::from_secs(60) {
        return Err("deadline must be nonzero and at most 60 seconds".into());
    }
    let mut config = quinn::TransportConfig::default();
    config.max_idle_timeout(Some(deadline.min(Duration::from_secs(5)).try_into()?));
    config.max_concurrent_bidi_streams(4u32.into());
    config.max_concurrent_uni_streams(4u32.into());
    config.stream_receive_window(quinn::VarInt::from_u32(256 * 1024));
    config.receive_window(quinn::VarInt::from_u32(1024 * 1024));
    config.send_window(1024 * 1024);
    config.datagram_receive_buffer_size(Some(256 * 1024));
    config.datagram_send_buffer_size(256 * 1024);
    config.allow_spin(false);
    Ok(Arc::new(config))
}
