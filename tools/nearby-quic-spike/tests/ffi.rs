use nearby_quic_spike::{ffi::nearby_quic_probe_client, start_server, tls};
use std::{ffi::CStr, ptr};

fn invoke(bind: &[u8], peer: &[u8], pin: &[u8], output: &mut [u8]) -> i32 {
    unsafe {
        nearby_quic_probe_client(
            bind.as_ptr(),
            bind.len(),
            peer.as_ptr(),
            peer.len(),
            pin.as_ptr(),
            pin.len(),
            output.as_mut_ptr(),
            output.len(),
        )
    }
}

#[test]
fn rejects_invalid_inputs_without_starting_network() {
    let (_, pin) = tls::server_config().unwrap();
    let pin: String = pin.iter().map(|b| format!("{b:02x}")).collect();
    for (bind, peer, pin) in [
        (&b"\xff"[..], &b"127.0.0.1:7"[..], pin.as_bytes()),
        (&b"hostname:0"[..], &b"127.0.0.1:7"[..], pin.as_bytes()),
        (&b"127.0.0.1:0"[..], &b"8.8.8.8:7"[..], pin.as_bytes()),
        (&b"127.0.0.1:0"[..], &b"[::1]:7"[..], pin.as_bytes()),
        (&b"127.0.0.1:0"[..], &b"127.0.0.1:0"[..], pin.as_bytes()),
        (&b"127.0.0.1:0"[..], &b"127.0.0.1:7"[..], &b"00"[..]),
        (&b"127.0.0.1:0"[..], &b"127.0.0.1:7"[..], &[b'g'; 182][..]),
    ] {
        let mut output = [0xff; 1024];
        assert_eq!(invoke(bind, peer, pin, &mut output), 1);
        assert!(
            CStr::from_bytes_until_nul(&output)
                .unwrap()
                .to_str()
                .unwrap()
                .starts_with("ERROR")
        );
    }
    let mut output = [0xff; 1024];
    assert_eq!(
        unsafe {
            nearby_quic_probe_client(
                ptr::null(),
                1,
                ptr::null(),
                0,
                ptr::null(),
                0,
                output.as_mut_ptr(),
                output.len(),
            )
        },
        1
    );
    assert_eq!(
        unsafe {
            nearby_quic_probe_client(
                ptr::null(),
                usize::MAX,
                ptr::null(),
                0,
                ptr::null(),
                0,
                ptr::null_mut(),
                0,
            )
        },
        2
    );
    for capacity in [0, 1, 100, 1023] {
        let mut output = [0xff; 1024];
        assert_eq!(
            invoke(
                b"127.0.0.1:0",
                b"127.0.0.1:7",
                pin.as_bytes(),
                &mut output[..capacity]
            ),
            2
        );
        if capacity > 0 {
            assert_eq!(output[0], 0);
        }
    }
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn ffi_verifies_real_udp_and_rejects_wrong_pin() {
    for wrong in [false, true] {
        let server = start_server("127.0.0.1:0".parse().unwrap()).await.unwrap();
        let pin = if wrong {
            tls::server_config().unwrap().1
        } else {
            server.spki.clone()
        };
        let pin: String = pin.iter().map(|b| format!("{b:02x}")).collect();
        let peer = server.address.to_string();
        let (status, output) = tokio::task::spawn_blocking(move || {
            let mut output = [0; 1024];
            let status = invoke(b"127.0.0.1:0", peer.as_bytes(), pin.as_bytes(), &mut output);
            (
                status,
                CStr::from_bytes_until_nul(&output)
                    .unwrap()
                    .to_str()
                    .unwrap()
                    .to_owned(),
            )
        })
        .await
        .unwrap();
        if wrong {
            assert_eq!(status, 3);
            assert!(output.contains("pin mismatch"), "{output}");
            assert!(!output.contains("VERIFIED"));
            assert!(server.handle.await.unwrap().is_err());
        } else {
            assert_eq!(status, 0, "{output}");
            let report = server.handle.await.unwrap().unwrap().remove(0);
            let digest: String = report
                .exporter_digest
                .iter()
                .map(|b| format!("{b:02x}"))
                .collect();
            assert!(
                output.starts_with("VERIFIED role=client stream=true datagram=true pin_checks=1")
            );
            assert!(output.ends_with(&digest));
        }
    }
}
