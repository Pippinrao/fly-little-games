use std::{net::SocketAddr, time::Duration};

pub mod ffi;
mod probe;
pub mod tls;
pub use probe::{run_client, run_client_deadline, start_server, start_server_sessions};
pub type Result<T> = std::result::Result<T, Box<dyn std::error::Error + Send + Sync>>;
pub const DEADLINE: Duration = Duration::from_secs(20);

#[derive(Debug)]
pub struct Report {
    pub stream_verified: bool,
    pub datagram_verified: bool,
    pub pin_checks: usize,
    pub exporter_digest: [u8; 32],
    pub alternate_context_digest: [u8; 32],
}

pub struct Server {
    pub address: SocketAddr,
    pub spki: Vec<u8>,
    pub handle: tokio::task::JoinHandle<Result<Vec<Report>>>,
}

pub fn validate_address(address: SocketAddr, peer: bool) -> Result<()> {
    let local = match address {
        SocketAddr::V4(v4) => {
            v4.ip().is_loopback() || v4.ip().is_private() || v4.ip().is_link_local()
        }
        // This disposable spike rejects IPv6 link-local/scoped endpoints explicitly:
        // the platform interface-index mapping has not been certified.
        SocketAddr::V6(v6) => {
            v6.scope_id() == 0
                && v6.flowinfo() == 0
                && (v6.ip().is_loopback() || v6.ip().is_unique_local())
        }
    };
    if !local || (peer && address.port() == 0) {
        return Err("address must be numeric loopback/private/IPv4-link-local; peer port nonzero; scoped IPv6 unsupported".into());
    }
    Ok(())
}
