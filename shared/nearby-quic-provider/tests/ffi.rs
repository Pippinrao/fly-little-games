use flynes_nearby_quic_provider::ffi::*;
use rcgen::{CertificateParams, KeyPair, PKCS_ECDSA_P256_SHA256, PublicKeyData};
use std::{
    ffi::c_void,
    sync::{
        Mutex, OnceLock,
        atomic::{AtomicUsize, Ordering},
    },
    time::{Duration, Instant},
};

fn pin_hash(spki: &[u8]) -> Vec<u8> {
    ring::digest::digest(&ring::digest::SHA256, spki)
        .as_ref()
        .to_vec()
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct Event {
    operation: u64,
    result: i32,
    resource: u64,
    bytes: Vec<u8>,
}
static EVENTS: OnceLock<Mutex<Vec<Event>>> = OnceLock::new();
static TEST_SERIAL: Mutex<()> = Mutex::new(());

unsafe extern "C" fn retain(_: *mut core::ffi::c_void) {}
unsafe extern "C" fn release(_: *mut core::ffi::c_void) {}
unsafe extern "C" fn completion(
    _: *mut core::ffi::c_void,
    operation: u64,
    result: i32,
    _: u32,
    resource: u64,
    bytes: *const u8,
    size: usize,
) {
    let bytes = if size == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(bytes, size) }.to_vec()
    };
    EVENTS
        .get_or_init(Default::default)
        .lock()
        .unwrap()
        .push(Event {
            operation,
            result,
            resource,
            bytes,
        });
}

fn wait(operation: u64) -> Event {
    let deadline = Instant::now() + Duration::from_secs(3);
    loop {
        {
            let mut events = EVENTS.get_or_init(Default::default).lock().unwrap();
            if let Some(index) = events.iter().position(|value| value.operation == operation) {
                return events.remove(index);
            }
        }
        assert!(Instant::now() < deadline, "operation {operation} timed out");
        std::thread::yield_now();
    }
}

struct SignerContext {
    key: KeyPair,
    retains: AtomicUsize,
    releases: AtomicUsize,
}
unsafe extern "C" fn signer_retain(context: *mut c_void) {
    unsafe { &*(context as *const SignerContext) }
        .retains
        .fetch_add(1, Ordering::Relaxed);
}
unsafe extern "C" fn signer_release(context: *mut c_void) {
    unsafe { &*(context as *const SignerContext) }
        .releases
        .fetch_add(1, Ordering::Relaxed);
}
unsafe extern "C" fn signer_sign(
    context: *mut c_void,
    message: *const u8,
    message_size: usize,
    output: *mut u8,
    capacity: usize,
) -> usize {
    let context = unsafe { &*(context as *const SignerContext) };
    let message = unsafe { std::slice::from_raw_parts(message, message_size) };
    let Ok(signature) = rcgen::SigningKey::sign(&context.key, message) else {
        return 0;
    };
    if signature.len() > capacity {
        return 0;
    }
    unsafe { std::ptr::copy_nonoverlapping(signature.as_ptr(), output, signature.len()) };
    signature.len()
}

fn provider() -> *mut FlynesQuicProvider {
    let callbacks = FlynesQuicCallbacks {
        struct_size: core::mem::size_of::<FlynesQuicCallbacks>() as u32,
        abi_version: FLYNES_QUIC_PROVIDER_ABI_V1,
        context: core::ptr::null_mut(),
        retain: Some(retain),
        release: Some(release),
        completion: Some(completion),
    };
    unsafe { flynes_quic_provider_create(&callbacks) }
}

#[test]
fn lifecycle_and_invalid_operations_are_nonblocking_and_terminal_once() {
    let _serial = TEST_SERIAL.lock().unwrap();
    EVENTS.get_or_init(Default::default).lock().unwrap().clear();
    let callbacks = FlynesQuicCallbacks {
        struct_size: core::mem::size_of::<FlynesQuicCallbacks>() as u32,
        abi_version: FLYNES_QUIC_PROVIDER_ABI_V1,
        context: core::ptr::null_mut(),
        retain: Some(retain),
        release: Some(release),
        completion: Some(completion),
    };
    let provider = unsafe { flynes_quic_provider_create(&callbacks) };
    assert!(!provider.is_null());
    unsafe { flynes_quic_provider_retain(provider) };
    let began = Instant::now();
    assert_eq!(
        unsafe { flynes_quic_provider_close(provider, 7, 999, 0) },
        FLYNES_QUIC_ACCEPTED
    );
    assert!(began.elapsed() < Duration::from_millis(20));
    let deadline = Instant::now() + Duration::from_secs(1);
    while EVENTS
        .get_or_init(Default::default)
        .lock()
        .unwrap()
        .is_empty()
        && Instant::now() < deadline
    {
        std::thread::yield_now();
    }
    assert_eq!(
        *EVENTS.get().unwrap().lock().unwrap(),
        vec![Event {
            operation: 7,
            result: FLYNES_QUIC_INVALID_HANDLE,
            resource: 0,
            bytes: Vec::new()
        }]
    );
    unsafe {
        flynes_quic_provider_release(provider);
        flynes_quic_provider_release(provider);
    }
}

#[test]
fn ffi_real_connection_exporter_stream_datagram_query_and_cleanup() {
    let _serial = TEST_SERIAL.lock().unwrap();
    EVENTS.get_or_init(Default::default).lock().unwrap().clear();
    let key = KeyPair::generate_for(&PKCS_ECDSA_P256_SHA256).unwrap();
    let spki = key.subject_public_key_info();
    let certificate = CertificateParams::new(vec!["flynes.invalid".into()])
        .unwrap()
        .self_signed(&key)
        .unwrap();
    let signer_context = Box::new(SignerContext {
        key,
        retains: AtomicUsize::new(0),
        releases: AtomicUsize::new(0),
    });
    let signer = flynes_nearby_quic_provider::tls::TlsSignerCallbacks {
        context: (&*signer_context as *const SignerContext).cast_mut().cast(),
        retain: Some(signer_retain),
        release: Some(signer_release),
        sign: Some(signer_sign),
    };
    let provider = provider();
    assert!(!provider.is_null());
    assert_eq!(
        unsafe {
            flynes_quic_provider_register_tls_material(
                provider,
                1,
                certificate.der().as_ptr(),
                certificate.der().len(),
                spki.as_ptr(),
                spki.len(),
                &signer,
            )
        },
        FLYNES_QUIC_ACCEPTED
    );
    let material = wait(1);
    assert_eq!(material.result, FLYNES_QUIC_OK);
    assert_eq!(
        unsafe {
            flynes_quic_provider_listen(
                provider,
                2,
                b"127.0.0.1:0".as_ptr(),
                11,
                material.resource,
                2000,
            )
        },
        FLYNES_QUIC_ACCEPTED
    );
    let listener = wait(2);
    let address = String::from_utf8(listener.bytes).unwrap();
    let expected_pin = pin_hash(&spki);
    assert_eq!(
        unsafe { flynes_quic_provider_accept(provider, 3, listener.resource) },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(
        unsafe {
            flynes_quic_provider_connect(
                provider,
                4,
                b"127.0.0.1:0".as_ptr(),
                11,
                address.as_ptr(),
                address.len(),
                expected_pin.as_ptr(),
                expected_pin.len(),
                2000,
            )
        },
        FLYNES_QUIC_ACCEPTED
    );
    let client = wait(4);
    let server = wait(3);
    assert_eq!(
        (client.result, server.result),
        (FLYNES_QUIC_OK, FLYNES_QUIC_OK)
    );
    for (operation, connection, connector) in
        [(19, client.resource, true), (20, server.resource, false)]
    {
        assert_eq!(
            unsafe { flynes_quic_provider_inspect_handshake(provider, operation, connection) },
            FLYNES_QUIC_ACCEPTED
        );
        let inspected = wait(operation);
        assert_eq!(inspected.result, FLYNES_QUIC_OK);
        assert_eq!(
            inspected.bytes.len(),
            core::mem::size_of::<FlynesQuicHandshakeFactsV1>()
        );
        let facts = unsafe {
            (inspected.bytes.as_ptr() as *const FlynesQuicHandshakeFactsV1).read_unaligned()
        };
        assert_eq!((facts.tls_major, facts.tls_minor), (1, 3));
        assert_eq!(facts.full_handshake, 1);
        assert_eq!(facts.pin_verifier_invoked, connector as u8);
        assert_eq!(facts.peer_certificate_verified, connector as u8);
        assert_eq!((facts.resumed, facts.zero_rtt), (0, 0));
        assert_eq!(&facts.alpn[..facts.alpn_size as usize], b"flynes-nearby/2");
        if connector {
            assert_eq!(facts.peer_der_spki_hash.as_slice(), expected_pin.as_slice());
        } else {
            assert_eq!(facts.peer_der_spki_hash, [0; 32]);
        }
    }

    for (operation, connection) in [(5, client.resource), (6, server.resource)] {
        assert_eq!(
            unsafe {
                flynes_quic_provider_exporter(
                    provider,
                    operation,
                    connection,
                    b"ffi-link".as_ptr(),
                    8,
                )
            },
            FLYNES_QUIC_ACCEPTED
        );
    }
    assert_eq!(wait(5).bytes, wait(6).bytes);

    assert_eq!(
        unsafe { flynes_quic_provider_accept_bidi(provider, 7, server.resource) },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(
        unsafe { flynes_quic_provider_open_bidi(provider, 8, client.resource) },
        FLYNES_QUIC_ACCEPTED
    );
    let opened = wait(8);
    let client_send = opened.resource;
    let client_recv = u64::from_be_bytes(opened.bytes[..8].try_into().unwrap());
    assert_eq!(
        unsafe { flynes_quic_provider_write(provider, 9, client_send, b"hello".as_ptr(), 5, 0) },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(wait(9).result, FLYNES_QUIC_OK);
    assert_eq!(
        unsafe { flynes_quic_provider_finish(provider, 21, client_send) },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(wait(21).result, FLYNES_QUIC_OK);
    let accepted = wait(7);
    assert_eq!(accepted.result, FLYNES_QUIC_OK);
    let server_recv = u64::from_be_bytes(accepted.bytes[..8].try_into().unwrap());
    assert_eq!(
        unsafe { flynes_quic_provider_grant_read_credit(provider, 10, server_recv, 64) },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(wait(10).bytes, b"hello");

    assert_eq!(
        unsafe { flynes_quic_provider_read_datagram(provider, 11, server.resource) },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(
        unsafe {
            flynes_quic_provider_send_datagram(provider, 12, client.resource, b"ping".as_ptr(), 4)
        },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(wait(12).result, FLYNES_QUIC_OK);
    assert_eq!(wait(11).bytes, b"ping");
    assert_eq!(
        unsafe { flynes_quic_provider_query(provider, 13, client.resource) },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(wait(13).bytes.len(), 32);
    assert_eq!(
        unsafe { flynes_quic_provider_payload_budget(provider, 22, client.resource) },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(wait(22).bytes.len(), 8);
    assert_eq!(
        unsafe { flynes_quic_provider_stats(provider, 23, client.resource) },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(wait(23).bytes.len(), 24);

    assert_eq!(
        unsafe { flynes_quic_provider_open_uni(provider, 24, client.resource) },
        FLYNES_QUIC_ACCEPTED
    );
    let reset_stream = wait(24).resource;
    assert_eq!(
        unsafe { flynes_quic_provider_reset(provider, 25, reset_stream, 7) },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(wait(25).result, FLYNES_QUIC_OK);

    assert_eq!(
        unsafe { flynes_quic_provider_close(provider, 14, client.resource, 0) },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(wait(14).result, FLYNES_QUIC_OK);
    for (operation, stream) in [(26, client_send), (27, client_recv)] {
        let status = if operation == 26 {
            unsafe { flynes_quic_provider_write(provider, operation, stream, b"x".as_ptr(), 1, 0) }
        } else {
            unsafe { flynes_quic_provider_read(provider, operation, stream, 1) }
        };
        assert_eq!(status, FLYNES_QUIC_ACCEPTED);
        assert_eq!(wait(operation).result, FLYNES_QUIC_INVALID_HANDLE);
    }

    for (operation, resource) in [
        (15, server.resource),
        (16, listener.resource),
        (17, material.resource),
    ] {
        assert_eq!(
            unsafe { flynes_quic_provider_close(provider, operation, resource, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(wait(operation).result, FLYNES_QUIC_OK);
    }
    unsafe { flynes_quic_provider_release(provider) };
    let deadline = Instant::now() + Duration::from_secs(1);
    while signer_context.releases.load(Ordering::Relaxed) == 0 && Instant::now() < deadline {
        std::thread::yield_now();
    }
    assert_eq!(signer_context.retains.load(Ordering::Relaxed), 1);
    assert_eq!(signer_context.releases.load(Ordering::Relaxed), 1);
}

#[test]
fn cancellation_aborts_one_pending_operation_and_duplicate_is_rejected() {
    let _serial = TEST_SERIAL.lock().unwrap();
    EVENTS.get_or_init(Default::default).lock().unwrap().clear();
    let key = KeyPair::generate_for(&PKCS_ECDSA_P256_SHA256).unwrap();
    let spki = key.subject_public_key_info();
    let silent = std::net::UdpSocket::bind("127.0.0.1:0").unwrap();
    let peer = silent.local_addr().unwrap().to_string();
    let expected_pin = pin_hash(&spki);
    let provider = provider();
    assert_eq!(
        unsafe {
            flynes_quic_provider_connect(
                provider,
                20,
                b"127.0.0.1:0".as_ptr(),
                11,
                peer.as_ptr(),
                peer.len(),
                expected_pin.as_ptr(),
                expected_pin.len(),
                5000,
            )
        },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(
        unsafe {
            flynes_quic_provider_connect(
                provider,
                20,
                b"127.0.0.1:0".as_ptr(),
                11,
                peer.as_ptr(),
                peer.len(),
                expected_pin.as_ptr(),
                expected_pin.len(),
                5000,
            )
        },
        FLYNES_QUIC_DUPLICATE
    );
    assert_eq!(
        unsafe { flynes_quic_provider_cancel(provider, 20) },
        FLYNES_QUIC_ACCEPTED
    );
    assert_eq!(wait(20).result, FLYNES_QUIC_CANCELLED);
    std::thread::sleep(Duration::from_millis(50));
    assert!(
        EVENTS.get().unwrap().lock().unwrap().is_empty(),
        "cancelled operation has exactly one terminal callback"
    );
    unsafe { flynes_quic_provider_release(provider) };
}
