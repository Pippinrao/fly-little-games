use nearby_quic_spike::{Report, Result, run_client, start_server, tls, validate_address};
use std::{
    io::{self, Write},
    net::SocketAddr,
};

enum Command {
    Server(SocketAddr),
    Client {
        bind: SocketAddr,
        peer: SocketAddr,
        pin: Vec<u8>,
    },
}

fn address(value: &str, peer: bool) -> Result<SocketAddr> {
    let address = value.parse::<SocketAddr>()?;
    validate_address(address, peer)?;
    Ok(address)
}

fn parse(args: &[String]) -> Result<Command> {
    match args {
        [mode, bind] if mode == "server" => Ok(Command::Server(address(bind, false)?)),
        [mode, bind, peer, pin] if mode == "client" => {
            let bind = address(bind, false)?;
            let peer = address(peer, true)?;
            if bind.is_ipv4() != peer.is_ipv4() { return Err("bind and peer address families differ".into()); }
            if pin.len() != tls::PIN_LENGTH * 2 || !pin.bytes().all(|byte| byte.is_ascii_hexdigit()) {
                return Err("spki-hex must contain exactly 182 hexadecimal digits".into());
            }
            let pin = (0..pin.len()).step_by(2)
                .map(|i| u8::from_str_radix(&pin[i..i + 2], 16))
                .collect::<std::result::Result<Vec<_>, _>>()?;
            tls::validate_pin(&pin)?;
            Ok(Command::Client { bind, peer, pin })
        }
        _ => Err("usage: nearby-quic-spike server <bind-IP:port> | client <bind-IP:port> <peer-IP:port> <full-DER-SPKI-hex>".into()),
    }
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

fn print_report(role: &str, report: &Report) {
    println!(
        "VERIFIED role={role} stream={} datagram={} pin_checks={} exporter_sha256={}",
        report.stream_verified,
        report.datagram_verified,
        report.pin_checks,
        hex(&report.exporter_digest)
    );
}

async fn run(command: Command) -> Result<()> {
    match command {
        Command::Server(bind) => {
            let server = start_server(bind).await?;
            println!(
                "READY address={} spki={}",
                server.address,
                hex(&server.spki)
            );
            io::stdout().flush()?;
            for report in server.handle.await?? {
                print_report("server", &report);
            }
        }
        Command::Client { bind, peer, pin } => {
            print_report("client", &run_client(bind, peer, &pin).await?)
        }
    }
    Ok(())
}

fn main() {
    // Reject all invalid arguments before creating a runtime, credential or socket.
    let result = (|| {
        let command = parse(&std::env::args().skip(1).collect::<Vec<_>>())?;
        tokio::runtime::Builder::new_multi_thread()
            .enable_all()
            .build()?
            .block_on(run(command))
    })();
    if let Err(error) = result {
        eprintln!("ERROR {error}");
        std::process::exit(1);
    }
}
