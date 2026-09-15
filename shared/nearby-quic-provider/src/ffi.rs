//! Non-blocking C boundary. Every pointer supplied by a caller must remain valid for the
//! duration of that entry-point call; inputs are copied before return. Provider handles must
//! be live and balanced with retain/release. Callback contexts are retained at create and
//! released after all asynchronous callbacks have stopped.
#![allow(clippy::missing_safety_doc)]

use crate::{runtime, tls};
use bytes::Bytes;
use rustls::{pki_types::CertificateDer, sign::SigningKey};
use std::{
    collections::HashMap,
    ffi::c_void,
    future::Future,
    slice, str,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering},
    },
    time::Duration,
};
use tokio::{
    runtime::Runtime,
    sync::{Mutex as AsyncMutex, oneshot},
};

pub const FLYNES_QUIC_PROVIDER_ABI_V1: u32 = 1;
pub const FLYNES_QUIC_ACCEPTED: i32 = 1;
pub const FLYNES_QUIC_OK: i32 = 0;
pub const FLYNES_QUIC_INVALID_ARGUMENT: i32 = -1;
pub const FLYNES_QUIC_INVALID_HANDLE: i32 = -2;
pub const FLYNES_QUIC_CLOSED: i32 = -3;
pub const FLYNES_QUIC_FAILED: i32 = -4;
pub const FLYNES_QUIC_CANCELLED: i32 = -5;
pub const FLYNES_QUIC_DUPLICATE: i32 = -6;
const MAX_INPUT: usize = 1024 * 1024;

pub type FlynesContextRetain = unsafe extern "C" fn(*mut c_void);
pub type FlynesContextRelease = unsafe extern "C" fn(*mut c_void);
pub type FlynesQuicCompletion = unsafe extern "C" fn(
    context: *mut c_void,
    operation: u64,
    result: i32,
    terminal: u32,
    resource: u64,
    bytes: *const u8,
    bytes_size: usize,
);

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FlynesQuicCallbacks {
    pub struct_size: u32,
    pub abi_version: u32,
    pub context: *mut c_void,
    pub retain: Option<FlynesContextRetain>,
    pub release: Option<FlynesContextRelease>,
    pub completion: Option<FlynesQuicCompletion>,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FlynesQuicHandshakeFactsV1 {
    pub struct_size: u32,
    pub abi_version: u32,
    pub tls_major: u16,
    pub tls_minor: u16,
    pub full_handshake: u8,
    pub pin_verifier_invoked: u8,
    pub peer_certificate_verified: u8,
    pub resumed: u8,
    pub zero_rtt: u8,
    pub reserved_zero: [u8; 3],
    pub alpn_size: u32,
    pub alpn: [u8; 32],
    pub peer_der_spki_hash: [u8; 32],
}

fn handshake_facts_bytes(value: runtime::HandshakeFacts) -> Option<Vec<u8>> {
    let peer_der_spki_hash = if value.peer_spki_hash.is_empty() {
        [0; 32]
    } else {
        value.peer_spki_hash.try_into().ok()?
    };
    let mut facts = FlynesQuicHandshakeFactsV1 {
        struct_size: size_of::<FlynesQuicHandshakeFactsV1>() as u32,
        abi_version: FLYNES_QUIC_PROVIDER_ABI_V1,
        tls_major: value.tls_major,
        tls_minor: value.tls_minor,
        full_handshake: value.full_handshake.into(),
        pin_verifier_invoked: value.pin_verifier_invoked.into(),
        peer_certificate_verified: value.peer_certificate_verified.into(),
        resumed: value.resumed.into(),
        zero_rtt: value.zero_rtt.into(),
        reserved_zero: [0; 3],
        alpn_size: value.alpn.len().try_into().ok()?,
        alpn: [0; 32],
        peer_der_spki_hash,
    };
    if value.alpn.len() >= facts.alpn.len() {
        return None;
    }
    facts.alpn[..value.alpn.len()].copy_from_slice(&value.alpn);
    // SAFETY: facts is a fully initialized repr(C) byte-only value and the copy
    // ends before this stack value is dropped.
    Some(unsafe {
        slice::from_raw_parts(
            (&facts as *const FlynesQuicHandshakeFactsV1).cast::<u8>(),
            size_of::<FlynesQuicHandshakeFactsV1>(),
        )
        .to_vec()
    })
}

struct CallbackTarget(FlynesQuicCallbacks);
unsafe impl Send for CallbackTarget {}
unsafe impl Sync for CallbackTarget {}

impl CallbackTarget {
    fn complete(&self, operation: u64, result: i32, resource: u64, bytes: &[u8]) {
        if let Some(callback) = self.0.completion {
            // SAFETY: the retained context and bytes remain valid for this callback.
            unsafe {
                callback(
                    self.0.context,
                    operation,
                    result,
                    1,
                    resource,
                    bytes.as_ptr(),
                    bytes.len(),
                )
            };
        }
    }
}

impl Drop for CallbackTarget {
    fn drop(&mut self) {
        if let Some(release) = self.0.release {
            // SAFETY: create retained exactly one callback context reference.
            unsafe { release(self.0.context) };
        }
    }
}

struct Material {
    certificates: Vec<CertificateDer<'static>>,
    signing_key: Arc<dyn SigningKey>,
    spki: Vec<u8>,
}

enum Resource {
    Material(Arc<Material>),
    Listener(Arc<runtime::ProductListener>),
    Connection(Arc<runtime::ProductConnection>),
    Send(Arc<AsyncMutex<quinn::SendStream>>),
    Recv(Arc<AsyncMutex<quinn::RecvStream>>),
}

struct PendingOperation {
    abort: tokio::task::AbortHandle,
    completed: Arc<AtomicBool>,
}

struct ProviderState {
    callbacks: Arc<CallbackTarget>,
    resources: Mutex<HashMap<u64, Resource>>,
    pending: Mutex<HashMap<u64, PendingOperation>>,
    next_resource: AtomicU64,
}

impl ProviderState {
    fn insert(&self, resource: Resource) -> u64 {
        let handle = self.next_resource.fetch_add(1, Ordering::Relaxed);
        self.resources
            .lock()
            .expect("resource mutex poisoned")
            .insert(handle, resource);
        handle
    }

    fn material(&self, handle: u64) -> Option<Arc<Material>> {
        match self.resources.lock().ok()?.get(&handle)? {
            Resource::Material(value) => Some(value.clone()),
            _ => None,
        }
    }
    fn listener(&self, handle: u64) -> Option<Arc<runtime::ProductListener>> {
        match self.resources.lock().ok()?.get(&handle)? {
            Resource::Listener(value) => Some(value.clone()),
            _ => None,
        }
    }
    fn connection(&self, handle: u64) -> Option<Arc<runtime::ProductConnection>> {
        match self.resources.lock().ok()?.get(&handle)? {
            Resource::Connection(value) => Some(value.clone()),
            _ => None,
        }
    }
    fn send(&self, handle: u64) -> Option<Arc<AsyncMutex<quinn::SendStream>>> {
        match self.resources.lock().ok()?.get(&handle)? {
            Resource::Send(value) => Some(value.clone()),
            _ => None,
        }
    }
    fn recv(&self, handle: u64) -> Option<Arc<AsyncMutex<quinn::RecvStream>>> {
        match self.resources.lock().ok()?.get(&handle)? {
            Resource::Recv(value) => Some(value.clone()),
            _ => None,
        }
    }
}

#[repr(C)]
pub struct FlynesQuicProvider {
    references: AtomicUsize,
    state: Arc<ProviderState>,
    // Drop after state so streams/endpoints release before Tokio joins its workers.
    runtime: Runtime,
}

type Completion = (i32, u64, Vec<u8>);

fn submit<F>(provider: &FlynesQuicProvider, operation: u64, future: F) -> i32
where
    F: Future<Output = Completion> + Send + 'static,
{
    if operation == 0 {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    }
    let completed = Arc::new(AtomicBool::new(false));
    let (start_sender, start_receiver) = oneshot::channel();
    let state = provider.state.clone();
    let task_completed = completed.clone();
    let handle = provider.runtime.spawn(async move {
        if start_receiver.await.is_err() {
            return;
        }
        let (result, resource, bytes) = future.await;
        if !task_completed.swap(true, Ordering::AcqRel) {
            state
                .callbacks
                .complete(operation, result, resource, &bytes);
        }
        if let Ok(mut pending) = state.pending.lock() {
            pending.remove(&operation);
        }
    });
    {
        let Ok(mut pending) = provider.state.pending.lock() else {
            handle.abort();
            return FLYNES_QUIC_CLOSED;
        };
        if pending.contains_key(&operation) {
            handle.abort();
            return FLYNES_QUIC_DUPLICATE;
        }
        pending.insert(
            operation,
            PendingOperation {
                abort: handle.abort_handle(),
                completed,
            },
        );
    }
    if start_sender.send(()).is_err() {
        return FLYNES_QUIC_CLOSED;
    }
    FLYNES_QUIC_ACCEPTED
}

unsafe fn copy_bytes(pointer: *const u8, size: usize, allow_empty: bool) -> Option<Vec<u8>> {
    if size > MAX_INPUT || (!allow_empty && size == 0) || (pointer.is_null() && size != 0) {
        return None;
    }
    if size == 0 {
        return Some(Vec::new());
    }
    // SAFETY: caller guarantees the bounded input is readable for this call.
    Some(unsafe { slice::from_raw_parts(pointer, size) }.to_vec())
}

unsafe fn copy_text(pointer: *const u8, size: usize) -> Option<String> {
    let bytes = unsafe { copy_bytes(pointer, size, false) }?;
    Some(str::from_utf8(&bytes).ok()?.to_owned())
}

fn provider_ref<'a>(provider: *mut FlynesQuicProvider) -> Option<&'a FlynesQuicProvider> {
    // SAFETY: every exported entry point requires a live provider handle.
    unsafe { provider.as_ref() }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_create(
    callbacks: *const FlynesQuicCallbacks,
) -> *mut FlynesQuicProvider {
    if callbacks.is_null() {
        return std::ptr::null_mut();
    }
    // SAFETY: the callbacks table is readable for this call.
    let callbacks = unsafe { *callbacks };
    if callbacks.struct_size as usize != size_of::<FlynesQuicCallbacks>()
        || callbacks.abi_version != FLYNES_QUIC_PROVIDER_ABI_V1
        || callbacks.retain.is_none()
        || callbacks.release.is_none()
        || callbacks.completion.is_none()
    {
        return std::ptr::null_mut();
    }
    if let Some(retain) = callbacks.retain {
        // SAFETY: a complete callback table promises a valid context.
        unsafe { retain(callbacks.context) };
    }
    let runtime = match tokio::runtime::Builder::new_multi_thread()
        .worker_threads(2)
        .enable_all()
        .build()
    {
        Ok(value) => value,
        Err(_) => {
            if let Some(release) = callbacks.release {
                // SAFETY: balances the retain above on construction failure.
                unsafe { release(callbacks.context) };
            }
            return std::ptr::null_mut();
        }
    };
    Box::into_raw(Box::new(FlynesQuicProvider {
        references: AtomicUsize::new(1),
        state: Arc::new(ProviderState {
            callbacks: Arc::new(CallbackTarget(callbacks)),
            resources: Mutex::new(HashMap::new()),
            pending: Mutex::new(HashMap::new()),
            next_resource: AtomicU64::new(1),
        }),
        runtime,
    }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_retain(provider: *mut FlynesQuicProvider) {
    if let Some(provider) = provider_ref(provider) {
        let _ = provider
            .references
            .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |value| {
                value.checked_add(1)
            });
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_release(provider: *mut FlynesQuicProvider) {
    let Some(provider_ref) = provider_ref(provider) else {
        return;
    };
    if provider_ref.references.fetch_sub(1, Ordering::AcqRel) == 1 {
        // SAFETY: the final counted owner uniquely destroys the allocation.
        let owned = unsafe { Box::from_raw(provider) };
        let FlynesQuicProvider {
            references: _,
            state,
            runtime,
        } = *owned;
        drop(state);
        runtime.shutdown_background();
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_register_tls_material(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    certificate: *const u8,
    certificate_size: usize,
    spki: *const u8,
    spki_size: usize,
    signer: *const tls::TlsSignerCallbacks,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let Some(certificate) = (unsafe { copy_bytes(certificate, certificate_size, false) }) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let Some(spki) = (unsafe { copy_bytes(spki, spki_size, false) }) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let Some(signer) = (unsafe { signer.as_ref() }).copied() else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let signing_key = match tls::OpaqueTlsSigningKey::new(signer, &spki) {
        Ok(value) => Arc::new(value) as Arc<dyn SigningKey>,
        Err(_) => return FLYNES_QUIC_INVALID_ARGUMENT,
    };
    if tls::server_config(
        vec![CertificateDer::from(certificate.clone())],
        signing_key.clone(),
        &spki,
    )
    .is_err()
    {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    }
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let handle = state.insert(Resource::Material(Arc::new(Material {
            certificates: vec![CertificateDer::from(certificate)],
            signing_key,
            spki,
        })));
        (FLYNES_QUIC_OK, handle, Vec::new())
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_listen(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    bind: *const u8,
    bind_size: usize,
    material: u64,
    deadline_ms: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let Some(bind) = (unsafe { copy_text(bind, bind_size) }) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(material) = state.material(material) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        let Ok(bind) = bind.parse() else {
            return (FLYNES_QUIC_INVALID_ARGUMENT, 0, Vec::new());
        };
        match runtime::listen(
            bind,
            material.certificates.clone(),
            material.signing_key.clone(),
            &material.spki,
            Duration::from_millis(deadline_ms),
        ) {
            Ok(listener) => match listener.local_addr() {
                Ok(address) => {
                    let handle = state.insert(Resource::Listener(Arc::new(listener)));
                    (FLYNES_QUIC_OK, handle, address.to_string().into_bytes())
                }
                Err(_) => (FLYNES_QUIC_FAILED, 0, Vec::new()),
            },
            Err(_) => (FLYNES_QUIC_FAILED, 0, Vec::new()),
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_accept(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    listener: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(listener) = state.listener(listener) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        match listener.accept().await {
            Ok(connection) => {
                let handle = state.insert(Resource::Connection(Arc::new(connection)));
                (FLYNES_QUIC_OK, handle, Vec::new())
            }
            Err(_) => (FLYNES_QUIC_FAILED, 0, Vec::new()),
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_connect(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    bind: *const u8,
    bind_size: usize,
    peer: *const u8,
    peer_size: usize,
    expected_spki_hash: *const u8,
    expected_spki_hash_size: usize,
    deadline_ms: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let (Some(bind), Some(peer), Some(expected_spki_hash)) = (
        unsafe { copy_text(bind, bind_size) },
        unsafe { copy_text(peer, peer_size) },
        unsafe { copy_bytes(expected_spki_hash, expected_spki_hash_size, false) },
    ) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let (Ok(bind), Ok(peer)) = (bind.parse(), peer.parse()) else {
            return (FLYNES_QUIC_INVALID_ARGUMENT, 0, Vec::new());
        };
        match runtime::connect(
            bind,
            peer,
            &expected_spki_hash,
            Duration::from_millis(deadline_ms),
        )
        .await
        {
            Ok(connection) => {
                let handle = state.insert(Resource::Connection(Arc::new(connection)));
                (FLYNES_QUIC_OK, handle, Vec::new())
            }
            Err(_) => (FLYNES_QUIC_FAILED, 0, Vec::new()),
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_inspect_handshake(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    connection: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(connection) = state.connection(connection) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        match connection
            .handshake_facts()
            .ok()
            .and_then(handshake_facts_bytes)
        {
            Some(bytes) => (FLYNES_QUIC_OK, connection.stable_id() as u64, bytes),
            None => (FLYNES_QUIC_FAILED, 0, Vec::new()),
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_exporter(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    connection: u64,
    context: *const u8,
    context_size: usize,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let Some(context) = (unsafe { copy_bytes(context, context_size, false) }) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(connection) = state.connection(connection) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        match connection.exporter(&context) {
            Ok(value) => (FLYNES_QUIC_OK, 0, value.to_vec()),
            Err(_) => (FLYNES_QUIC_FAILED, 0, Vec::new()),
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_open_bidi(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    connection: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(connection) = state.connection(connection) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        match connection.open_bidi().await {
            Ok((send, recv)) => {
                let send = state.insert(Resource::Send(Arc::new(AsyncMutex::new(send))));
                let recv = state.insert(Resource::Recv(Arc::new(AsyncMutex::new(recv))));
                (FLYNES_QUIC_OK, send, recv.to_be_bytes().to_vec())
            }
            Err(_) => (FLYNES_QUIC_FAILED, 0, Vec::new()),
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_accept_bidi(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    connection: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(connection) = state.connection(connection) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        match connection.accept_bidi().await {
            Ok((send, recv)) => {
                let send = state.insert(Resource::Send(Arc::new(AsyncMutex::new(send))));
                let recv = state.insert(Resource::Recv(Arc::new(AsyncMutex::new(recv))));
                (FLYNES_QUIC_OK, send, recv.to_be_bytes().to_vec())
            }
            Err(_) => (FLYNES_QUIC_FAILED, 0, Vec::new()),
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_open_uni(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    connection: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(connection) = state.connection(connection) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        match connection.open_uni().await {
            Ok(send) => {
                let handle = state.insert(Resource::Send(Arc::new(AsyncMutex::new(send))));
                (FLYNES_QUIC_OK, handle, Vec::new())
            }
            Err(_) => (FLYNES_QUIC_FAILED, 0, Vec::new()),
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_write(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    stream: u64,
    bytes: *const u8,
    bytes_size: usize,
    finish: u32,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let Some(bytes) = (unsafe { copy_bytes(bytes, bytes_size, true) }) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    if finish > 1 {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    }
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(stream) = state.send(stream) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        let mut stream = stream.lock().await;
        if stream.write_all(&bytes).await.is_err() || (finish == 1 && stream.finish().is_err()) {
            return (FLYNES_QUIC_FAILED, 0, Vec::new());
        }
        (FLYNES_QUIC_OK, 0, Vec::new())
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_finish(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    stream: u64,
) -> i32 {
    // SAFETY: the public finish operation is the zero-byte FIN form of write.
    unsafe { flynes_quic_provider_write(provider, operation, stream, std::ptr::null(), 0, 1) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_reset(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    stream: u64,
    reason: u32,
) -> i32 {
    // SAFETY: close applies QUIC RESET_STREAM when the resource is a send stream.
    unsafe { flynes_quic_provider_close(provider, operation, stream, reason) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_read(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    stream: u64,
    maximum: usize,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    if maximum == 0 || maximum > MAX_INPUT {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    }
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(stream) = state.recv(stream) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        let mut stream = stream.lock().await;
        match stream.read_chunk(maximum, true).await {
            Ok(Some(value)) => (FLYNES_QUIC_OK, value.offset, value.bytes.to_vec()),
            Ok(None) => (FLYNES_QUIC_OK, 0, Vec::new()),
            Err(_) => (FLYNES_QUIC_FAILED, 0, Vec::new()),
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_grant_read_credit(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    stream: u64,
    maximum: usize,
) -> i32 {
    // SAFETY: one credit grant maps to one bounded provider read completion.
    unsafe { flynes_quic_provider_read(provider, operation, stream, maximum) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_send_datagram(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    connection: u64,
    bytes: *const u8,
    bytes_size: usize,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let Some(bytes) = (unsafe { copy_bytes(bytes, bytes_size, false) }) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(connection) = state.connection(connection) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        match connection.send_datagram(Bytes::from(bytes)) {
            Ok(()) => (FLYNES_QUIC_OK, 0, Vec::new()),
            Err(_) => (FLYNES_QUIC_FAILED, 0, Vec::new()),
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_read_datagram(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    connection: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(connection) = state.connection(connection) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        match connection.read_datagram().await {
            Ok(value) => (FLYNES_QUIC_OK, 0, value.to_vec()),
            Err(_) => (FLYNES_QUIC_FAILED, 0, Vec::new()),
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_query(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    connection: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(connection) = state.connection(connection) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        let stats = connection.stats();
        let values = [
            connection.payload_budget().unwrap_or(0) as u64,
            stats.path.sent_packets,
            stats.path.lost_packets,
            stats.path.congestion_events,
        ];
        let bytes = values.into_iter().flat_map(u64::to_be_bytes).collect();
        (FLYNES_QUIC_OK, connection.stable_id() as u64, bytes)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_payload_budget(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    connection: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(connection) = state.connection(connection) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        match connection.payload_budget() {
            Some(value) if value != 0 => (
                FLYNES_QUIC_OK,
                connection.stable_id() as u64,
                (value as u64).to_be_bytes().to_vec(),
            ),
            _ => (FLYNES_QUIC_FAILED, 0, Vec::new()),
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_stats(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    connection: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let Some(connection) = state.connection(connection) else {
            return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
        };
        let stats = connection.stats();
        let values = [
            stats.path.sent_packets,
            stats.path.lost_packets,
            stats.path.congestion_events,
        ];
        let bytes = values.into_iter().flat_map(u64::to_be_bytes).collect();
        (FLYNES_QUIC_OK, connection.stable_id() as u64, bytes)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_close(
    provider: *mut FlynesQuicProvider,
    operation: u64,
    resource: u64,
    reason: u32,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let state = provider.state.clone();
    submit(provider, operation, async move {
        let removed = state
            .resources
            .lock()
            .ok()
            .and_then(|mut values| values.remove(&resource));
        match removed {
            Some(Resource::Connection(value)) => value.close(reason),
            Some(Resource::Send(value)) => {
                let _ = value.lock().await.reset(reason.into());
            }
            Some(Resource::Recv(value)) => {
                let _ = value.lock().await.stop(reason.into());
            }
            Some(Resource::Listener(_)) | Some(Resource::Material(_)) => {}
            None => return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new()),
        }
        (FLYNES_QUIC_OK, 0, Vec::new())
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_cancel(
    provider: *mut FlynesQuicProvider,
    operation: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let pending = provider
        .state
        .pending
        .lock()
        .ok()
        .and_then(|mut values| values.remove(&operation));
    let Some(pending) = pending else {
        return FLYNES_QUIC_INVALID_HANDLE;
    };
    if !pending.completed.swap(true, Ordering::AcqRel) {
        pending.abort.abort();
        provider
            .state
            .callbacks
            .complete(operation, FLYNES_QUIC_CANCELLED, 0, &[]);
    }
    FLYNES_QUIC_ACCEPTED
}
