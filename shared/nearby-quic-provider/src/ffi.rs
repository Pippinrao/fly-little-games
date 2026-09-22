//! Non-blocking C boundary. Every pointer supplied by a caller must remain valid for the
//! duration of that entry-point call; inputs are copied before return. Provider handles must
//! be live and balanced with retain/release. Callback contexts are retained at create and
//! released after all asynchronous callbacks have stopped.
#![allow(clippy::missing_safety_doc)]

use crate::{runtime, tls};
use bytes::Bytes;
use rcgen::{CertificateParams, KeyPair, PKCS_ECDSA_P256_SHA256, PublicKeyData};
use rustls::{
    crypto::ring::sign::any_supported_type,
    pki_types::{CertificateDer, PrivatePkcs8KeyDer},
    sign::SigningKey,
};
use std::{
    collections::HashMap,
    ffi::c_void,
    future::Future,
    slice, str,
    sync::{
        Arc, Mutex,
        atomic::{AtomicU64, AtomicUsize, Ordering},
    },
    time::Duration,
};
use tokio::{
    runtime::Runtime,
    sync::{Mutex as AsyncMutex, oneshot, watch},
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
    Send {
        parent_connection: u64,
        stream: Arc<AsyncMutex<quinn::SendStream>>,
    },
    Recv {
        parent_connection: u64,
        stream: Arc<AsyncMutex<quinn::RecvStream>>,
    },
}

impl Resource {
    fn parent_connection(&self) -> Option<u64> {
        match self {
            Self::Send {
                parent_connection, ..
            }
            | Self::Recv {
                parent_connection, ..
            } => Some(*parent_connection),
            _ => None,
        }
    }
}

struct PendingOperation {
    abort: tokio::task::AbortHandle,
    phase: Arc<Mutex<OperationPhase>>,
    parent_connection: Option<u64>,
    drained: watch::Receiver<bool>,
}

enum OperationPhase {
    Active,
    Cancelled,
    Closing,
    Committed,
}

struct ProviderTables {
    resources: HashMap<u64, Resource>,
    pending: HashMap<u64, PendingOperation>,
}

#[derive(Clone, Copy)]
enum Admission {
    None,
    Connection(u64),
    Send(u64),
    Recv(u64),
    Close(u64),
}

enum Admitted {
    None,
    Connection(Arc<runtime::ProductConnection>),
    Send(Arc<AsyncMutex<quinn::SendStream>>),
    Recv(Arc<AsyncMutex<quinn::RecvStream>>),
    Close(u64),
    Invalid,
}

impl Admitted {
    fn connection(self) -> Option<Arc<runtime::ProductConnection>> {
        match self {
            Self::Connection(value) => Some(value),
            _ => None,
        }
    }
    fn send(self) -> Option<Arc<AsyncMutex<quinn::SendStream>>> {
        match self {
            Self::Send(value) => Some(value),
            _ => None,
        }
    }
    fn recv(self) -> Option<Arc<AsyncMutex<quinn::RecvStream>>> {
        match self {
            Self::Recv(value) => Some(value),
            _ => None,
        }
    }
}

struct ProviderState {
    callbacks: Arc<CallbackTarget>,
    tables: Mutex<ProviderTables>,
    next_resource: AtomicU64,
    #[cfg(test)]
    gates: Mutex<Vec<lifecycle_tests::Gate>>,
}

impl ProviderState {
    fn material(&self, handle: u64) -> Option<Arc<Material>> {
        match self.tables.lock().ok()?.resources.get(&handle)? {
            Resource::Material(value) => Some(value.clone()),
            _ => None,
        }
    }
    fn listener(&self, handle: u64) -> Option<Arc<runtime::ProductListener>> {
        match self.tables.lock().ok()?.resources.get(&handle)? {
            Resource::Listener(value) => Some(value.clone()),
            _ => None,
        }
    }
    #[cfg(test)]
    fn send(&self, handle: u64) -> Option<Arc<AsyncMutex<quinn::SendStream>>> {
        match self.tables.lock().ok()?.resources.get(&handle)? {
            Resource::Send { stream, .. } => Some(stream.clone()),
            _ => None,
        }
    }
    #[cfg(test)]
    fn recv(&self, handle: u64) -> Option<Arc<AsyncMutex<quinn::RecvStream>>> {
        match self.tables.lock().ok()?.resources.get(&handle)? {
            Resource::Recv { stream, .. } => Some(stream.clone()),
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

fn diagnostic_failure(error: impl std::fmt::Debug) -> Completion {
    // Classify locally; never export arbitrary remote close reasons/certificate text.
    // Preserve nested Quinn causes such as ConnectionLost(TimedOut). Display
    // collapses these to "connection lost". Only the safe category escapes.
    let text = format!("{error:?}").to_ascii_lowercase();
    let category =
        if text.contains("timed out") || text.contains("deadline") || text.contains("timeout") || text.contains("timedout") {
            "timeout"
        } else if text.contains("certificate")
            || text.contains("cryptographic")
            || text.contains("pin mismatch")
        {
            "tls"
        } else if text.contains("closed") || text.contains("lost") {
            "closed"
        } else if text.contains("reset") || text.contains("stopped") {
            "reset"
        } else if text.contains("socket") || text.contains("network") || text.contains("os error") {
            "io"
        } else {
            "transport"
        };
    (FLYNES_QUIC_FAILED, 0, category.as_bytes().to_vec())
}

#[cfg(test)]
mod diagnostic_tests {
    use super::*;
    #[test]
    fn errors_are_classified_without_echoing_peer_text() {
        for (error, expected) in [
            ("timed out: private detail", "timeout"),
            ("connection closed: secret token", "closed"),
            ("certificate verify failed: private detail", "tls"),
            ("unknown private detail", "transport"),
        ] {
            let value = diagnostic_failure(error);
            assert_eq!(value.0, FLYNES_QUIC_FAILED);
            assert_eq!(value.2, expected.as_bytes());
        }
        let nested = quinn::ReadError::ConnectionLost(quinn::ConnectionError::TimedOut);
        assert_eq!(diagnostic_failure(nested).2, b"timeout");
    }
}

// Futures own these resources until terminal arbitration commits the entire batch.
// Dropping a cancelled result therefore cannot leave an unreported table entry.
enum CreatedResources {
    Single(Resource, Vec<u8>),
    Bidi(Resource, Resource),
}

fn submit_created<F>(provider: &FlynesQuicProvider, operation: u64, future: F) -> i32
where
    F: Future<Output = Result<CreatedResources, Completion>> + Send + 'static,
{
    submit_created_admitted(provider, operation, Admission::None, move |_| future)
}

fn submit_created_admitted<B, F>(
    provider: &FlynesQuicProvider,
    operation: u64,
    admission: Admission,
    build: B,
) -> i32
where
    B: FnOnce(Admitted) -> F,
    F: Future<Output = Result<CreatedResources, Completion>> + Send + 'static,
{
    submit_task(
        provider,
        operation,
        admission,
        move |admitted, _, _| build(admitted),
        |state, output| {
            let created = match output.as_ref().expect("joined creator output") {
                Ok(created) => created,
                Err(_) => {
                    let Some(Err(completion)) = output.take() else {
                        unreachable!()
                    };
                    return completion;
                }
            };
            let mut tables = state.tables.lock().expect("provider tables poisoned");
            let parent_connection = match created {
                CreatedResources::Single(resource, _) => resource.parent_connection(),
                CreatedResources::Bidi(send, recv) => {
                    let parent = send.parent_connection();
                    debug_assert_eq!(parent, recv.parent_connection());
                    parent
                }
            };
            if parent_connection.is_some_and(|parent| {
                !matches!(tables.resources.get(&parent), Some(Resource::Connection(_)))
            }) {
                drop(tables);
                return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
            }
            let Some(Ok(created)) = output.take() else {
                unreachable!()
            };
            let first = state.next_resource.fetch_add(1, Ordering::Relaxed);
            let bytes = match created {
                CreatedResources::Single(resource, bytes) => {
                    tables.resources.insert(first, resource);
                    bytes
                }
                CreatedResources::Bidi(send, recv) => {
                    let second = state.next_resource.fetch_add(1, Ordering::Relaxed);
                    tables.resources.insert(first, send);
                    tables.resources.insert(second, recv);
                    second.to_be_bytes().to_vec()
                }
            };
            (FLYNES_QUIC_OK, first, bytes)
        },
    )
}

#[cfg(test)]
fn submit<F>(provider: &FlynesQuicProvider, operation: u64, future: F) -> i32
where
    F: Future<Output = Completion> + Send + 'static,
{
    submit_admitted(provider, operation, Admission::None, move |_| future)
}

fn submit_admitted<B, F>(
    provider: &FlynesQuicProvider,
    operation: u64,
    admission: Admission,
    build: B,
) -> i32
where
    B: FnOnce(Admitted) -> F,
    F: Future<Output = Completion> + Send + 'static,
{
    submit_task(
        provider,
        operation,
        admission,
        move |admitted, _, _| build(admitted),
        |_, output| output.take().expect("joined operation output"),
    )
}

fn admit(tables: &ProviderTables, admission: Admission) -> (Admitted, Option<u64>) {
    match admission {
        Admission::None => (Admitted::None, None),
        Admission::Connection(handle) => match tables.resources.get(&handle) {
            Some(Resource::Connection(value)) => {
                (Admitted::Connection(value.clone()), Some(handle))
            }
            _ => (Admitted::Invalid, None),
        },
        Admission::Send(handle) => match tables.resources.get(&handle) {
            Some(Resource::Send {
                parent_connection,
                stream,
            }) if matches!(
                tables.resources.get(parent_connection),
                Some(Resource::Connection(_))
            ) =>
            {
                (Admitted::Send(stream.clone()), Some(*parent_connection))
            }
            _ => (Admitted::Invalid, None),
        },
        Admission::Recv(handle) => match tables.resources.get(&handle) {
            Some(Resource::Recv {
                parent_connection,
                stream,
            }) if matches!(
                tables.resources.get(parent_connection),
                Some(Resource::Connection(_))
            ) =>
            {
                (Admitted::Recv(stream.clone()), Some(*parent_connection))
            }
            _ => (Admitted::Invalid, None),
        },
        Admission::Close(handle) => match tables.resources.get(&handle) {
            Some(resource) => (Admitted::Close(handle), resource.parent_connection()),
            None => (Admitted::Invalid, None),
        },
    }
}

fn submit_task<B, F, T, C>(
    provider: &FlynesQuicProvider,
    operation: u64,
    admission: Admission,
    build: B,
    commit: C,
) -> i32
where
    B: FnOnce(Admitted, Arc<Mutex<OperationPhase>>, Arc<ProviderState>) -> F,
    F: Future<Output = T> + Send + 'static,
    T: Send + 'static,
    C: FnOnce(&ProviderState, &mut Option<T>) -> Completion + Send + 'static,
{
    if operation == 0 {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    }
    let phase = Arc::new(Mutex::new(OperationPhase::Active));
    let (start_sender, start_receiver) = oneshot::channel();
    let state = provider.state.clone();
    let (drained_sender, drained_receiver) = watch::channel(false);
    let handle = {
        let Ok(mut tables) = provider.state.tables.lock() else {
            return FLYNES_QUIC_CLOSED;
        };
        if tables.pending.contains_key(&operation) {
            return FLYNES_QUIC_DUPLICATE;
        }
        // Resolve, capture and register as one admission transaction. The worker
        // cannot poll until start_sender is signalled after this lock is gone.
        let (admitted, parent_connection) = admit(&tables, admission);
        let future = build(admitted, phase.clone(), state.clone());
        let handle = provider.runtime.spawn(async move {
            if start_receiver.await.is_err() {
                return None;
            }
            Some(future.await)
        });
        tables.pending.insert(
            operation,
            PendingOperation {
                abort: handle.abort_handle(),
                phase: phase.clone(),
                parent_connection,
                drained: drained_receiver,
            },
        );
        handle
    };
    provider.runtime.spawn(async move {
        let joined = handle.await;
        let mut output = joined.ok().flatten();
        #[cfg(test)]
        lifecycle_tests::pause(&state, operation, 0);
        let mut commit = Some(commit);
        let completion = {
            let mut phase = phase.lock().expect("operation mutex poisoned");
            match *phase {
                OperationPhase::Cancelled => (FLYNES_QUIC_CANCELLED, 0, Vec::new()),
                OperationPhase::Active | OperationPhase::Closing => {
                    *phase = OperationPhase::Committed;
                    if output.is_some() {
                        commit.take().unwrap()(&state, &mut output)
                    } else {
                        (FLYNES_QUIC_FAILED, 0, Vec::new())
                    }
                }
                OperationPhase::Committed => unreachable!(),
            }
        };
        // Resource destructors may release foreign contexts: run them outside
        // every table/operation lock and before the cancellation callback.
        drop(output);
        drop(commit);
        #[cfg(test)]
        lifecycle_tests::pause(&state, operation, 6);
        drained_sender.send_replace(true);
        #[cfg(test)]
        lifecycle_tests::pause(&state, operation, 1);
        let (result, resource, bytes) = completion;
        state
            .callbacks
            .complete(operation, result, resource, &bytes);
        if let Ok(mut tables) = state.tables.lock() {
            if tables
                .pending
                .get(&operation)
                .is_some_and(|entry| Arc::ptr_eq(&entry.phase, &phase))
            {
                tables.pending.remove(&operation);
            }
        }
    });
    #[cfg(test)]
    lifecycle_tests::pause(&provider.state, operation, 2);
    // Once registered, the supervisor owns exactly one terminal. Cancellation
    // may already have dropped the worker's receiver, but is still ACCEPTED.
    let _ = start_sender.send(());
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
            tables: Mutex::new(ProviderTables {
                resources: HashMap::new(),
                pending: HashMap::new(),
            }),
            next_resource: AtomicU64::new(1),
            #[cfg(test)]
            gates: Mutex::new(Vec::new()),
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
    submit_created(provider, operation, async move {
        Ok(CreatedResources::Single(
            Resource::Material(Arc::new(Material {
                certificates: vec![CertificateDer::from(certificate)],
                signing_key,
                spki,
            })),
            Vec::new(),
        ))
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_generate_self_signed(
    provider: *mut FlynesQuicProvider,
    operation: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    submit_created(provider, operation, async move {
        let Ok(key) = KeyPair::generate_for(&PKCS_ECDSA_P256_SHA256) else {
            return Err((FLYNES_QUIC_FAILED, 0, Vec::new()));
        };
        let spki = key.subject_public_key_info();
        let Ok(params) = CertificateParams::new(vec!["flynes.invalid".into()]) else {
            return Err((FLYNES_QUIC_FAILED, 0, Vec::new()));
        };
        let Ok(cert) = params.self_signed(&key) else {
            return Err((FLYNES_QUIC_FAILED, 0, Vec::new()));
        };
        let Ok(signing) = any_supported_type(&PrivatePkcs8KeyDer::from(key.serialize_der()).into())
        else {
            return Err((FLYNES_QUIC_FAILED, 0, Vec::new()));
        };
        let signing_key: Arc<dyn SigningKey> = signing;
        let certificates = vec![cert.der().clone()];
        if tls::server_config(certificates.clone(), signing_key.clone(), &spki).is_err() {
            return Err((FLYNES_QUIC_FAILED, 0, Vec::new()));
        }
        Ok(CreatedResources::Single(
            Resource::Material(Arc::new(Material {
                certificates,
                signing_key,
                spki: spki.clone(),
            })),
            spki,
        ))
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
    submit_created(provider, operation, async move {
        let Some(material) = state.material(material) else {
            return Err((FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new()));
        };
        let Ok(bind) = bind.parse() else {
            return Err((FLYNES_QUIC_INVALID_ARGUMENT, 0, Vec::new()));
        };
        match runtime::listen(
            bind,
            material.certificates.clone(),
            material.signing_key.clone(),
            &material.spki,
            Duration::from_millis(deadline_ms),
        ) {
            Ok(listener) => match listener.local_addr() {
                Ok(address) => Ok(CreatedResources::Single(
                    Resource::Listener(Arc::new(listener)),
                    address.to_string().into_bytes(),
                )),
                Err(error) => Err(diagnostic_failure(error)),
            },
            Err(error) => Err(diagnostic_failure(error)),
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
    submit_created(provider, operation, async move {
        let Some(listener) = state.listener(listener) else {
            return Err((FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new()));
        };
        match listener.accept().await {
            Ok(connection) => Ok(CreatedResources::Single(
                Resource::Connection(Arc::new(connection)),
                Vec::new(),
            )),
            Err(error) => Err(diagnostic_failure(error)),
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
    submit_created(provider, operation, async move {
        let (Ok(bind), Ok(peer)) = (bind.parse(), peer.parse()) else {
            return Err((FLYNES_QUIC_INVALID_ARGUMENT, 0, Vec::new()));
        };
        match runtime::connect(
            bind,
            peer,
            &expected_spki_hash,
            Duration::from_millis(deadline_ms),
        )
        .await
        {
            Ok(connection) => Ok(CreatedResources::Single(
                Resource::Connection(Arc::new(connection)),
                Vec::new(),
            )),
            Err(error) => Err(diagnostic_failure(error)),
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
    submit_admitted(
        provider,
        operation,
        Admission::Connection(connection),
        |admitted| async move {
            let Some(connection) = admitted.connection() else {
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
        },
    )
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
    submit_admitted(
        provider,
        operation,
        Admission::Connection(connection),
        |admitted| async move {
            let Some(connection) = admitted.connection() else {
                return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
            };
            match connection.exporter(&context) {
                Ok(value) => (FLYNES_QUIC_OK, 0, value.to_vec()),
                Err(error) => diagnostic_failure(error),
            }
        },
    )
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
    let connection_handle = connection;
    submit_created_admitted(
        provider,
        operation,
        Admission::Connection(connection),
        |admitted| async move {
            let Some(connection) = admitted.connection() else {
                return Err((FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new()));
            };
            match connection.open_bidi().await {
                Ok((send, recv)) => Ok(CreatedResources::Bidi(
                    Resource::Send {
                        parent_connection: connection_handle,
                        stream: Arc::new(AsyncMutex::new(send)),
                    },
                    Resource::Recv {
                        parent_connection: connection_handle,
                        stream: Arc::new(AsyncMutex::new(recv)),
                    },
                )),
                Err(error) => Err(diagnostic_failure(error)),
            }
        },
    )
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
    let connection_handle = connection;
    submit_created_admitted(
        provider,
        operation,
        Admission::Connection(connection),
        |admitted| async move {
            let Some(connection) = admitted.connection() else {
                return Err((FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new()));
            };
            match connection.accept_bidi().await {
                Ok((send, recv)) => Ok(CreatedResources::Bidi(
                    Resource::Send {
                        parent_connection: connection_handle,
                        stream: Arc::new(AsyncMutex::new(send)),
                    },
                    Resource::Recv {
                        parent_connection: connection_handle,
                        stream: Arc::new(AsyncMutex::new(recv)),
                    },
                )),
                Err(error) => Err(diagnostic_failure(error)),
            }
        },
    )
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
    let connection_handle = connection;
    submit_created_admitted(
        provider,
        operation,
        Admission::Connection(connection),
        |admitted| async move {
            let Some(connection) = admitted.connection() else {
                return Err((FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new()));
            };
            match connection.open_uni().await {
                Ok(send) => Ok(CreatedResources::Single(
                    Resource::Send {
                        parent_connection: connection_handle,
                        stream: Arc::new(AsyncMutex::new(send)),
                    },
                    Vec::new(),
                )),
                Err(error) => Err(diagnostic_failure(error)),
            }
        },
    )
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
    submit_admitted(
        provider,
        operation,
        Admission::Send(stream),
        |admitted| async move {
            let Some(stream) = admitted.send() else {
                return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
            };
            let mut stream = stream.lock().await;
            if let Err(error) = stream.write_all(&bytes).await {
                return diagnostic_failure(error);
            }
            if finish == 1 {
                if let Err(error) = stream.finish() {
                    return diagnostic_failure(error);
                }
            }
            (FLYNES_QUIC_OK, 0, Vec::new())
        },
    )
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
    #[cfg(test)]
    let state = provider.state.clone();
    submit_admitted(
        provider,
        operation,
        Admission::Recv(stream),
        |admitted| async move {
            let Some(stream) = admitted.recv() else {
                return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
            };
            let mut stream = stream.lock().await;
            #[cfg(test)]
            let read_result = {
                let mut read = std::pin::pin!(stream.read_chunk(maximum, true));
                let mut observed = false;
                std::future::poll_fn(|context| {
                    let result = read.as_mut().poll(context);
                    if result.is_pending() && !observed {
                        observed = true;
                        lifecycle_tests::observe(&state, operation, 5);
                    }
                    result
                })
                .await
            };
            #[cfg(not(test))]
            let read_result = stream.read_chunk(maximum, true).await;
            match read_result {
                Ok(Some(value)) => (FLYNES_QUIC_OK, value.offset, value.bytes.to_vec()),
                Ok(None) => (FLYNES_QUIC_OK, 0, Vec::new()),
                Err(error) => diagnostic_failure(error),
            }
        },
    )
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
    submit_admitted(
        provider,
        operation,
        Admission::Connection(connection),
        |admitted| async move {
            let Some(connection) = admitted.connection() else {
                return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
            };
            match connection.send_datagram(Bytes::from(bytes)) {
                Ok(()) => (FLYNES_QUIC_OK, 0, Vec::new()),
                Err(error) => diagnostic_failure(error),
            }
        },
    )
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
    submit_admitted(
        provider,
        operation,
        Admission::Connection(connection),
        |admitted| async move {
            let Some(connection) = admitted.connection() else {
                return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
            };
            match connection.read_datagram().await {
                Ok(value) => (FLYNES_QUIC_OK, 0, value.to_vec()),
                Err(error) => diagnostic_failure(error),
            }
        },
    )
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
    submit_admitted(
        provider,
        operation,
        Admission::Connection(connection),
        |admitted| async move {
            let Some(connection) = admitted.connection() else {
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
        },
    )
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
    submit_admitted(
        provider,
        operation,
        Admission::Connection(connection),
        |admitted| async move {
            let Some(connection) = admitted.connection() else {
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
        },
    )
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
    submit_admitted(
        provider,
        operation,
        Admission::Connection(connection),
        |admitted| async move {
            let Some(connection) = admitted.connection() else {
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
        },
    )
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
    submit_task(
        provider,
        operation,
        Admission::Close(resource),
        move |admitted, phase, state| async move {
            let Admitted::Close(handle) = admitted else {
                return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
            };
            #[cfg(test)]
            lifecycle_tests::pause(&state, operation, 3);
            // Cancellation and removal arbitrate on the same phase. No resource
            // destructor or callback runs while either lock is held.
            let (removed, children, receipts) = {
                let mut phase = phase.lock().expect("operation mutex poisoned");
                if matches!(*phase, OperationPhase::Cancelled) {
                    return (FLYNES_QUIC_CANCELLED, 0, Vec::new());
                }
                let mut tables = state.tables.lock().expect("provider tables poisoned");
                let Some(removed) = tables.resources.remove(&handle) else {
                    return (FLYNES_QUIC_INVALID_HANDLE, 0, Vec::new());
                };
                *phase = OperationPhase::Closing;
                let (children, receipts) = if matches!(removed, Resource::Connection(_)) {
                    let handles: Vec<_> = tables
                        .resources
                        .iter()
                        .filter_map(|(child, value)| {
                            (value.parent_connection() == Some(handle)).then_some(*child)
                        })
                        .collect();
                    let children = handles
                        .into_iter()
                        .filter_map(|child| tables.resources.remove(&child))
                        .collect();
                    let receipts = tables
                        .pending
                        .values()
                        .filter(|entry| entry.parent_connection == Some(handle))
                        .map(|entry| entry.drained.clone())
                        .collect();
                    (children, receipts)
                } else {
                    (Vec::new(), Vec::new())
                };
                (removed, children, receipts)
            };
            #[cfg(test)]
            lifecycle_tests::pause(&state, operation, 4);
            match removed {
                Resource::Connection(value) => {
                    value.close(reason);
                    drop(children);
                    drop(value);
                    for mut receipt in receipts {
                        while !*receipt.borrow() {
                            if receipt.changed().await.is_err() {
                                return (FLYNES_QUIC_FAILED, 0, Vec::new());
                            }
                        }
                    }
                }
                Resource::Send { stream, .. } => {
                    let _ = stream.lock().await.reset(reason.into());
                    drop(stream);
                }
                Resource::Recv { stream, .. } => {
                    let _ = stream.lock().await.stop(reason.into());
                    drop(stream);
                }
                Resource::Listener(_) | Resource::Material(_) => {}
            }
            (FLYNES_QUIC_OK, 0, Vec::new())
        },
        |_, output| output.take().expect("joined close output"),
    )
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn flynes_quic_provider_cancel(
    provider: *mut FlynesQuicProvider,
    operation: u64,
) -> i32 {
    let Some(provider) = provider_ref(provider) else {
        return FLYNES_QUIC_INVALID_ARGUMENT;
    };
    let pending = provider.state.tables.lock().ok().and_then(|tables| {
        tables
            .pending
            .get(&operation)
            .map(|entry| (entry.phase.clone(), entry.abort.clone()))
    });
    let Some((phase, abort)) = pending else {
        return FLYNES_QUIC_INVALID_HANDLE;
    };
    let cancel = {
        let mut phase = phase.lock().expect("operation mutex poisoned");
        if matches!(*phase, OperationPhase::Active) {
            *phase = OperationPhase::Cancelled;
            true
        } else {
            false
        }
    };
    if cancel {
        abort.abort();
    }
    FLYNES_QUIC_ACCEPTED
}

#[cfg(test)]
mod lifecycle_tests {
    use super::*;
    use std::sync::atomic::AtomicBool;
    use std::sync::mpsc::{self, Receiver, Sender};

    const WAIT: Duration = Duration::from_secs(3);
    pub(super) struct Gate {
        operation: u64,
        point: u8,
        entered: Sender<()>,
        resume: Receiver<()>,
    }
    pub(super) fn pause(state: &ProviderState, operation: u64, point: u8) {
        let gate = {
            let mut gates = state.gates.lock().unwrap();
            gates
                .iter()
                .position(|g| g.operation == operation && g.point == point)
                .map(|index| gates.remove(index))
        };
        if let Some(gate) = gate {
            let _ = gate.entered.send(());
            // The test's Sender is the permit: sending or dropping it releases
            // the gate, including during assertion unwinding. Never silently
            // continue a correctness gate after a timer expires.
            let _ = gate.resume.recv();
        }
    }
    pub(super) fn observe(state: &ProviderState, operation: u64, point: u8) {
        let gate = {
            let mut gates = state.gates.lock().unwrap();
            gates
                .iter()
                .position(|g| g.operation == operation && g.point == point)
                .map(|index| gates.remove(index))
        };
        if let Some(gate) = gate {
            let _ = gate.entered.send(());
        }
    }
    fn gate(
        provider: &FlynesQuicProvider,
        operation: u64,
        point: u8,
    ) -> (Receiver<()>, Sender<()>) {
        let (entered, reached) = mpsc::channel();
        let (resume, permit) = mpsc::channel();
        provider.state.gates.lock().unwrap().push(Gate {
            operation,
            point,
            entered,
            resume: permit,
        });
        (reached, resume)
    }
    #[derive(Debug)]
    struct Event {
        operation: u64,
        result: i32,
        resource: u64,
        bytes: Vec<u8>,
    }
    struct CallbackContext {
        sender: Sender<Event>,
        reenter: AtomicBool,
        provider: Mutex<usize>,
        reentry: Mutex<Vec<i32>>,
    }
    unsafe extern "C" fn retain(context: *mut c_void) {
        // SAFETY: Fixture supplies Arc::as_ptr and keeps its own owner alive.
        unsafe { Arc::increment_strong_count(context.cast::<CallbackContext>()) };
    }
    unsafe extern "C" fn release(context: *mut c_void) {
        // SAFETY: balances the callback target's one successful retain.
        unsafe { Arc::decrement_strong_count(context.cast::<CallbackContext>()) };
    }
    unsafe extern "C" fn complete(
        context: *mut c_void,
        operation: u64,
        result: i32,
        terminal: u32,
        resource: u64,
        bytes: *const u8,
        size: usize,
    ) {
        assert_eq!(terminal, 1);
        let context = unsafe { &*context.cast::<CallbackContext>() };
        let bytes = if size == 0 {
            Vec::new()
        } else {
            unsafe { slice::from_raw_parts(bytes, size) }.to_vec()
        };
        if context.reenter.swap(false, Ordering::AcqRel) {
            // Keep the independent gate through every provider access. Teardown
            // disables new reentry and waits for any old reentry before dropping it.
            let guarded_pointer = context.provider.lock().unwrap();
            let pointer = *guarded_pointer as *mut FlynesQuicProvider;
            if !pointer.is_null() {
                let provider = unsafe { &*pointer };
                let locks_free = provider.state.tables.try_lock().is_ok();
                let phase = provider
                    .state
                    .tables
                    .lock()
                    .unwrap()
                    .pending
                    .get(&operation)
                    .unwrap()
                    .phase
                    .clone();
                let locks_free = locks_free && phase.try_lock().is_ok();
                if locks_free {
                    let results = unsafe {
                        vec![
                            flynes_quic_provider_cancel(pointer, operation),
                            flynes_quic_provider_generate_self_signed(pointer, operation),
                            flynes_quic_provider_cancel(pointer, 9999),
                        ]
                    };
                    *context.reentry.lock().unwrap() = results;
                }
            }
        }
        let _ = context.sender.send(Event {
            operation,
            result,
            resource,
            bytes,
        });
    }
    struct Fixture {
        provider: *mut FlynesQuicProvider,
        context: Arc<CallbackContext>,
        events: Receiver<Event>,
    }
    impl Fixture {
        fn new() -> Self {
            let (sender, events) = mpsc::channel();
            let context = Arc::new(CallbackContext {
                sender,
                reenter: AtomicBool::new(false),
                provider: Mutex::new(0),
                reentry: Mutex::new(Vec::new()),
            });
            let callbacks = FlynesQuicCallbacks {
                struct_size: size_of::<FlynesQuicCallbacks>() as u32,
                abi_version: FLYNES_QUIC_PROVIDER_ABI_V1,
                context: Arc::as_ptr(&context).cast_mut().cast(),
                retain: Some(retain),
                release: Some(release),
                completion: Some(complete),
            };
            let provider = unsafe { flynes_quic_provider_create(&callbacks) };
            assert!(!provider.is_null());
            *context.provider.lock().unwrap() = provider as usize;
            Self {
                provider,
                context,
                events,
            }
        }
        fn get(&self) -> &FlynesQuicProvider {
            unsafe { &*self.provider }
        }
        fn event(&self, operation: u64) -> Event {
            let event = self.events.recv_timeout(WAIT).unwrap();
            assert_eq!(event.operation, operation);
            event
        }
        fn connections(&self) -> (u64, u64) {
            assert_eq!(
                unsafe { flynes_quic_provider_generate_self_signed(self.provider, 10) },
                FLYNES_QUIC_ACCEPTED
            );
            let material = self.event(10);
            assert_eq!(material.result, FLYNES_QUIC_OK);
            let pin = ring::digest::digest(&ring::digest::SHA256, &material.bytes);
            assert_eq!(
                unsafe {
                    flynes_quic_provider_listen(
                        self.provider,
                        11,
                        b"127.0.0.1:0".as_ptr(),
                        11,
                        material.resource,
                        2000,
                    )
                },
                FLYNES_QUIC_ACCEPTED
            );
            let listener = self.event(11);
            assert_eq!(listener.result, FLYNES_QUIC_OK);
            assert_eq!(
                unsafe { flynes_quic_provider_accept(self.provider, 12, listener.resource) },
                FLYNES_QUIC_ACCEPTED
            );
            assert_eq!(
                unsafe {
                    flynes_quic_provider_connect(
                        self.provider,
                        13,
                        b"127.0.0.1:0".as_ptr(),
                        11,
                        listener.bytes.as_ptr(),
                        listener.bytes.len(),
                        pin.as_ref().as_ptr(),
                        pin.as_ref().len(),
                        2000,
                    )
                },
                FLYNES_QUIC_ACCEPTED
            );
            let a = self.events.recv_timeout(WAIT).unwrap();
            let b = self.events.recv_timeout(WAIT).unwrap();
            assert_eq!((a.result, b.result), (FLYNES_QUIC_OK, FLYNES_QUIC_OK));
            if a.operation == 13 {
                (a.resource, b.resource)
            } else {
                (b.resource, a.resource)
            }
        }
        fn another_pair(&self, accept_operation: u64, connect_operation: u64) -> (u64, u64) {
            let (listener_handle, listener, spki) = {
                let tables = self.get().state.tables.lock().unwrap();
                let (handle, listener) = tables
                    .resources
                    .iter()
                    .find_map(|(handle, resource)| match resource {
                        Resource::Listener(value) => Some((*handle, value.clone())),
                        _ => None,
                    })
                    .unwrap();
                let spki = tables
                    .resources
                    .values()
                    .find_map(|resource| match resource {
                        Resource::Material(value) => Some(value.spki.clone()),
                        _ => None,
                    })
                    .unwrap();
                (handle, listener, spki)
            };
            let address = listener.local_addr().unwrap().to_string();
            let pin = ring::digest::digest(&ring::digest::SHA256, &spki);
            assert_eq!(
                unsafe {
                    flynes_quic_provider_accept(self.provider, accept_operation, listener_handle)
                },
                FLYNES_QUIC_ACCEPTED
            );
            assert_eq!(
                unsafe {
                    flynes_quic_provider_connect(
                        self.provider,
                        connect_operation,
                        b"127.0.0.1:0".as_ptr(),
                        11,
                        address.as_ptr(),
                        address.len(),
                        pin.as_ref().as_ptr(),
                        pin.as_ref().len(),
                        2000,
                    )
                },
                FLYNES_QUIC_ACCEPTED
            );
            let first = self.events.recv_timeout(WAIT).unwrap();
            let second = self.events.recv_timeout(WAIT).unwrap();
            assert_eq!([first.result, second.result], [FLYNES_QUIC_OK; 2]);
            if first.operation == connect_operation {
                assert_eq!(second.operation, accept_operation);
                (first.resource, second.resource)
            } else {
                assert_eq!(
                    (first.operation, second.operation),
                    (accept_operation, connect_operation)
                );
                (second.resource, first.resource)
            }
        }
    }
    impl Drop for Fixture {
        fn drop(&mut self) {
            *self.context.provider.lock().unwrap() = 0;
            // Runtime::drop joins all workers without a timeout. Box keeps its
            // allocation until every field has dropped; no runtime field is moved
            // out while a callback can reenter. Context also has a real retained Arc.
            unsafe { drop(Box::from_raw(self.provider)) };
        }
    }

    #[test]
    fn fixture_context_reference_is_balanced_after_join() {
        let fixture = Fixture::new();
        assert_eq!(Arc::strong_count(&fixture.context), 2);
        let context = fixture.context.clone();
        drop(fixture);
        assert_eq!(Arc::strong_count(&context), 1);
        assert_eq!(*context.provider.lock().unwrap(), 0);
    }

    #[test]
    fn fixture_destruction_joins_blocked_workers_without_timeout() {
        let (started, running) = mpsc::channel();
        let (resume, permit) = mpsc::channel();
        let (destroyed, ended) = mpsc::channel();
        let owner = std::thread::spawn(move || {
            let fixture = Fixture::new();
            fixture.get().runtime.spawn_blocking(move || {
                started.send(()).unwrap();
                let _ = permit.recv();
            });
            drop(fixture);
            destroyed.send(()).unwrap();
        });
        running.recv_timeout(WAIT).unwrap();
        // Exceed the old teardown timeout while the worker is deliberately held.
        // No worker dereferences the provider/context, even against the broken fixture.
        let premature = ended.recv_timeout(WAIT + Duration::from_secs(1)).is_ok();
        let _ = resume.send(());
        owner.join().unwrap();
        assert!(
            !premature,
            "fixture returned before its held runtime worker exited"
        );
        ended.recv_timeout(WAIT).unwrap();
    }

    #[test]
    fn cancel_before_creator_commit_leaves_no_resource() {
        let fixture = Fixture::new();
        let (reached, resume) = gate(fixture.get(), 1, 0);
        assert_eq!(
            unsafe { flynes_quic_provider_generate_self_signed(fixture.provider, 1) },
            FLYNES_QUIC_ACCEPTED
        );
        reached.recv_timeout(WAIT).unwrap();
        assert_eq!(
            unsafe { flynes_quic_provider_cancel(fixture.provider, 1) },
            FLYNES_QUIC_ACCEPTED
        );
        resume.send(()).unwrap();
        let event = fixture.event(1);
        assert_eq!(event.result, FLYNES_QUIC_CANCELLED);
        assert_eq!(event.resource, 0);
        assert!(
            fixture
                .get()
                .state
                .tables
                .lock()
                .unwrap()
                .resources
                .is_empty(),
            "cancelled creator leaked committed resources"
        );
    }

    struct DropProbe {
        entered: Sender<()>,
        resume: Receiver<()>,
    }
    impl Drop for DropProbe {
        fn drop(&mut self) {
            let _ = self.entered.send(());
            let _ = self.resume.recv();
        }
    }
    #[test]
    fn cancellation_terminal_waits_for_business_future_drop() {
        let fixture = Fixture::new();
        let (started, running) = mpsc::channel();
        let (entered, dropping) = mpsc::channel();
        let (resume, permit) = mpsc::channel();
        let probe = DropProbe {
            entered,
            resume: permit,
        };
        assert_eq!(
            submit(fixture.get(), 2, async move {
                let _probe = probe;
                started.send(()).unwrap();
                std::future::pending::<()>().await;
                (FLYNES_QUIC_OK, 0, Vec::new())
            }),
            FLYNES_QUIC_ACCEPTED
        );
        running.recv_timeout(WAIT).unwrap();
        assert_eq!(
            unsafe { flynes_quic_provider_cancel(fixture.provider, 2) },
            FLYNES_QUIC_ACCEPTED
        );
        dropping.recv_timeout(WAIT).unwrap();
        let premature = fixture.events.try_recv().ok();
        resume.send(()).unwrap();
        assert!(
            premature.is_none(),
            "CANCELLED preceded business future destruction: {premature:?}"
        );
        assert_eq!(fixture.event(2).result, FLYNES_QUIC_CANCELLED);
    }

    #[test]
    fn committed_success_survives_cancel_before_callback() {
        let fixture = Fixture::new();
        let (reached, resume) = gate(fixture.get(), 3, 1);
        assert_eq!(
            unsafe { flynes_quic_provider_generate_self_signed(fixture.provider, 3) },
            FLYNES_QUIC_ACCEPTED
        );
        reached.recv_timeout(WAIT).unwrap();
        assert_eq!(
            unsafe { flynes_quic_provider_cancel(fixture.provider, 3) },
            FLYNES_QUIC_ACCEPTED
        );
        let duplicate = unsafe { flynes_quic_provider_generate_self_signed(fixture.provider, 3) };
        resume.send(()).unwrap();
        assert_eq!(
            duplicate, FLYNES_QUIC_DUPLICATE,
            "success awaiting callback must retain operation ownership"
        );
        let event = fixture.event(3);
        assert_eq!(event.result, FLYNES_QUIC_OK);
        assert!(!event.bytes.is_empty());
        assert!(fixture.get().state.material(event.resource).is_some());
        assert!(fixture.events.try_recv().is_err());
    }

    #[test]
    fn callback_reentry_keeps_id_owned_without_holding_locks() {
        let fixture = Fixture::new();
        fixture.context.reenter.store(true, Ordering::Release);
        assert_eq!(
            unsafe { flynes_quic_provider_generate_self_signed(fixture.provider, 4) },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(fixture.event(4).result, FLYNES_QUIC_OK);
        assert_eq!(
            *fixture.context.reentry.lock().unwrap(),
            vec![
                FLYNES_QUIC_ACCEPTED,
                FLYNES_QUIC_DUPLICATE,
                FLYNES_QUIC_INVALID_HANDLE
            ]
        );
    }

    #[test]
    fn real_stream_creation_commits_whole_pairs_and_cancel_drops_batches() {
        let fixture = Fixture::new();
        let (client, server) = fixture.connections();
        let baseline = fixture.get().state.tables.lock().unwrap().resources.len();
        for (operation, bidi) in [(20, true), (21, false)] {
            let (reached, resume) = gate(fixture.get(), operation, 0);
            let result = unsafe {
                if bidi {
                    flynes_quic_provider_open_bidi(fixture.provider, operation, client)
                } else {
                    flynes_quic_provider_open_uni(fixture.provider, operation, client)
                }
            };
            assert_eq!(result, FLYNES_QUIC_ACCEPTED);
            reached.recv_timeout(WAIT).unwrap();
            let before = fixture.get().state.tables.lock().unwrap().resources.len();
            assert_eq!(
                unsafe { flynes_quic_provider_cancel(fixture.provider, operation) },
                FLYNES_QUIC_ACCEPTED
            );
            resume.send(()).unwrap();
            assert_eq!(fixture.event(operation).result, FLYNES_QUIC_CANCELLED);
            assert_eq!(
                before, baseline,
                "uncommitted stream batch must stay out of table"
            );
            assert_eq!(
                fixture.get().state.tables.lock().unwrap().resources.len(),
                baseline
            );
        }
        let (reached, resume) = gate(fixture.get(), 22, 1);
        assert_eq!(
            unsafe { flynes_quic_provider_open_bidi(fixture.provider, 22, client) },
            FLYNES_QUIC_ACCEPTED
        );
        reached.recv_timeout(WAIT).unwrap();
        let committed_count = fixture.get().state.tables.lock().unwrap().resources.len();
        assert_eq!(
            unsafe { flynes_quic_provider_cancel(fixture.provider, 22) },
            FLYNES_QUIC_ACCEPTED
        );
        resume.send(()).unwrap();
        let opened = fixture.event(22);
        assert_eq!(opened.result, FLYNES_QUIC_OK);
        assert_eq!(committed_count, baseline + 2);
        assert_eq!(opened.bytes.len(), 8);
        let recv = u64::from_be_bytes(opened.bytes.try_into().unwrap());
        assert!(fixture.get().state.send(opened.resource).is_some());
        assert!(fixture.get().state.recv(recv).is_some());
        assert_eq!(
            unsafe {
                flynes_quic_provider_write(
                    fixture.provider,
                    23,
                    opened.resource,
                    b"x".as_ptr(),
                    1,
                    0,
                )
            },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(fixture.event(23).result, FLYNES_QUIC_OK);
        let (reached, resume) = gate(fixture.get(), 24, 0);
        assert_eq!(
            unsafe { flynes_quic_provider_accept_bidi(fixture.provider, 24, server) },
            FLYNES_QUIC_ACCEPTED
        );
        reached.recv_timeout(WAIT).unwrap();
        let before = fixture.get().state.tables.lock().unwrap().resources.len();
        assert_eq!(
            unsafe { flynes_quic_provider_cancel(fixture.provider, 24) },
            FLYNES_QUIC_ACCEPTED
        );
        resume.send(()).unwrap();
        assert_eq!(fixture.event(24).result, FLYNES_QUIC_CANCELLED);
        assert_eq!(before, baseline + 2);
        assert_eq!(
            fixture.get().state.tables.lock().unwrap().resources.len(),
            baseline + 2
        );
        assert!(fixture.events.try_recv().is_err());
    }

    #[test]
    fn a2_close_removes_connection_and_all_children() {
        let fixture = Fixture::new();
        let (client, _server) = fixture.connections();
        let baseline = fixture.get().state.tables.lock().unwrap().resources.len();
        assert_eq!(
            unsafe { flynes_quic_provider_open_bidi(fixture.provider, 40, client) },
            FLYNES_QUIC_ACCEPTED
        );
        let bidi = fixture.event(40);
        assert_eq!(bidi.result, FLYNES_QUIC_OK);
        assert_eq!(bidi.bytes.len(), 8);
        let bidi_send = bidi.resource;
        let bidi_recv = u64::from_be_bytes(bidi.bytes.try_into().unwrap());
        assert_eq!(
            unsafe { flynes_quic_provider_open_uni(fixture.provider, 41, client) },
            FLYNES_QUIC_ACCEPTED
        );
        let uni = fixture.event(41);
        assert_eq!(uni.result, FLYNES_QUIC_OK);
        let uni_send = uni.resource;
        let handles = [client, bidi_send, bidi_recv, uni_send];
        let (connection_weak, bidi_send_weak, bidi_recv_weak, uni_send_weak) = {
            let tables = fixture.get().state.tables.lock().unwrap();
            let resources = &tables.resources;
            assert_eq!(resources.len(), baseline + 3);
            assert!(handles.iter().all(|handle| *handle != 0));
            for (index, handle) in handles.iter().enumerate() {
                assert!(!handles[..index].contains(handle));
            }
            let Some(Resource::Connection(connection)) = resources.get(&client) else {
                panic!("real client connection must be registered");
            };
            let Some(Resource::Send { stream: send, .. }) = resources.get(&bidi_send) else {
                panic!("real bidirectional send stream must be registered");
            };
            let Some(Resource::Recv { stream: recv, .. }) = resources.get(&bidi_recv) else {
                panic!("real bidirectional receive stream must be registered");
            };
            let Some(Resource::Send { stream: uni, .. }) = resources.get(&uni_send) else {
                panic!("real unidirectional send stream must be registered");
            };
            (
                Arc::downgrade(connection),
                Arc::downgrade(send),
                Arc::downgrade(recv),
                Arc::downgrade(uni),
            )
        };

        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 42, client, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(fixture.event(42).result, FLYNES_QUIC_OK);
        // Observe close's terminal guarantee while the provider remains alive.
        // Only Weak references escape the resource-table snapshot above.
        let present_after_close = {
            let tables = fixture.get().state.tables.lock().unwrap();
            let resources = &tables.resources;
            handles.map(|handle| resources.contains_key(&handle))
        };
        let alive_after_close = [
            connection_weak.upgrade().is_some(),
            bidi_send_weak.upgrade().is_some(),
            bidi_recv_weak.upgrade().is_some(),
            uni_send_weak.upgrade().is_some(),
        ];

        assert_eq!(
            unsafe { flynes_quic_provider_query(fixture.provider, 43, client) },
            FLYNES_QUIC_ACCEPTED
        );
        let query = fixture.event(43);
        let mut writes = Vec::new();
        for (operation, stream) in [(44, bidi_send), (45, uni_send)] {
            assert_eq!(
                unsafe {
                    flynes_quic_provider_write(
                        fixture.provider,
                        operation,
                        stream,
                        b"x".as_ptr(),
                        1,
                        0,
                    )
                },
                FLYNES_QUIC_ACCEPTED
            );
            writes.push(fixture.event(operation).result);
        }
        assert_eq!(
            unsafe { flynes_quic_provider_read(fixture.provider, 46, bidi_recv, 1) },
            FLYNES_QUIC_ACCEPTED
        );
        let read = fixture.event(46);
        eprintln!(
            "close OK: present={present_after_close:?}, alive={alive_after_close:?}, query={}, writes={writes:?}, read={}",
            query.result, read.result
        );
        assert!(!present_after_close[0], "close retained the client handle");
        assert_eq!(
            &present_after_close[1..],
            &[false; 3],
            "close OK must remove all three child stream handles"
        );
        assert_eq!(
            alive_after_close, [false; 4],
            "close OK must drop the connection and all child resource owners"
        );
        assert_eq!(query.result, FLYNES_QUIC_INVALID_HANDLE);
        assert_eq!(writes, vec![FLYNES_QUIC_INVALID_HANDLE; 2]);
        assert_eq!(read.result, FLYNES_QUIC_INVALID_HANDLE);
        assert!(fixture.events.try_recv().is_err());
    }

    #[test]
    fn a2_close_rejects_late_stream_batch() {
        let fixture = Fixture::new();
        let (client, _server) = fixture.connections();
        let (reached, resume) = gate(fixture.get(), 50, 0);
        assert_eq!(
            unsafe { flynes_quic_provider_open_bidi(fixture.provider, 50, client) },
            FLYNES_QUIC_ACCEPTED
        );
        reached.recv_timeout(WAIT).unwrap();
        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 51, client, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        assert!(
            fixture
                .events
                .recv_timeout(Duration::from_millis(250))
                .is_err(),
            "close must wait for the admitted creator to discard its late batch"
        );
        resume.send(()).unwrap();
        let first = fixture.events.recv_timeout(WAIT).unwrap();
        let second = fixture.events.recv_timeout(WAIT).unwrap();
        assert!(
            [
                (first.operation, first.result),
                (second.operation, second.result)
            ]
            .contains(&(50, FLYNES_QUIC_INVALID_HANDLE))
        );
        assert!(
            [
                (first.operation, first.result),
                (second.operation, second.result)
            ]
            .contains(&(51, FLYNES_QUIC_OK))
        );
        assert!(
            fixture
                .get()
                .state
                .tables
                .lock()
                .unwrap()
                .resources
                .values()
                .all(|value| value.parent_connection() != Some(client)),
            "late stream commit must not recreate children of a closed connection"
        );
    }

    #[test]
    fn a2_close_rejects_late_uni_and_accepted_bidi_batches() {
        for accept_bidi in [false, true] {
            let fixture = Fixture::new();
            let (client, server) = fixture.connections();
            let parent = if accept_bidi { server } else { client };
            if accept_bidi {
                assert_eq!(
                    unsafe { flynes_quic_provider_open_bidi(fixture.provider, 140, client) },
                    FLYNES_QUIC_ACCEPTED
                );
                let opened = fixture.event(140);
                assert_eq!(opened.result, FLYNES_QUIC_OK);
                assert_eq!(
                    unsafe {
                        flynes_quic_provider_write(
                            fixture.provider,
                            141,
                            opened.resource,
                            b"x".as_ptr(),
                            1,
                            0,
                        )
                    },
                    FLYNES_QUIC_ACCEPTED
                );
                assert_eq!(fixture.event(141).result, FLYNES_QUIC_OK);
            }
            let (reached, resume) = gate(fixture.get(), 142, 0);
            let accepted = if accept_bidi {
                unsafe { flynes_quic_provider_accept_bidi(fixture.provider, 142, parent) }
            } else {
                unsafe { flynes_quic_provider_open_uni(fixture.provider, 142, parent) }
            };
            assert_eq!(accepted, FLYNES_QUIC_ACCEPTED);
            reached.recv_timeout(WAIT).unwrap();
            assert_eq!(
                unsafe { flynes_quic_provider_close(fixture.provider, 143, parent, 0) },
                FLYNES_QUIC_ACCEPTED
            );
            assert!(
                fixture
                    .events
                    .recv_timeout(Duration::from_millis(100))
                    .is_err()
            );
            resume.send(()).unwrap();
            let first = fixture.events.recv_timeout(WAIT).unwrap();
            let second = fixture.events.recv_timeout(WAIT).unwrap();
            assert!(
                [
                    (first.operation, first.result),
                    (second.operation, second.result)
                ]
                .contains(&(142, FLYNES_QUIC_INVALID_HANDLE))
            );
            assert!(
                [
                    (first.operation, first.result),
                    (second.operation, second.result)
                ]
                .contains(&(143, FLYNES_QUIC_OK))
            );
            assert!(
                fixture
                    .get()
                    .state
                    .tables
                    .lock()
                    .unwrap()
                    .resources
                    .values()
                    .all(|value| value.parent_connection() != Some(parent))
            );
        }
    }

    #[test]
    fn a2_close_waits_for_admitted_unpolled_query() {
        let fixture = Fixture::new();
        let (client, _server) = fixture.connections();
        let (reached, resume) = gate(fixture.get(), 60, 2);
        let (cutoff_reached, cutoff_resume) = gate(fixture.get(), 61, 4);
        std::thread::scope(|scope| {
            let pointer = fixture.provider as usize;
            let caller = scope.spawn(move || unsafe {
                flynes_quic_provider_query(pointer as *mut FlynesQuicProvider, 60, client)
            });
            reached.recv_timeout(WAIT).unwrap();
            assert_eq!(
                unsafe { flynes_quic_provider_close(fixture.provider, 61, client, 0) },
                FLYNES_QUIC_ACCEPTED
            );
            cutoff_reached.recv_timeout(WAIT).unwrap();
            cutoff_resume.send(()).unwrap();
            let premature = fixture.events.recv_timeout(Duration::from_millis(250)).ok();
            resume.send(()).unwrap();
            assert_eq!(caller.join().unwrap(), FLYNES_QUIC_ACCEPTED);
            assert!(
                premature.is_none(),
                "close completed before admitted query was allowed to start: {premature:?}"
            );
        });
        let first = fixture.events.recv_timeout(WAIT).unwrap();
        let second = fixture.events.recv_timeout(WAIT).unwrap();
        assert!([first.operation, second.operation].contains(&60));
        assert!(
            [
                (first.operation, first.result),
                (second.operation, second.result)
            ]
            .contains(&(61, FLYNES_QUIC_OK))
        );
    }

    #[test]
    fn a2_close_drains_every_admitted_connection_and_stream_consumer() {
        for kind in 0..12 {
            let fixture = Fixture::new();
            let (client, _server) = fixture.connections();
            let (send, recv) = if kind == 7 || kind == 8 {
                assert_eq!(
                    unsafe { flynes_quic_provider_open_bidi(fixture.provider, 490, client) },
                    FLYNES_QUIC_ACCEPTED
                );
                let opened = fixture.event(490);
                (
                    opened.resource,
                    u64::from_be_bytes(opened.bytes.try_into().unwrap()),
                )
            } else {
                (0, 0)
            };
            let (admitted, release_admission) = gate(fixture.get(), 500, 2);
            let (cutoff, release_cutoff) = gate(fixture.get(), 501, 4);
            std::thread::scope(|scope| {
                let pointer = fixture.provider as usize;
                let caller = scope.spawn(move || unsafe {
                    let provider = pointer as *mut FlynesQuicProvider;
                    match kind {
                        0 => flynes_quic_provider_inspect_handshake(provider, 500, client),
                        1 => {
                            flynes_quic_provider_exporter(provider, 500, client, b"ctx".as_ptr(), 3)
                        }
                        2 => flynes_quic_provider_send_datagram(
                            provider,
                            500,
                            client,
                            b"x".as_ptr(),
                            1,
                        ),
                        3 => flynes_quic_provider_read_datagram(provider, 500, client),
                        4 => flynes_quic_provider_query(provider, 500, client),
                        5 => flynes_quic_provider_payload_budget(provider, 500, client),
                        6 => flynes_quic_provider_stats(provider, 500, client),
                        7 => flynes_quic_provider_write(provider, 500, send, b"x".as_ptr(), 1, 0),
                        8 => flynes_quic_provider_read(provider, 500, recv, 1),
                        9 => flynes_quic_provider_open_bidi(provider, 500, client),
                        10 => flynes_quic_provider_accept_bidi(provider, 500, client),
                        11 => flynes_quic_provider_open_uni(provider, 500, client),
                        _ => unreachable!(),
                    }
                });
                admitted.recv_timeout(WAIT).unwrap();
                assert_eq!(
                    unsafe { flynes_quic_provider_close(fixture.provider, 501, client, 0) },
                    FLYNES_QUIC_ACCEPTED,
                    "consumer kind {kind}"
                );
                cutoff.recv_timeout(WAIT).unwrap();
                release_cutoff.send(()).unwrap();
                let premature = fixture.events.recv_timeout(Duration::from_millis(25)).ok();
                release_admission.send(()).unwrap();
                assert_eq!(
                    caller.join().unwrap(),
                    FLYNES_QUIC_ACCEPTED,
                    "consumer kind {kind}"
                );
                assert!(
                    premature.is_none(),
                    "close did not drain consumer kind {kind}: {premature:?}"
                );
            });
            let first = fixture.events.recv_timeout(WAIT).unwrap();
            let second = fixture.events.recv_timeout(WAIT).unwrap();
            assert_eq!(
                [first.operation, second.operation]
                    .into_iter()
                    .collect::<std::collections::BTreeSet<_>>(),
                [500, 501].into_iter().collect(),
                "consumer kind {kind}"
            );
            assert!(
                [
                    (first.operation, first.result),
                    (second.operation, second.result)
                ]
                .contains(&(501, FLYNES_QUIC_OK)),
                "consumer kind {kind}"
            );
        }
    }

    #[test]
    fn a2_close_includes_already_removed_stream_close() {
        let fixture = Fixture::new();
        let (client, _server) = fixture.connections();
        assert_eq!(
            unsafe { flynes_quic_provider_open_bidi(fixture.provider, 70, client) },
            FLYNES_QUIC_ACCEPTED
        );
        let stream = fixture.event(70).resource;
        let (reached, resume) = gate(fixture.get(), 71, 4);
        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 71, stream, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        reached.recv_timeout(WAIT).unwrap();
        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 72, client, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        let premature = fixture.events.recv_timeout(Duration::from_millis(250)).ok();
        resume.send(()).unwrap();
        assert!(
            premature.is_none(),
            "parent close ignored removed child close: {premature:?}"
        );
        let first = fixture.events.recv_timeout(WAIT).unwrap();
        let second = fixture.events.recv_timeout(WAIT).unwrap();
        assert!(
            [
                (first.operation, first.result),
                (second.operation, second.result)
            ]
            .contains(&(71, FLYNES_QUIC_OK))
        );
        assert!(
            [
                (first.operation, first.result),
                (second.operation, second.result)
            ]
            .contains(&(72, FLYNES_QUIC_OK))
        );
    }

    #[test]
    fn a2_close_waits_for_actual_admitted_future_drop() {
        let fixture = Fixture::new();
        let (client, _server) = fixture.connections();
        let weak = {
            let tables = fixture.get().state.tables.lock().unwrap();
            let Some(Resource::Connection(value)) = tables.resources.get(&client) else {
                panic!("client connection missing");
            };
            Arc::downgrade(value)
        };
        let (started, running) = mpsc::channel();
        let (entered, dropping) = mpsc::channel();
        let (resume, permit) = mpsc::channel();
        let probe = DropProbe {
            entered,
            resume: permit,
        };
        assert_eq!(
            submit_task(
                fixture.get(),
                80,
                Admission::Connection(client),
                move |admitted, _, _| async move {
                    let _connection = admitted.connection().unwrap();
                    let _probe = probe;
                    started.send(()).unwrap();
                    std::future::pending::<()>().await;
                    (FLYNES_QUIC_OK, 0, Vec::new())
                },
                |_, output| output.take().expect("joined test output"),
            ),
            FLYNES_QUIC_ACCEPTED
        );
        running.recv_timeout(WAIT).unwrap();
        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 81, client, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(
            unsafe { flynes_quic_provider_cancel(fixture.provider, 80) },
            FLYNES_QUIC_ACCEPTED
        );
        dropping.recv_timeout(WAIT).unwrap();
        let premature = fixture.events.recv_timeout(Duration::from_millis(250)).ok();
        let alive_while_dropping = weak.upgrade().is_some();
        resume.send(()).unwrap();
        assert!(
            premature.is_none(),
            "close preceded admitted future destruction: {premature:?}"
        );
        assert!(
            alive_while_dropping,
            "admitted future did not own the connection"
        );
        let first = fixture.events.recv_timeout(WAIT).unwrap();
        let second = fixture.events.recv_timeout(WAIT).unwrap();
        assert!(
            [
                (first.operation, first.result),
                (second.operation, second.result)
            ]
            .contains(&(80, FLYNES_QUIC_CANCELLED))
        );
        assert!(
            [
                (first.operation, first.result),
                (second.operation, second.result)
            ]
            .contains(&(81, FLYNES_QUIC_OK))
        );
        assert!(weak.upgrade().is_none());
    }

    #[test]
    fn a2_cancel_close_before_cutoff_preserves_connection() {
        let fixture = Fixture::new();
        let (client, _server) = fixture.connections();
        let (reached, resume) = gate(fixture.get(), 90, 3);
        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 90, client, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        reached.recv_timeout(WAIT).unwrap();
        assert_eq!(
            unsafe { flynes_quic_provider_cancel(fixture.provider, 90) },
            FLYNES_QUIC_ACCEPTED
        );
        resume.send(()).unwrap();
        assert_eq!(fixture.event(90).result, FLYNES_QUIC_CANCELLED);
        assert!(matches!(
            fixture
                .get()
                .state
                .tables
                .lock()
                .unwrap()
                .resources
                .get(&client),
            Some(Resource::Connection(_))
        ));
        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 91, client, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(fixture.event(91).result, FLYNES_QUIC_OK);
    }

    #[test]
    fn a2_cancel_close_after_cutoff_does_not_abandon_cleanup() {
        let fixture = Fixture::new();
        let (client, _server) = fixture.connections();
        assert_eq!(
            unsafe { flynes_quic_provider_open_bidi(fixture.provider, 98, client) },
            FLYNES_QUIC_ACCEPTED
        );
        let stream = fixture.event(98).resource;
        let (child_reached, child_resume) = gate(fixture.get(), 99, 4);
        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 99, stream, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        child_reached.recv_timeout(WAIT).unwrap();
        let (reached, resume) = gate(fixture.get(), 100, 4);
        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 100, client, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        reached.recv_timeout(WAIT).unwrap();
        assert_eq!(
            unsafe { flynes_quic_provider_cancel(fixture.provider, 100) },
            FLYNES_QUIC_ACCEPTED
        );
        resume.send(()).unwrap();
        let premature = fixture.events.recv_timeout(Duration::from_millis(250)).ok();
        child_resume.send(()).unwrap();
        assert!(
            premature.is_none(),
            "post-cutoff cancel skipped the still-active child: {premature:?}"
        );
        let first = fixture.events.recv_timeout(WAIT).unwrap();
        let second = fixture.events.recv_timeout(WAIT).unwrap();
        assert!(
            [
                (first.operation, first.result),
                (second.operation, second.result)
            ]
            .contains(&(99, FLYNES_QUIC_OK))
        );
        assert!(
            [
                (first.operation, first.result),
                (second.operation, second.result)
            ]
            .contains(&(100, FLYNES_QUIC_OK))
        );
    }

    #[test]
    fn a2_committed_stream_success_survives_close_before_callback() {
        let fixture = Fixture::new();
        let (client, _server) = fixture.connections();
        let (reached, resume) = gate(fixture.get(), 110, 1);
        assert_eq!(
            unsafe { flynes_quic_provider_open_bidi(fixture.provider, 110, client) },
            FLYNES_QUIC_ACCEPTED
        );
        reached.recv_timeout(WAIT).unwrap();
        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 111, client, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        let close = fixture.event(111);
        assert_eq!(close.result, FLYNES_QUIC_OK);
        resume.send(()).unwrap();
        let opened = fixture.event(110);
        assert_eq!(opened.result, FLYNES_QUIC_OK);
        let recv = u64::from_be_bytes(opened.bytes.try_into().unwrap());
        let tables = fixture.get().state.tables.lock().unwrap();
        assert!(!tables.resources.contains_key(&opened.resource));
        assert!(!tables.resources.contains_key(&recv));
    }

    #[test]
    fn a2_failed_drain_does_not_report_close_success() {
        let fixture = Fixture::new();
        let (client, _server) = fixture.connections();
        let stranded = fixture.get().runtime.spawn(std::future::pending::<()>());
        let (sender, receiver) = watch::channel(false);
        drop(sender);
        fixture.get().state.tables.lock().unwrap().pending.insert(
            120,
            PendingOperation {
                abort: stranded.abort_handle(),
                phase: Arc::new(Mutex::new(OperationPhase::Active)),
                parent_connection: Some(client),
                drained: receiver,
            },
        );
        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 121, client, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(fixture.event(121).result, FLYNES_QUIC_FAILED);
        stranded.abort();
        fixture
            .get()
            .state
            .tables
            .lock()
            .unwrap()
            .pending
            .remove(&120);
    }

    #[test]
    fn a2_close_unblocks_read_while_it_owns_stream_mutex() {
        let fixture = Fixture::new();
        let (client, server) = fixture.connections();
        assert_eq!(
            unsafe { flynes_quic_provider_open_bidi(fixture.provider, 130, client) },
            FLYNES_QUIC_ACCEPTED
        );
        let opened = fixture.event(130);
        assert_eq!(opened.result, FLYNES_QUIC_OK);
        assert_eq!(
            unsafe { flynes_quic_provider_accept_bidi(fixture.provider, 131, server) },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(
            unsafe {
                flynes_quic_provider_write(
                    fixture.provider,
                    132,
                    opened.resource,
                    b"x".as_ptr(),
                    1,
                    0,
                )
            },
            FLYNES_QUIC_ACCEPTED
        );
        let first = fixture.events.recv_timeout(WAIT).unwrap();
        let second = fixture.events.recv_timeout(WAIT).unwrap();
        assert!(
            [
                (first.operation, first.result),
                (second.operation, second.result)
            ]
            .contains(&(132, FLYNES_QUIC_OK))
        );
        let accepted = if first.operation == 131 {
            first
        } else {
            second
        };
        assert_eq!(accepted.result, FLYNES_QUIC_OK);
        let recv = u64::from_be_bytes(accepted.bytes.try_into().unwrap());
        assert_eq!(
            unsafe { flynes_quic_provider_read(fixture.provider, 135, recv, 1) },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(fixture.event(135).bytes, b"x");
        let (reached, _permit) = gate(fixture.get(), 133, 5);
        let (drain_reached, drain_resume) = gate(fixture.get(), 133, 6);
        assert_eq!(
            unsafe { flynes_quic_provider_read(fixture.provider, 133, recv, 1) },
            FLYNES_QUIC_ACCEPTED
        );
        reached.recv_timeout(WAIT).unwrap();
        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 134, server, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        drain_reached.recv_timeout(WAIT).unwrap();
        let premature = fixture.events.recv_timeout(Duration::from_millis(250)).ok();
        drain_resume.send(()).unwrap();
        assert!(
            premature.is_none(),
            "close ignored accepted read: {premature:?}"
        );
        let first = fixture.events.recv_timeout(WAIT).unwrap();
        let second = fixture.events.recv_timeout(WAIT).unwrap();
        assert!([first.operation, second.operation].contains(&133));
        assert!(
            [
                (first.operation, first.result),
                (second.operation, second.result)
            ]
            .contains(&(134, FLYNES_QUIC_OK))
        );
    }

    #[test]
    fn a2_close_isolated_from_other_connection_and_bootstrap() {
        let fixture = Fixture::new();
        let (client_a, server_a) = fixture.connections();
        let (client_b, server_b) = fixture.another_pair(150, 151);
        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 152, client_a, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(fixture.event(152).result, FLYNES_QUIC_OK);
        assert_eq!(
            unsafe { flynes_quic_provider_close(fixture.provider, 153, server_a, 0) },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(fixture.event(153).result, FLYNES_QUIC_OK);
        assert_eq!(
            unsafe { flynes_quic_provider_query(fixture.provider, 154, client_b) },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(fixture.event(154).result, FLYNES_QUIC_OK);
        assert_eq!(
            unsafe { flynes_quic_provider_open_bidi(fixture.provider, 155, client_b) },
            FLYNES_QUIC_ACCEPTED
        );
        let opened = fixture.event(155);
        assert_eq!(opened.result, FLYNES_QUIC_OK);
        assert_eq!(
            unsafe { flynes_quic_provider_accept_bidi(fixture.provider, 156, server_b) },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(
            unsafe {
                flynes_quic_provider_write(
                    fixture.provider,
                    157,
                    opened.resource,
                    b"b".as_ptr(),
                    1,
                    0,
                )
            },
            FLYNES_QUIC_ACCEPTED
        );
        let first = fixture.events.recv_timeout(WAIT).unwrap();
        let second = fixture.events.recv_timeout(WAIT).unwrap();
        assert!(
            [
                (first.operation, first.result),
                (second.operation, second.result)
            ]
            .contains(&(157, FLYNES_QUIC_OK))
        );
        let accepted = if first.operation == 156 {
            first
        } else {
            second
        };
        assert_eq!(accepted.result, FLYNES_QUIC_OK);
        let recv = u64::from_be_bytes(accepted.bytes.try_into().unwrap());
        assert_eq!(
            unsafe { flynes_quic_provider_read(fixture.provider, 158, recv, 1) },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(fixture.event(158).bytes, b"b");
        let (_client_c, _server_c) = fixture.another_pair(159, 160);
        let material = {
            let tables = fixture.get().state.tables.lock().unwrap();
            tables
                .resources
                .iter()
                .find_map(|(handle, resource)| {
                    matches!(resource, Resource::Material(_)).then_some(*handle)
                })
                .unwrap()
        };
        assert_eq!(
            unsafe {
                flynes_quic_provider_listen(
                    fixture.provider,
                    161,
                    b"127.0.0.1:0".as_ptr(),
                    11,
                    material,
                    2000,
                )
            },
            FLYNES_QUIC_ACCEPTED
        );
        assert_eq!(fixture.event(161).result, FLYNES_QUIC_OK);
    }

    #[test]
    fn a2_repeated_connections_return_to_bootstrap_baseline() {
        let fixture = Fixture::new();
        let (first_client, first_server) = fixture.connections();
        for (operation, connection) in [(180, first_client), (181, first_server)] {
            assert_eq!(
                unsafe { flynes_quic_provider_close(fixture.provider, operation, connection, 0) },
                FLYNES_QUIC_ACCEPTED
            );
            assert_eq!(fixture.event(operation).result, FLYNES_QUIC_OK);
        }
        let baseline = fixture.get().state.tables.lock().unwrap().resources.len();
        assert_eq!(baseline, 2, "listener and material must remain independent");
        for iteration in 0..8 {
            let base = 200 + iteration * 6;
            let (client, server) = fixture.another_pair(base, base + 1);
            assert_eq!(
                unsafe { flynes_quic_provider_open_bidi(fixture.provider, base + 2, client) },
                FLYNES_QUIC_ACCEPTED
            );
            assert_eq!(fixture.event(base + 2).result, FLYNES_QUIC_OK);
            assert_eq!(
                unsafe { flynes_quic_provider_open_uni(fixture.provider, base + 3, client) },
                FLYNES_QUIC_ACCEPTED
            );
            assert_eq!(fixture.event(base + 3).result, FLYNES_QUIC_OK);
            for (operation, connection) in [(base + 4, client), (base + 5, server)] {
                assert_eq!(
                    unsafe {
                        flynes_quic_provider_close(fixture.provider, operation, connection, 0)
                    },
                    FLYNES_QUIC_ACCEPTED
                );
                assert_eq!(fixture.event(operation).result, FLYNES_QUIC_OK);
            }
            let deadline = std::time::Instant::now() + WAIT;
            loop {
                let tables = fixture.get().state.tables.lock().unwrap();
                let clean = tables.resources.len() == baseline
                    && tables.pending.values().all(|entry| {
                        entry.parent_connection != Some(client)
                            && entry.parent_connection != Some(server)
                    });
                drop(tables);
                if clean {
                    break;
                }
                assert!(
                    std::time::Instant::now() < deadline,
                    "connection {iteration} did not return to bootstrap baseline"
                );
                std::thread::yield_now();
            }
        }
    }

    #[test]
    fn cancellation_after_admission_before_worker_start_still_returns_accepted() {
        let fixture = Fixture::new();
        let (reached, resume) = gate(fixture.get(), 30, 2);
        let (entered, dropped) = mpsc::channel();
        let (drop_resume, permit) = mpsc::channel();
        drop_resume.send(()).unwrap();
        let probe = DropProbe {
            entered,
            resume: permit,
        };
        let provider = fixture.get();
        std::thread::scope(|scope| {
            let caller = scope.spawn(move || {
                submit(provider, 30, async move {
                    let _probe = probe;
                    (FLYNES_QUIC_OK, 0, Vec::new())
                })
            });
            reached.recv_timeout(WAIT).unwrap();
            assert_eq!(
                unsafe { flynes_quic_provider_cancel(fixture.provider, 30) },
                FLYNES_QUIC_ACCEPTED
            );
            dropped.recv_timeout(WAIT).unwrap();
            resume.send(()).unwrap();
            let result = caller.join().unwrap();
            assert_eq!(fixture.event(30).result, FLYNES_QUIC_CANCELLED);
            assert_eq!(
                result, FLYNES_QUIC_ACCEPTED,
                "admitted operation owns an asynchronous terminal even when its worker is cancelled before start"
            );
            assert!(fixture.events.try_recv().is_err());
        });
    }
}
