//! Blocking, bounded C entry point for disposable platform probe runners.
use crate::{Result, run_client, tls, validate_address};
use std::{net::SocketAddr, panic::catch_unwind, slice, str};

pub const RESULT_CAPACITY: usize = 1024;

unsafe fn input<'a>(pointer: *const u8, length: usize, maximum: usize) -> Result<&'a str> {
    if pointer.is_null() || length == 0 || length > maximum {
        return Err("invalid input pointer or length".into());
    }
    // SAFETY: caller guarantees readable memory for the bounded input range.
    Ok(str::from_utf8(unsafe {
        slice::from_raw_parts(pointer, length)
    })?)
}

fn address(value: &str, peer: bool) -> Result<SocketAddr> {
    let address = value.parse()?;
    validate_address(address, peer)?;
    Ok(address)
}

/// Runs one real pinned QUIC probe synchronously, with the fixed 20-second network deadline.
///
/// # Safety
/// Non-null input pointers must address readable memory for their respective lengths and
/// remain valid and immutable throughout the call. Output must address writable memory for
/// `output_capacity` bytes and must not overlap any input. Invalid/dangling pointers cannot
/// be detected. No borrowed input escapes this call. Call from a worker, never a JS/UI thread.
/// A non-null, nonempty output is always NUL terminated; success is never truncated.
/// Rust unwinding is caught; callers must not throw foreign exceptions through this function.
/// Status: 0 verified, 1 invalid arguments, 2 invalid/insufficient output, 3 probe/runtime
/// failure, 4 caught Rust panic. Allocation aborts and invalid caller memory are not recoverable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nearby_quic_probe_client(
    bind: *const u8,
    bind_length: usize,
    peer: *const u8,
    peer_length: usize,
    pin_hex: *const u8,
    pin_length: usize,
    output: *mut u8,
    output_capacity: usize,
) -> i32 {
    if output.is_null() || output_capacity == 0 || output_capacity > isize::MAX as usize {
        return 2;
    }
    // SAFETY: caller guarantees writable output, disjoint from inputs.
    unsafe { output.write(0) };
    if output_capacity < RESULT_CAPACITY {
        return 2;
    }
    let result = catch_unwind(|| -> std::result::Result<String, (i32, String)> {
        let parse = || -> Result<(SocketAddr, SocketAddr, Vec<u8>)> {
            // SAFETY: bounded ranges checked by input; caller owns their validity.
            let bind = address(unsafe { input(bind, bind_length, 128)? }, false)?;
            let peer = address(unsafe { input(peer, peer_length, 128)? }, true)?;
            if bind.is_ipv4() != peer.is_ipv4() {
                return Err("bind and peer address families differ".into());
            }
            let pin = unsafe { input(pin_hex, pin_length, tls::PIN_LENGTH * 2)? };
            if pin.len() != tls::PIN_LENGTH * 2 || !pin.bytes().all(|b| b.is_ascii_hexdigit()) {
                return Err("pin must contain exactly 182 hexadecimal digits".into());
            }
            let pin = (0..pin.len())
                .step_by(2)
                .map(|i| u8::from_str_radix(&pin[i..i + 2], 16))
                .collect::<std::result::Result<Vec<_>, _>>()?;
            tls::validate_pin(&pin)?;
            Ok((bind, peer, pin))
        };
        // All input/capacity validation precedes runtime creation and socket binding.
        let (bind, peer, pin) = parse().map_err(|e| (1, e.to_string()))?;
        let runtime = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .map_err(|e| (3, e.to_string()))?;
        let report = runtime
            .block_on(run_client(bind, peer, &pin))
            .map_err(|e| (3, e.to_string()))?;
        if !report.stream_verified
            || !report.datagram_verified
            || report.pin_checks != 1
            || report.exporter_digest == report.alternate_context_digest
        {
            return Err((3, "report verification failed".into()));
        }
        let digest: String = report
            .exporter_digest
            .iter()
            .map(|b| format!("{b:02x}"))
            .collect();
        Ok(format!(
            "VERIFIED role=client stream=true datagram=true pin_checks=1 exporter_sha256={digest}"
        ))
    });
    let (status, message) = match result {
        Ok(Ok(message)) => (0, message),
        Ok(Err((status, error))) => (status, format!("ERROR {error}")),
        Err(_) => (4, "ERROR Rust panic caught".into()),
    };
    if message.len() >= output_capacity || message.contains('\0') {
        return 2;
    }
    // SAFETY: capacity checked above and output does not overlap internal message.
    unsafe {
        std::ptr::copy_nonoverlapping(message.as_ptr(), output, message.len());
        output.add(message.len()).write(0);
    }
    status
}
