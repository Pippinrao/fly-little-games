#ifndef FLYNES_FLYNES_SESSION_H
#define FLYNES_FLYNES_SESSION_H

#include <flynes/flynes_app.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FLYNES_API
#define FLYNES_API
#endif

typedef struct fly_session_handle fly_session_t;

/*
 * Additive process ABI for the public multiplayer engine. This version axis is
 * independent from the product wire major and the repository release version.
 * V1 declarations below retain their existing layout and behavior.
 */
#define FLY_SESSION_ABI_VERSION_2 UINT32_C(2)

typedef int32_t fly_session_result_v2;
typedef struct fly_session_v2_handle fly_session_v2_t;
typedef struct fly_session_inbox_v2_handle fly_session_inbox_v2_t;
typedef struct fly_session_view_v2_handle fly_session_view_v2_t;
typedef struct fly_session_approval_token_v2_handle fly_session_approval_token_v2_t;
typedef struct fly_session_task_v2_handle fly_session_task_v2_t;
typedef struct fly_session_buffer_v2_handle fly_session_buffer_v2_t;

enum fly_session_result_code_v2
{
    FLY_SESSION_V2_OK = 0,
    FLY_SESSION_V2_ACCEPTED = 1,
    FLY_SESSION_V2_EMPTY = 2,
    FLY_SESSION_V2_DUPLICATE = 3,
    FLY_SESSION_V2_INVALID_ARGUMENT = -1,
    FLY_SESSION_V2_ABI_MISMATCH = -2,
    FLY_SESSION_V2_STALE = -3,
    FLY_SESSION_V2_INVALID_STATE = -4,
    FLY_SESSION_V2_UNSUPPORTED = -5,
    FLY_SESSION_V2_PERMISSION_DENIED = -6,
    FLY_SESSION_V2_BACKPRESSURE = -7,
    FLY_SESSION_V2_BUFFER_TOO_SMALL = -8,
    FLY_SESSION_V2_CLOSED = -9,
    FLY_SESSION_V2_CANCELLED = -10,
    FLY_SESSION_V2_TIMEOUT = -11,
    FLY_SESSION_V2_IO_FAILED = -12,
    FLY_SESSION_V2_AUTH_FAILED = -13,
    FLY_SESSION_V2_PROTOCOL_VIOLATION = -14,
    FLY_SESSION_V2_CONTRACT_VIOLATION = -15,
    FLY_SESSION_V2_BUSY = -16,
    FLY_SESSION_V2_OUT_OF_MEMORY = -17,
    FLY_SESSION_V2_UNAVAILABLE = -18
};

typedef struct fly_session_config_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t action_queue_capacity;
    uint32_t notice_queue_capacity;
    uint32_t reserved_zero[4];
} fly_session_config_v2;

#define FLY_SESSION_CONFIG_V2_SIZE \
    ((uint32_t)(offsetof(fly_session_config_v2, reserved_zero) + \
                sizeof(((fly_session_config_v2*)0)->reserved_zero)))

enum fly_session_scope_kind_v2
{
    FLY_SESSION_SCOPE_ENGINE_V2 = 1,
    FLY_SESSION_SCOPE_LINK_V2 = 2,
    FLY_SESSION_SCOPE_GAME_V2 = 3
};

typedef struct fly_session_scope_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t kind;
    uint32_t reserved_zero;
    uint8_t link_id[16];
    uint8_t branch_id[16];
} fly_session_scope_v2;

#define FLY_SESSION_SCOPE_V2_SIZE ((uint32_t)sizeof(fly_session_scope_v2))

typedef struct fly_session_op_token_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint8_t engine_instance_id[16];
    fly_session_scope_v2 scope;
    uint64_t connection_generation;
    uint64_t config_revision;
    uint64_t authority_term;
    uint64_t writer_generation;
    uint64_t timeline_epoch;
    uint64_t seat_revision;
    uint64_t mode_generation;
    uint64_t media_generation;
    uint64_t operation_id;
    uint8_t transition_id[16];
} fly_session_op_token_v2;

#define FLY_SESSION_OP_TOKEN_V2_SIZE ((uint32_t)sizeof(fly_session_op_token_v2))

typedef fly_session_result_v2 (*fly_session_operation_cancel_v2)(
    void*, const fly_session_op_token_v2*);

typedef struct fly_session_clock_sample_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t continuous_ns;
    uint8_t boot_generation[32];
    uint32_t suspend_inclusive;
    uint32_t reserved_zero;
} fly_session_clock_sample_v2;

#define FLY_SESSION_CLOCK_SAMPLE_V2_SIZE \
    ((uint32_t)sizeof(fly_session_clock_sample_v2))

typedef void (*fly_session_context_retain_v2)(void* context);
typedef void (*fly_session_context_release_v2)(void* context);

typedef fly_session_result_v2 (*fly_session_clock_read_continuous_v2)(
    void* context,
    fly_session_clock_sample_v2* out_sample);

typedef struct fly_session_clock_port_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    void* context;
    fly_session_context_retain_v2 retain;
    fly_session_context_release_v2 release;
    fly_session_clock_read_continuous_v2 read_continuous;
} fly_session_clock_port_v2;

#define FLY_SESSION_CLOCK_PORT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_clock_port_v2))

typedef fly_session_result_v2 (*fly_session_executor_post_v2)(
    void* context,
    fly_session_task_v2_t* task);
typedef fly_session_result_v2 (*fly_session_executor_arm_timer_v2)(
    void* context,
    uint64_t deadline_ns,
    const uint8_t boot_generation[32],
    uint64_t timer_id,
    fly_session_task_v2_t* task);
typedef fly_session_result_v2 (*fly_session_executor_cancel_timer_v2)(
    void* context,
    uint64_t timer_id);

typedef struct fly_session_executor_port_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    void* context;
    fly_session_context_retain_v2 retain;
    fly_session_context_release_v2 release;
    fly_session_executor_post_v2 post;
    fly_session_executor_arm_timer_v2 arm_timer;
    fly_session_executor_cancel_timer_v2 cancel_timer;
} fly_session_executor_port_v2;

#define FLY_SESSION_EXECUTOR_PORT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_executor_port_v2))

typedef fly_session_result_v2 (*fly_session_platform_state_watch_v2)(
    void* context,
    const fly_session_op_token_v2* token,
    fly_session_inbox_v2_t* inbox);
typedef fly_session_result_v2 (*fly_session_platform_state_stop_v2)(
    void* context,
    const fly_session_op_token_v2* token);

typedef struct fly_session_platform_state_port_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    void* context;
    fly_session_context_retain_v2 retain;
    fly_session_context_release_v2 release;
    fly_session_platform_state_watch_v2 watch;
    fly_session_platform_state_stop_v2 stop;
} fly_session_platform_state_port_v2;

#define FLY_SESSION_PLATFORM_STATE_PORT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_platform_state_port_v2))

typedef fly_session_result_v2 (*fly_session_camera_start_scan_v2)(
    void* context,
    const fly_session_op_token_v2* token,
    fly_session_inbox_v2_t* inbox);
typedef fly_session_result_v2 (*fly_session_camera_stop_scan_v2)(
    void* context,
    const fly_session_op_token_v2* token);

typedef struct fly_session_camera_port_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    void* context;
    fly_session_context_retain_v2 retain;
    fly_session_context_release_v2 release;
    fly_session_camera_start_scan_v2 start_scan;
    fly_session_camera_stop_scan_v2 stop_scan;
} fly_session_camera_port_v2;

#define FLY_SESSION_CAMERA_PORT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_camera_port_v2))

/*
 * Provider-owned opaque resources. Zero is always invalid. Public code may
 * copy handles but must use the owning provider's release operation; handles
 * never contain or expose private-key bytes.
 */
typedef uint64_t fly_session_resource_handle_v2;

typedef struct fly_session_bytes_v2
{
    const uint8_t* data;
    uint32_t size;
    uint32_t reserved_zero;
} fly_session_bytes_v2;

/* Borrowed writable storage for a synchronous BufferHandle read. */
typedef struct fly_session_write_bytes_v2
{
    uint8_t* data;
    uint64_t capacity;
} fly_session_write_bytes_v2;

enum fly_session_key_purpose_v2
{
    FLY_SESSION_KEY_DEVICE_IDENTITY_V2 = 1,
    FLY_SESSION_KEY_SESSION_SIGNING_V2 = 2,
    FLY_SESSION_KEY_PAIR_ECDH_V2 = 3,
    FLY_SESSION_KEY_TLS_V2 = 4
};

enum fly_session_public_key_encoding_v2
{
    FLY_SESSION_PUBLIC_KEY_X963_UNCOMPRESSED_V2 = 1,
    FLY_SESSION_PUBLIC_KEY_DER_SPKI_V2 = 2
};

typedef fly_session_result_v2 (*fly_session_key_generate_v2)(
    void*, const fly_session_op_token_v2*, uint32_t purpose,
    fly_session_bytes_v2 scope_binding, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_key_open_v2)(
    void*, const fly_session_op_token_v2*, uint32_t purpose,
    fly_session_bytes_v2 durable_key_ref, fly_session_bytes_v2 scope_binding,
    const uint8_t expected_public_hash[32], fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_key_public_key_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    uint32_t encoding, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_key_prehashed_sign_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    uint32_t purpose, fly_session_bytes_v2 domain,
    const uint8_t digest[32], fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_key_agree_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2 peer_x963, fly_session_bytes_v2 attempt_binding,
    fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_key_release_v2)(
    void*, fly_session_resource_handle_v2);
typedef fly_session_result_v2 (*fly_session_key_destroy_v2)(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2 durable_key_ref,
    uint64_t expected_revision, fly_session_inbox_v2_t*);

typedef struct fly_session_key_port_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    void* context;
    fly_session_context_retain_v2 retain;
    fly_session_context_release_v2 release;
    fly_session_key_generate_v2 generate;
    fly_session_key_open_v2 open;
    fly_session_key_public_key_v2 public_key;
    fly_session_key_prehashed_sign_v2 prehashed_sign;
    fly_session_key_agree_v2 key_agree;
    fly_session_key_release_v2 release_key;
    fly_session_key_destroy_v2 destroy;
    fly_session_operation_cancel_v2 cancel;
} fly_session_key_port_v2;

#define FLY_SESSION_KEY_PORT_V2_SIZE ((uint32_t)sizeof(fly_session_key_port_v2))

enum fly_session_crypto_algorithm_v2
{
    FLY_SESSION_CRYPTO_SHA256_V2 = 1,
    FLY_SESSION_CRYPTO_HKDF_SHA256_V2 = 2,
    FLY_SESSION_CRYPTO_AES_256_GCM_V2 = 3,
    FLY_SESSION_CRYPTO_ECDSA_P256_SHA256_PREHASHED_V2 = 4
};

typedef fly_session_result_v2 (*fly_session_crypto_random_v2)(
    void*, const fly_session_op_token_v2*, uint32_t byte_count,
    fly_session_bytes_v2 purpose, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_crypto_hkdf_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 secret,
    fly_session_bytes_v2 salt, fly_session_bytes_v2 info, uint32_t output_size,
    fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_crypto_aead_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 key,
    fly_session_bytes_v2 nonce, fly_session_bytes_v2 aad,
    fly_session_bytes_v2 input, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_crypto_verify_prehashed_v2)(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2 public_key_x963,
    fly_session_bytes_v2 domain, const uint8_t digest[32],
    fly_session_bytes_v2 canonical_low_s_signature, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_crypto_hmac_sha256_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 key,
    fly_session_bytes_v2 exact_input, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_crypto_release_secret_v2)(
    void*, fly_session_resource_handle_v2);

typedef struct fly_session_crypto_port_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    void* context;
    fly_session_context_retain_v2 retain;
    fly_session_context_release_v2 release;
    fly_session_crypto_random_v2 random;
    fly_session_crypto_hkdf_v2 hkdf;
    fly_session_crypto_aead_v2 aead_seal;
    fly_session_crypto_aead_v2 aead_open;
    fly_session_crypto_verify_prehashed_v2 verify_prehashed;
    fly_session_crypto_release_secret_v2 release_secret;
    fly_session_operation_cancel_v2 cancel;
    /* Appended in R2: required for exact protocol HMACs over non-exportable keys. */
    fly_session_crypto_hmac_sha256_v2 hmac_sha256;
} fly_session_crypto_port_v2;

#define FLY_SESSION_CRYPTO_PORT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_crypto_port_v2))

typedef fly_session_result_v2 (*fly_session_tls_material_create_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 tls_key,
    fly_session_bytes_v2 certificate_policy, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_tls_material_restore_v2)(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2 durable_ref,
    const uint8_t expected_spki_hash[32], fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_tls_material_release_v2)(
    void*, fly_session_resource_handle_v2);

typedef struct fly_session_tls_material_port_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    void* context;
    fly_session_context_retain_v2 retain;
    fly_session_context_release_v2 release;
    fly_session_tls_material_create_v2 create;
    fly_session_tls_material_restore_v2 restore;
    fly_session_tls_material_release_v2 release_material;
    fly_session_operation_cancel_v2 cancel;
} fly_session_tls_material_port_v2;

#define FLY_SESSION_TLS_MATERIAL_PORT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_tls_material_port_v2))

typedef fly_session_result_v2 (*fly_session_secure_store_read_v2)(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2 name_space,
    fly_session_bytes_v2 record_key, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_secure_store_compare_replace_v2)(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2 name_space,
    fly_session_bytes_v2 record_key, uint64_t expected_revision,
    fly_session_buffer_v2_t* immutable_secret_buffer, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_secure_store_remove_v2)(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2 name_space,
    fly_session_bytes_v2 record_key, uint64_t expected_revision,
    fly_session_inbox_v2_t*);

typedef struct fly_session_secure_store_port_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    void* context;
    fly_session_context_retain_v2 retain;
    fly_session_context_release_v2 release;
    fly_session_secure_store_read_v2 read;
    fly_session_secure_store_compare_replace_v2 compare_replace;
    fly_session_secure_store_remove_v2 remove;
    fly_session_operation_cancel_v2 cancel;
} fly_session_secure_store_port_v2;

#define FLY_SESSION_SECURE_STORE_PORT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_secure_store_port_v2))

typedef fly_session_result_v2 (*fly_session_object_store_put_immutable_v2)(
    void*, const fly_session_op_token_v2*, uint32_t object_kind,
    const uint8_t expected_content_hash[32],
    fly_session_buffer_v2_t* immutable_buffer, fly_session_inbox_v2_t*);

/*
 * Appended in R3: exact-byte read-back of a durable immutable object.
 *
 * The completion must be a FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2 hash event:
 * resource = the object reference (nonzero), buffer = the exact stored bytes,
 * hash = the object's content hash. That is the payload kind the caller already
 * expects for this object, and unlike the non-terminal Buffer form it can
 * terminate the asynchronous operation.
 *
 * The two failure modes are distinct and must never be collapsed:
 *   - the object does not exist            -> FLY_SESSION_V2_UNAVAILABLE
 *   - it exists but its content hash or its exact length differs from
 *     expected_content_hash / the stored length -> a terminal failure, never
 *     FLY_SESSION_V2_UNAVAILABLE (the caller reports AUTH_FAILED for that case)
 * A provider must never answer UNAVAILABLE for an object it did read, and must
 * never answer OK with bytes that do not hash to expected_content_hash.
 * The bytes a successful read returns are the exact stored object, so a caller
 * that only had a hash can now verify the durable bytes instead of assuming a
 * previous write succeeded.
 */
typedef fly_session_result_v2 (*fly_session_object_store_read_v2)(
    void*, const fly_session_op_token_v2*, uint32_t object_kind,
    const uint8_t expected_content_hash[32], fly_session_inbox_v2_t*);

typedef struct fly_session_object_store_port_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    void* context;
    fly_session_context_retain_v2 retain;
    fly_session_context_release_v2 release;
    fly_session_object_store_put_immutable_v2 put_immutable;
    fly_session_operation_cancel_v2 cancel;
    /* Appended in R3: required by the LINK_HELLO durable-binding gate, which
     * reads the exact 0x0212 local binding back and checks its hash instead of
     * trusting the earlier put. */
    fly_session_object_store_read_v2 read;
} fly_session_object_store_port_v2;

/*
 * The frozen R2 prefix: every field up to and including cancel. A provider built
 * against the R2 table is still a valid prefix; it simply has no read primitive,
 * and only the operations that need one fail closed.
 */
#define FLY_SESSION_OBJECT_STORE_PORT_V2_R2_SIZE \
    ((uint32_t)(offsetof(fly_session_object_store_port_v2, read)))

#define FLY_SESSION_OBJECT_STORE_PORT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_object_store_port_v2))

enum fly_session_quic_handshake_policy_v2
{
    FLY_SESSION_QUIC_REQUIRE_FULL_TLS13_V2 = 1,
    FLY_SESSION_QUIC_FORBID_RESUMPTION_V2 = 1,
    FLY_SESSION_QUIC_FORBID_ZERO_RTT_V2 = 1
};

typedef struct fly_session_quic_connect_policy_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t require_full_tls13;
    uint32_t forbid_resumption;
    uint32_t forbid_zero_rtt;
    uint32_t reserved_zero;
    char alpn[32];
    uint8_t expected_der_spki_hash[32];
} fly_session_quic_connect_policy_v2;

#define FLY_SESSION_QUIC_CONNECT_POLICY_V2_SIZE \
    ((uint32_t)sizeof(fly_session_quic_connect_policy_v2))

typedef fly_session_result_v2 (*fly_session_quic_start_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 path,
    fly_session_bytes_v2 endpoint, fly_session_resource_handle_v2 tls_material,
    const fly_session_quic_connect_policy_v2*, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_quic_inspect_handshake_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 connection,
    fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_quic_exporter_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 connection,
    fly_session_bytes_v2 label, fly_session_bytes_v2 context, uint32_t output_size,
    fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_quic_open_stream_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 connection,
    uint32_t opener_role, uint32_t stream_kind, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_quic_write_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 stream,
    fly_session_buffer_v2_t* immutable_buffer, uint32_t finish,
    fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_quic_stream_control_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 stream,
    uint64_t value, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_quic_send_datagram_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 connection,
    fly_session_resource_handle_v2 immutable_buffer, uint64_t deadline_ns,
    fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_quic_connection_query_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 connection,
    fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_quic_close_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 connection,
    uint32_t reason, fly_session_inbox_v2_t*);

typedef struct fly_session_quic_port_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    void* context;
    fly_session_context_retain_v2 retain;
    fly_session_context_release_v2 release;
    fly_session_quic_start_v2 listen;
    fly_session_quic_start_v2 connect;
    fly_session_quic_inspect_handshake_v2 inspect_handshake;
    fly_session_quic_exporter_v2 exporter;
    fly_session_quic_open_stream_v2 open_uni;
    fly_session_quic_open_stream_v2 open_bidi;
    fly_session_quic_open_stream_v2 accept_uni;
    fly_session_quic_open_stream_v2 accept_bidi;
    fly_session_quic_write_v2 write;
    fly_session_quic_stream_control_v2 finish;
    fly_session_quic_stream_control_v2 reset;
    fly_session_quic_stream_control_v2 grant_read_credit;
    fly_session_quic_send_datagram_v2 send_datagram;
    fly_session_quic_connection_query_v2 payload_budget;
    fly_session_quic_connection_query_v2 stats;
    fly_session_quic_close_v2 close;
    fly_session_operation_cancel_v2 cancel;
} fly_session_quic_port_v2;

#define FLY_SESSION_QUIC_PORT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_quic_port_v2))

typedef fly_session_result_v2 (*fly_session_discovery_start_v2)(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2 policy,
    uint64_t deadline_ns, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_discovery_stop_v2)(
    void*, const fly_session_op_token_v2*);
typedef fly_session_result_v2 (*fly_session_discovery_connection_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 candidate,
    uint64_t generation, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_discovery_write_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 connection,
    uint32_t characteristic, fly_session_buffer_v2_t*, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_discovery_subscribe_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 connection,
    uint32_t characteristic, fly_session_inbox_v2_t*);

enum fly_session_discovery_physical_role_v2
{
    FLY_SESSION_DISCOVERY_PHYSICAL_CENTRAL_V2 = 1,
    FLY_SESSION_DISCOVERY_PHYSICAL_PERIPHERAL_V2 = 2
};

enum fly_session_discovery_characteristic_v2
{
    FLY_SESSION_DISCOVERY_WRITE_V2 = 1,
    FLY_SESSION_DISCOVERY_INDICATE_V2 = 2
};

typedef struct fly_session_discovery_port_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    void* context;
    fly_session_context_retain_v2 retain;
    fly_session_context_release_v2 release;
    fly_session_discovery_start_v2 scan;
    fly_session_discovery_start_v2 advertise;
    fly_session_discovery_stop_v2 stop;
    fly_session_discovery_connection_v2 connect;
    fly_session_discovery_connection_v2 disconnect;
    fly_session_discovery_write_v2 write;
    fly_session_discovery_write_v2 indicate;
    fly_session_discovery_subscribe_v2 subscribe;
} fly_session_discovery_port_v2;

#define FLY_SESSION_DISCOVERY_PORT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_discovery_port_v2))

typedef fly_session_result_v2 (*fly_session_bearer_probe_v2)(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2 policy,
    uint64_t deadline_ns, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_bearer_start_v2)(
    void*, const fly_session_op_token_v2*, const uint8_t plan_hash[32],
    fly_session_resource_handle_v2 credential, uint32_t confirmation_budget,
    fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_bearer_resolve_endpoint_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 path,
    fly_session_bytes_v2 listener_token, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_bearer_release_v2)(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2 bearer,
    fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_bearer_prepare_credential_v2)(
    void*, const fly_session_op_token_v2*, const uint8_t plan_hash[32],
    fly_session_bytes_v2 selected_plan, fly_session_resource_handle_v2 bearer,
    uint32_t creator,
    fly_session_bytes_v2 canonical_join_params, fly_session_inbox_v2_t*);
typedef fly_session_result_v2 (*fly_session_bearer_release_credential_v2)(
    void*, fly_session_resource_handle_v2 credential);

typedef struct fly_session_bearer_port_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    void* context;
    fly_session_context_retain_v2 retain;
    fly_session_context_release_v2 release;
    fly_session_bearer_probe_v2 probe;
    fly_session_bearer_start_v2 create;
    fly_session_bearer_start_v2 join;
    fly_session_bearer_resolve_endpoint_v2 resolve_endpoint;
    fly_session_bearer_release_v2 release_bearer;
    fly_session_operation_cancel_v2 cancel;
    fly_session_bearer_prepare_credential_v2 prepare_credential;
    fly_session_bearer_release_credential_v2 release_credential;
} fly_session_bearer_port_v2;

#define FLY_SESSION_BEARER_PORT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_bearer_port_v2))

/*
 * ---------------------------------------------------------------------------
 * DUAL runtime provider (IF12's RuntimePort, DUAL mode).
 *
 * The engine stays the only reducer owner. It decides the content reference,
 * the frame/seat/authority key tuple, when a frame may be stepped and when a
 * frame counts as committed; this provider only executes one frame at a time
 * and reports its state digest. It mirrors the frozen internal seam
 * `flynes::session::dual::DualRuntimePort`
 * (shared/src/session/dual/dual_runtime_contract.hpp) field for field, because
 * the public session boundary is C.
 *
 * Rules the engine enforces and this table must therefore obey:
 *   - every call is serialized on the engine's simulation worker; the provider
 *     never sees UI events, sockets or other provider callbacks;
 *   - after pause, disconnect, transport terminal, authenticated-activity
 *     timeout or shutdown, `step` is never called again;
 *   - a committed frame's input history is immutable, and `state_digest`
 *     covers the committed frame only, never a prediction;
 *   - implementations must be deterministic: two isolated providers replaying
 *     the same canonical bundle from the same checkpoint must produce the same
 *     digest.
 *
 * STREAM is not selectable in this release and this table deliberately has no
 * codec, encoder, decoder, sink, media or transport entry point of any kind.
 * The engine must be able to disable DUAL by omitting the whole port.
 * ---------------------------------------------------------------------------
 */

/*
 * DUAL port count. The DUAL seam always carries a complete four-port bundle
 * (shared/src/session/dual/dual_runtime_contract.hpp: kDualPortCountV1), which
 * matches the public runtime ABI's FLY_RUNTIME_PORT_COUNT.
 */
#define FLY_SESSION_DUAL_PORT_COUNT_V2 4u

/* Raw content reference. ROM bytes never cross this seam. */
typedef struct fly_session_dual_content_ref_v2
{
    uint8_t session_id[16];
    uint8_t branch_id[16];
    uint8_t content_hash[32];
    uint64_t timeline_epoch;
} fly_session_dual_content_ref_v2;

/* One port of the canonical four-port bundle. */
typedef struct fly_session_dual_port_sample_v2
{
    uint32_t mask;
    uint32_t reserved_zero;
    uint64_t input_sequence;
} fly_session_dual_port_sample_v2;

/*
 * The canonical input unit: always a complete four-port bundle, with the exact
 * input key tuple and the prediction marker. `predicted_port_mask` marks the
 * ports whose sample is a prediction so a prediction can never be mistaken for
 * a confirmed peer sample.
 */
typedef struct fly_session_dual_input_bundle_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint8_t session_id[16];
    uint8_t branch_id[16];
    uint64_t timeline_epoch;
    uint64_t frame_index;
    uint64_t seat_revision;
    uint8_t logical_seat;
    uint8_t reserved_zero[3];
    uint32_t predicted_port_mask;
    fly_session_dual_port_sample_v2 ports[FLY_SESSION_DUAL_PORT_COUNT_V2];
} fly_session_dual_input_bundle_v2;

#define FLY_SESSION_DUAL_INPUT_BUNDLE_V2_SIZE \
    ((uint32_t)sizeof(fly_session_dual_input_bundle_v2))

/* What the provider reports about the frame it just executed. */
typedef struct fly_session_dual_frame_outcome_v2
{
    uint64_t frame_index;
    uint64_t applied_input_sequence[FLY_SESSION_DUAL_PORT_COUNT_V2];
    uint32_t honoured_port_mask;
    uint32_t reserved_zero;
} fly_session_dual_frame_outcome_v2;

#define FLY_SESSION_DUAL_FRAME_OUTCOME_V2_SIZE \
    ((uint32_t)sizeof(fly_session_dual_frame_outcome_v2))

/* Determinism gate: state, frame and PCM digests of one committed frame. */
typedef struct fly_session_dual_state_digest_v2
{
    uint8_t state[32];
    uint8_t frame[32];
    uint8_t pcm[32];
} fly_session_dual_state_digest_v2;

#define FLY_SESSION_DUAL_STATE_DIGEST_V2_SIZE \
    ((uint32_t)sizeof(fly_session_dual_state_digest_v2))

typedef fly_session_result_v2 (*fly_session_dual_load_v2)(
    void*, const fly_session_dual_content_ref_v2*);
typedef fly_session_result_v2 (*fly_session_dual_step_v2)(
    void*, const fly_session_dual_input_bundle_v2*,
    fly_session_dual_frame_outcome_v2*);
typedef fly_session_result_v2 (*fly_session_dual_export_state_v2)(
    void*, uint8_t*, size_t, size_t*, uint8_t hash_out[32]);
typedef fly_session_result_v2 (*fly_session_dual_import_state_v2)(
    void*, const uint8_t*, size_t);
typedef fly_session_result_v2 (*fly_session_dual_state_digest_fn_v2)(
    void*, uint64_t, fly_session_dual_state_digest_v2*);

typedef struct fly_session_dual_runtime_port_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    void* context;
    fly_session_context_retain_v2 retain;
    fly_session_context_release_v2 release;
    fly_session_dual_load_v2 load;
    fly_session_dual_step_v2 step;
    fly_session_dual_export_state_v2 export_state;
    fly_session_dual_import_state_v2 import_state;
    fly_session_dual_state_digest_fn_v2 state_digest;
} fly_session_dual_runtime_port_v2;

#define FLY_SESSION_DUAL_RUNTIME_PORT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_dual_runtime_port_v2))

typedef struct fly_session_ports_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    const fly_session_clock_port_v2* clock;
    const fly_session_executor_port_v2* executor;
    const fly_session_platform_state_port_v2* platform_state;
    const fly_session_camera_port_v2* camera;
    const fly_session_key_port_v2* key;
    const fly_session_crypto_port_v2* crypto;
    const fly_session_tls_material_port_v2* tls_material;
    const fly_session_secure_store_port_v2* secure_store;
    const fly_session_quic_port_v2* quic;
    const fly_session_discovery_port_v2* discovery;
    const fly_session_bearer_port_v2* bearer;
    const fly_session_object_store_port_v2* object_store;
    /*
     * DUAL runtime, appended and optional. A shorter table (an older caller)
     * leaves it absent, and DUAL actions then fail closed with
     * FLY_SESSION_V2_UNAVAILABLE instead of pretending to run.
     */
    const fly_session_dual_runtime_port_v2* dual_runtime;
} fly_session_ports_v2;

#define FLY_SESSION_PORTS_V2_R0_SIZE \
    ((uint32_t)(offsetof(fly_session_ports_v2, camera) + \
                sizeof(((fly_session_ports_v2*)0)->camera)))
/* The largest table that has no DUAL runtime slot: the pre-DUAL prefix. */
#define FLY_SESSION_PORTS_V2_R1_SIZE \
    ((uint32_t)(offsetof(fly_session_ports_v2, dual_runtime)))
#define FLY_SESSION_PORTS_V2_SIZE ((uint32_t)sizeof(fly_session_ports_v2))
#ifdef __cplusplus
static_assert(FLY_SESSION_PORTS_V2_SIZE ==
                  FLY_SESSION_PORTS_V2_R1_SIZE +
                      sizeof(const fly_session_dual_runtime_port_v2*),
              "the DUAL runtime slot is a pure tail append");
#endif

enum fly_session_engine_state_v2
{
    FLY_SESSION_ENGINE_LOADING_V2 = 1,
    FLY_SESSION_ENGINE_READY_V2 = 2,
    FLY_SESSION_ENGINE_SHUTTING_DOWN_V2 = 3,
    FLY_SESSION_ENGINE_SHUTDOWN_COMPLETE_V2 = 4
};

enum fly_session_action_kind_v2
{
    FLY_SESSION_ACTION_CANCEL_LOADING_V2 = 1,
    FLY_SESSION_ACTION_SET_NEARBY_VISIBLE_V2 = 2,
    FLY_SESSION_ACTION_START_DISCOVERY_V2 = 3,
    FLY_SESSION_ACTION_STOP_DISCOVERY_V2 = 4,
    FLY_SESSION_ACTION_CREATE_INVITE_V2 = 5,
    FLY_SESSION_ACTION_REGENERATE_INVITE_V2 = 6,
    FLY_SESSION_ACTION_CANCEL_INVITE_V2 = 7,
    FLY_SESSION_ACTION_CANCEL_JOIN_V2 = 8,
    FLY_SESSION_ACTION_JOIN_CODE_V2 = 9,
    FLY_SESSION_ACTION_BEGIN_SCAN_V2 = 10,
    FLY_SESSION_ACTION_END_SCAN_V2 = 11,
    FLY_SESSION_ACTION_SUBMIT_SCANNED_INVITE_V2 = 12,
    FLY_SESSION_ACTION_JOIN_CANDIDATE_V2 = 13,
    FLY_SESSION_ACTION_JOIN_FRIEND_V2 = 14,
    FLY_SESSION_ACTION_ACCEPT_REQUEST_V2 = 15,
    FLY_SESSION_ACTION_REJECT_REQUEST_V2 = 16,
    FLY_SESSION_ACTION_CONFIRM_SAS_V2 = 17,
    FLY_SESSION_ACTION_REJECT_SAS_V2 = 18,
    FLY_SESSION_ACTION_CANCEL_CONNECT_V2 = 19,
    FLY_SESSION_ACTION_DISCONNECT_LINK_V2 = 20,
    FLY_SESSION_ACTION_PROPOSE_GAME_V2 = 21,
    FLY_SESSION_ACTION_CHANGE_AUTHORITY_V2 = 22,
    FLY_SESSION_ACTION_CHANGE_SEATS_V2 = 23,
    FLY_SESSION_ACTION_CONFIRM_GAME_CONFIG_V2 = 24,
    FLY_SESSION_ACTION_RETURN_TO_LOBBY_V2 = 25,
    FLY_SESSION_ACTION_SET_LOCAL_MUTE_V2 = 26,
    FLY_SESSION_ACTION_OFFER_CONTENT_V2 = 27,
    FLY_SESSION_ACTION_APPROVE_SEND_V2 = 28,
    FLY_SESSION_ACTION_APPROVE_RECEIVE_V2 = 29,
    FLY_SESSION_ACTION_CANCEL_CONTENT_V2 = 30,
    FLY_SESSION_ACTION_APPROVE_IMPORT_V2 = 31,
    FLY_SESSION_ACTION_PAUSE_GAME_V2 = 32,
    FLY_SESSION_ACTION_RESUME_GAME_V2 = 33,
    FLY_SESSION_ACTION_TAKE_OVER_V2 = 34,
    FLY_SESSION_ACTION_CONTINUE_SINGLE_V2 = 35,
    FLY_SESSION_ACTION_SAVE_AND_END_V2 = 36,
    FLY_SESSION_ACTION_RETRY_FAILURE_V2 = 37,
    FLY_SESSION_ACTION_RENAME_FRIEND_V2 = 38,
    FLY_SESSION_ACTION_DELETE_FRIEND_V2 = 39,
    FLY_SESSION_ACTION_BLOCK_FRIEND_V2 = 40,
    FLY_SESSION_ACTION_RESET_LOCAL_IDENTITY_V2 = 41,
    /*
     * DUAL run control, appended. The config/seat/authority confirmations reuse
     * the kinds above (23 CHANGE_SEATS, 24 CONFIRM_GAME_CONFIG and 22
     * CHANGE_AUTHORITY); these two are the only genuinely new controls:
     * selecting the content the run will use, and starting the DUAL run itself.
     * Pause, resume, end and exit reuse 32 PAUSE_GAME, 33 RESUME_GAME, 36
     * SAVE_AND_END and 25 RETURN_TO_LOBBY.
     */
    FLY_SESSION_ACTION_SELECT_CONTENT_V2 = 42,
    FLY_SESSION_ACTION_START_DUAL_V2 = 43
};

enum fly_session_link_state_v2
{
    FLY_SESSION_LINK_UNAVAILABLE_V2 = 0,
    FLY_SESSION_LINK_IDLE_V2 = 1,
    FLY_SESSION_LINK_DISCOVERING_V2 = 2,
    FLY_SESSION_LINK_INVITING_V2 = 3,
    FLY_SESSION_LINK_JOINING_V2 = 4,
    FLY_SESSION_LINK_AUTHENTICATING_V2 = 5,
    FLY_SESSION_LINK_PROVISIONING_V2 = 6,
    FLY_SESSION_LINK_CONNECTING_V2 = 7,
    FLY_SESSION_LINK_CONNECTED_LOBBY_V2 = 8,
    FLY_SESSION_LINK_FAILED_V2 = 9
};

enum fly_session_game_state_v2
{
    FLY_SESSION_GAME_NOT_STARTED_V2 = 0,
    FLY_SESSION_GAME_CONFIGURING_V2 = 1,
    FLY_SESSION_GAME_SYNCING_V2 = 2,
    FLY_SESSION_GAME_RUNNING_V2 = 3,
    FLY_SESSION_GAME_FROZEN_V2 = 4,
    FLY_SESSION_GAME_ENDING_V2 = 5
};

enum fly_session_friend_identity_state_v2
{
    FLY_SESSION_FRIEND_ACTIVE_V2 = 1,
    FLY_SESSION_FRIEND_NEEDS_REVERIFY_V2 = 2,
    FLY_SESSION_FRIEND_BLOCKED_V2 = 3
};

typedef struct fly_session_candidate_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    fly_session_resource_handle_v2 candidate;
    uint64_t discovery_generation;
    uint64_t last_observed_continuous_ns;
    int32_t rssi_bucket;
    uint32_t reserved_zero;
    uint64_t available_action_mask;
    char reason_key[64];
} fly_session_candidate_v2;

#define FLY_SESSION_CANDIDATE_V2_SIZE ((uint32_t)sizeof(fly_session_candidate_v2))

typedef struct fly_session_friend_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint8_t contact_id[16];
    uint64_t record_revision;
    uint32_t identity_state;
    uint32_t display_name_size;
    uint64_t available_action_mask;
    uint8_t display_name[128];
} fly_session_friend_v2;

#define FLY_SESSION_FRIEND_V2_SIZE ((uint32_t)sizeof(fly_session_friend_v2))

typedef struct fly_session_game_choice_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint8_t content_id[32];
    uint8_t source_choice_ref[16];
    uint64_t catalog_revision;
    uint64_t progress_revision;
    uint32_t selectable;
    uint32_t display_name_size;
    uint8_t display_name[128];
    char reason_key[64];
} fly_session_game_choice_v2;

#define FLY_SESSION_GAME_CHOICE_V2_SIZE \
    ((uint32_t)sizeof(fly_session_game_choice_v2))

typedef struct fly_session_snapshot_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t view_revision;
    uint32_t engine_state;
    uint32_t action_count;
    uint32_t game_choice_count;
    uint32_t reserved_zero;
    fly_session_scope_v2 scope;
    char primary_reason_key[64];
    uint32_t link_state;
    uint32_t game_state;
    uint32_t candidate_count;
    uint32_t friend_count;
    /*
     * DUAL run state, appended. These are the only fields a DUAL reader needs:
     * the simulation lifecycle, the freeze reason when it stopped being legal to
     * step, the frame/verified/commit watermarks, and the run identity the two
     * engines must agree on (session, branch, content hash, seats, seat
     * revision). They are zero for a non-DUAL engine. A reader whose
     * struct_size stops before FLY_SESSION_SNAPSHOT_V2_R0_SIZE is rejected; a
     * reader that declares exactly the older prefix is served the older fields
     * and nothing is written past the size it declared.
     */
    uint32_t dual_mode;
    uint32_t dual_state;
    uint32_t dual_freeze_reason;
    uint32_t dual_local_seat;
    uint32_t dual_authority_seat;
    uint32_t dual_seats_confirmed;
    uint64_t dual_seat_revision;
    uint64_t dual_frame_index;
    uint64_t dual_verified_through;
    uint64_t dual_commit_frontier;
    uint64_t dual_prediction_depth;
    uint8_t dual_content_hash[32];
    uint8_t dual_session_id[16];
    uint8_t dual_branch_id[16];
    /*
     * The run's consistency digests, appended once more. These are the values the
     * two engines must agree on: `dual_state_digest` covers the last committed
     * frame's exported state, `dual_frame_digest` binds that frame to the
     * canonical input bundle it was stepped with, and `dual_pcm_digest` covers
     * the canonical PCM producer state of the same frame. They are published so
     * that a reader can compare two engines at the same committed frame without
     * taking either engine's word about the other.
     */
    uint8_t dual_state_digest[32];
    uint8_t dual_frame_digest[32];
    uint8_t dual_pcm_digest[32];
} fly_session_snapshot_v2;

/*
 * The pre-DUAL prefix. offsetof(dual_mode) is the size the structure had before
 * the DUAL block was appended, so an older caller that declares exactly this
 * much is still a legal reader.
 */
#define FLY_SESSION_SNAPSHOT_V2_R0_SIZE \
    ((uint32_t)(offsetof(fly_session_snapshot_v2, dual_mode)))
/*
 * The second prefix: the snapshot as it stood once the DUAL run state existed and
 * before the digest block was appended. R0 stays the required size, so every
 * older reader is still served and the copy stays bounded by what it declared.
 */
#define FLY_SESSION_SNAPSHOT_V2_R1_SIZE \
    ((uint32_t)(offsetof(fly_session_snapshot_v2, dual_state_digest)))
#define FLY_SESSION_SNAPSHOT_V2_SIZE ((uint32_t)sizeof(fly_session_snapshot_v2))
#ifdef __cplusplus
static_assert(FLY_SESSION_SNAPSHOT_V2_SIZE ==
                  FLY_SESSION_SNAPSHOT_V2_R1_SIZE + 96u,
              "the DUAL digest block is a pure tail append");
static_assert(FLY_SESSION_SNAPSHOT_V2_R0_SIZE <=
                  FLY_SESSION_SNAPSHOT_V2_R1_SIZE,
              "the pre-DUAL prefix is not larger than the pre-digest prefix");
#endif

/*
 * DUAL simulation lifecycle. Mirrors the internal DUAL seam's
 * DualSimStateV1 (shared/src/session/dual/dual_run_scheduler.hpp) so that a
 * platform reads one vocabulary, not two.
 */
enum fly_session_dual_state_v2
{
    FLY_SESSION_DUAL_UNLOADED_V2 = 0,
    FLY_SESSION_DUAL_READY_V2 = 1,
    FLY_SESSION_DUAL_RUNNING_V2 = 2,
    FLY_SESSION_DUAL_FROZEN_V2 = 3
};

/*
 * Why a DUAL run stopped being steppable. Mirrors DualFreezeReasonV1. This
 * release has no STREAM fallback, so a freeze is explicit and never a silent
 * mode switch.
 */
enum fly_session_dual_freeze_reason_v2
{
    FLY_SESSION_DUAL_FREEZE_NONE_V2 = 0,
    FLY_SESSION_DUAL_FREEZE_INPUT_WINDOW_EXCEEDED_V2 = 1,
    FLY_SESSION_DUAL_FREEZE_PREDICTION_DEPTH_EXCEEDED_V2 = 2,
    FLY_SESSION_DUAL_FREEZE_COMMITTED_HISTORY_REWRITE_V2 = 3,
    FLY_SESSION_DUAL_FREEZE_DIGEST_MISMATCH_V2 = 4,
    FLY_SESSION_DUAL_FREEZE_AUTHENTICATED_ACTIVITY_TIMEOUT_V2 = 5,
    FLY_SESSION_DUAL_FREEZE_TRANSPORT_TERMINAL_V2 = 6,
    FLY_SESSION_DUAL_FREEZE_PAUSED_V2 = 7,
    FLY_SESSION_DUAL_FREEZE_SHUTDOWN_V2 = 8,
    FLY_SESSION_DUAL_FREEZE_SEAT_OR_AUTHORITY_CHANGED_V2 = 9
};

/* The DUAL mode discriminator carried by `dual_mode`. */
enum fly_session_dual_mode_v2
{
    FLY_SESSION_DUAL_MODE_NONE_V2 = 0,
    FLY_SESSION_DUAL_MODE_DUAL_V2 = 1
};

enum fly_session_pairing_stage_v2
{
    FLY_SESSION_PAIRING_AWAITING_LOCAL_SAS_V2 = 1,
    FLY_SESSION_PAIRING_AWAITING_PEER_CONFIRM_V2 = 2,
    FLY_SESSION_PAIRING_CONFIRMED_V2 = 3
};

typedef struct fly_session_pairing_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t connection_generation;
    uint32_t entry_mode;
    uint32_t local_role;
    uint32_t stage;
    uint32_t local_confirmed;
    uint32_t peer_confirmed;
    uint8_t sas[6];
    uint8_t reserved_zero[2];
    uint8_t pair_transcript_hash[32];
} fly_session_pairing_v2;

#define FLY_SESSION_PAIRING_V2_SIZE \
    ((uint32_t)sizeof(fly_session_pairing_v2))

typedef struct fly_session_action_descriptor_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t descriptor_id;
    uint32_t action_kind;
    uint32_t enabled;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    char text_key[64];
    char reason_key[64];
    fly_session_approval_token_v2_t* approval_token;
} fly_session_action_descriptor_v2;

#define FLY_SESSION_ACTION_DESCRIPTOR_V2_SIZE \
    ((uint32_t)sizeof(fly_session_action_descriptor_v2))

typedef struct fly_session_action_choice_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t choice_kind;
    uint32_t reserved_zero;
    uint64_t value;
    uint8_t choice_id[16];
} fly_session_action_choice_v2;

enum fly_session_action_choice_kind_v2
{
    FLY_SESSION_CHOICE_NONE_V2 = 0,
    FLY_SESSION_CHOICE_BOOLEAN_V2 = 1,
    FLY_SESSION_CHOICE_INVITE_CODE_V2 = 2,
    FLY_SESSION_CHOICE_REFERENCE_V2 = 3
};

#define FLY_SESSION_ACTION_CHOICE_V2_SIZE \
    ((uint32_t)sizeof(fly_session_action_choice_v2))

typedef struct fly_session_action_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t request_id;
    uint64_t expected_view_revision;
    fly_session_approval_token_v2_t* approval_token;
    uint32_t choice_size;
    uint32_t reserved_zero;
    fly_session_action_choice_v2 choice;
} fly_session_action_v2;

#define FLY_SESSION_ACTION_V2_SIZE ((uint32_t)sizeof(fly_session_action_v2))

/*
 * DUAL port count is defined with the DUAL runtime block above
 * (FLY_SESSION_DUAL_PORT_COUNT_V2).
 */
typedef struct fly_session_input_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    fly_session_scope_v2 scope;
    uint64_t expected_seat_revision;
    uint64_t local_device_input_id;
    uint32_t buttons;
    uint32_t reserved_zero;
    fly_session_clock_sample_v2 capture_clock;
    /*
     * DUAL complete four-port mask, appended. The DUAL input unit is always a
     * full four-port bundle, never one mask and never an implicit port 0, and
     * each entry is normalized (UP+DOWN and LEFT+RIGHT cleared together) before
     * it is ever built into a canonical bundle. This field is authoritative
     * only when the caller's struct_size reaches FLY_SESSION_INPUT_V2_SIZE; an
     * older, shorter declaration is still accepted and then carries the
     * single-player `buttons` mask, so the appended field can never be read out
     * of a buffer the caller did not fill.
     */
    uint32_t port_mask[FLY_SESSION_DUAL_PORT_COUNT_V2];
} fly_session_input_v2;

/* The pre-DUAL prefix: the largest honest size that has no port_mask. */
#define FLY_SESSION_INPUT_V2_R0_SIZE \
    ((uint32_t)(offsetof(fly_session_input_v2, port_mask)))
#define FLY_SESSION_INPUT_V2_SIZE ((uint32_t)sizeof(fly_session_input_v2))
#ifdef __cplusplus
static_assert(FLY_SESSION_INPUT_V2_SIZE ==
                  FLY_SESSION_INPUT_V2_R0_SIZE +
                      FLY_SESSION_DUAL_PORT_COUNT_V2 * 4u,
              "the DUAL port mask is a pure tail append");
#endif

enum fly_session_notice_kind_v2
{
    FLY_SESSION_NOTICE_ACTION_RESULT_V2 = 1,
    FLY_SESSION_NOTICE_SHUTDOWN_COMPLETE_V2 = 2
};

enum fly_session_action_outcome_v2
{
    FLY_SESSION_ACTION_APPLIED_V2 = 1,
    FLY_SESSION_ACTION_REJECTED_V2 = 2
};

typedef struct fly_session_notice_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t notice_sequence;
    uint64_t request_id;
    uint32_t kind;
    uint32_t outcome;
    fly_session_result_v2 result;
    uint32_t reserved_zero;
    uint64_t view_revision;
    char reason_key[64];
} fly_session_notice_v2;

#define FLY_SESSION_NOTICE_V2_SIZE ((uint32_t)sizeof(fly_session_notice_v2))

enum fly_session_port_event_kind_v2
{
    FLY_SESSION_PORT_EVENT_PLATFORM_STATE_V2 = 1,
    FLY_SESSION_PORT_EVENT_TIMER_V2 = 2,
    FLY_SESSION_PORT_EVENT_OPERATION_V2 = 3
};

enum fly_session_platform_state_payload_kind_v2
{
    FLY_SESSION_PLATFORM_STATE_SNAPSHOT_V2 = 1
};

typedef struct fly_session_platform_state_event_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t state_revision;
    uint32_t foreground;
    uint32_t network_ready;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
} fly_session_platform_state_event_v2;

#define FLY_SESSION_PLATFORM_STATE_EVENT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_platform_state_event_v2))

enum fly_session_authenticated_operation_payload_kind_v2
{
    FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2 = 101,
    FLY_SESSION_PAIR_KEY_CONFIRM_VERIFIED_V2 = 102,
    FLY_SESSION_PROVIDER_DISCOVERY_CANDIDATE_V2 = 201,
    FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2 = 202,
    FLY_SESSION_PROVIDER_DISCOVERY_MTU_V2 = 203,
    FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2 = 204,
    FLY_SESSION_PROVIDER_DISCOVERY_END_V2 = 205,
    FLY_SESSION_PROVIDER_BEARER_CAPABILITIES_V2 = 220,
    FLY_SESSION_PROVIDER_BEARER_PATH_V2 = 221,
    FLY_SESSION_PROVIDER_BEARER_ENDPOINT_V2 = 222,
    FLY_SESSION_PROVIDER_BEARER_END_V2 = 223,
    FLY_SESSION_PROVIDER_BEARER_CREDENTIAL_V2 = 224,
    FLY_SESSION_PROVIDER_KEY_HANDLE_V2 = 240,
    FLY_SESSION_PROVIDER_KEY_PUBLIC_V2 = 241,
    FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2 = 242,
    FLY_SESSION_PROVIDER_KEY_AGREEMENT_V2 = 243,
    FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2 = 244,
    FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2 = 245,
    FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2 = 246,
    FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2 = 247,
    FLY_SESSION_PROVIDER_CRYPTO_MAC_V2 = 248,
    FLY_SESSION_PROVIDER_TLS_MATERIAL_V2 = 250,
    FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2 = 260,
    FLY_SESSION_PROVIDER_SECURE_STORE_RECORD_V2 = 261,
    FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2 = 270,
    FLY_SESSION_PROVIDER_QUIC_CONNECTION_V2 = 280,
    FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2 = 281,
    FLY_SESSION_PROVIDER_QUIC_EXPORTER_V2 = 282,
    FLY_SESSION_PROVIDER_QUIC_STREAM_V2 = 283,
    FLY_SESSION_PROVIDER_QUIC_DATA_V2 = 284,
    FLY_SESSION_PROVIDER_QUIC_PAYLOAD_BUDGET_V2 = 285,
    FLY_SESSION_PROVIDER_QUIC_STATS_V2 = 286,
    FLY_SESSION_PROVIDER_QUIC_END_V2 = 287
};

typedef struct fly_session_provider_resource_event_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    fly_session_resource_handle_v2 resource;
    uint64_t generation;
    uint64_t value0;
    uint64_t value1;
} fly_session_provider_resource_event_v2;

#define FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_provider_resource_event_v2))

typedef struct fly_session_provider_buffer_event_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    fly_session_buffer_v2_t* buffer;
    uint64_t generation;
    uint64_t logical_size;
    uint64_t flags;
} fly_session_provider_buffer_event_v2;

#define FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_provider_buffer_event_v2))

typedef struct fly_session_provider_store_event_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    fly_session_buffer_v2_t* buffer;
    uint64_t revision;
    uint64_t logical_size;
    uint64_t flags;
} fly_session_provider_store_event_v2;

#define FLY_SESSION_PROVIDER_STORE_EVENT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_provider_store_event_v2))

typedef struct fly_session_provider_hash_event_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    fly_session_resource_handle_v2 resource;
    fly_session_buffer_v2_t* buffer;
    uint8_t hash[32];
} fly_session_provider_hash_event_v2;

#define FLY_SESSION_PROVIDER_HASH_EVENT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_provider_hash_event_v2))

typedef struct fly_session_provider_metrics_event_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
    uint64_t values[6];
} fly_session_provider_metrics_event_v2;

#define FLY_SESSION_PROVIDER_METRICS_EVENT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_provider_metrics_event_v2))

typedef struct fly_session_provider_end_event_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t reserved_zero;
    uint32_t reserved_zero2;
} fly_session_provider_end_event_v2;

#define FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_provider_end_event_v2))

enum fly_session_pair_approval_kind_v2
{
    FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2 = 1,
    FLY_SESSION_APPROVAL_QR_SCANNER_JOIN_V2 = 2,
    FLY_SESSION_APPROVAL_QR_INVITER_ACCEPT_V2 = 3,
    FLY_SESSION_APPROVAL_KNOWN_FRIEND_INVITE_V2 = 4,
    FLY_SESSION_APPROVAL_KNOWN_FRIEND_ACCEPT_V2 = 5
};

typedef struct fly_session_port_event_v2
{
    uint32_t struct_size;
    uint32_t abi_version;
    fly_session_op_token_v2 token;
    uint64_t event_sequence;
    uint32_t event_kind;
    uint32_t terminal;
    fly_session_result_v2 result;
    uint32_t payload_kind;
    uint32_t payload_size;
    uint32_t reserved_zero;
    uint8_t payload[64];
} fly_session_port_event_v2;

#define FLY_SESSION_PORT_EVENT_V2_SIZE \
    ((uint32_t)sizeof(fly_session_port_event_v2))

FLYNES_API fly_session_result_v2 fly_session_create_v2(
    const fly_session_config_v2* config,
    const fly_session_ports_v2* ports,
    fly_session_v2_t** out_engine);
FLYNES_API fly_session_result_v2 fly_session_submit_action_v2(
    fly_session_v2_t* engine,
    const fly_session_action_v2* action);
FLYNES_API fly_session_result_v2 fly_session_submit_input_v2(
    fly_session_v2_t* engine,
    const fly_session_input_v2* input);
FLYNES_API fly_session_result_v2 fly_session_read_notice_v2(
    fly_session_v2_t* engine,
    fly_session_notice_v2* out_notice);
FLYNES_API fly_session_result_v2 fly_session_deliver_v2(
    fly_session_inbox_v2_t* inbox,
    const fly_session_port_event_v2* event);
FLYNES_API fly_session_result_v2 fly_session_buffer_size_v2(
    const fly_session_buffer_v2_t* buffer,
    uint64_t* out_size);
FLYNES_API fly_session_result_v2 fly_session_buffer_create_copy_v2(
    fly_session_bytes_v2 source,
    fly_session_buffer_v2_t** out_buffer);
FLYNES_API fly_session_result_v2 fly_session_buffer_read_v2(
    const fly_session_buffer_v2_t* buffer,
    uint64_t offset,
    fly_session_write_bytes_v2 destination,
    uint64_t* out_written);
FLYNES_API void fly_session_buffer_retain_v2(fly_session_buffer_v2_t* buffer);
FLYNES_API void fly_session_buffer_release_v2(fly_session_buffer_v2_t* buffer);
FLYNES_API void fly_session_inbox_retain_v2(fly_session_inbox_v2_t* inbox);
FLYNES_API void fly_session_inbox_release_v2(fly_session_inbox_v2_t* inbox);
FLYNES_API fly_session_result_v2 fly_session_acquire_view_v2(
    fly_session_v2_t* engine,
    fly_session_view_v2_t** out_view);
FLYNES_API fly_session_result_v2 fly_session_view_read_v2(
    const fly_session_view_v2_t* view,
    fly_session_snapshot_v2* out_snapshot);
FLYNES_API fly_session_result_v2 fly_session_view_read_pairing_v2(
    const fly_session_view_v2_t* view,
    fly_session_pairing_v2* out_pairing);
FLYNES_API fly_session_result_v2 fly_session_view_copy_actions_v2(
    const fly_session_view_v2_t* view,
    uint32_t offset,
    fly_session_action_descriptor_v2* out_actions,
    uint32_t capacity,
    uint32_t* written);
FLYNES_API fly_session_result_v2 fly_session_view_copy_candidates_v2(
    const fly_session_view_v2_t* view,
    uint32_t offset,
    fly_session_candidate_v2* out_candidates,
    uint32_t capacity,
    uint32_t* written);
FLYNES_API fly_session_result_v2 fly_session_view_copy_friends_v2(
    const fly_session_view_v2_t* view,
    uint32_t offset,
    fly_session_friend_v2* out_friends,
    uint32_t capacity,
    uint32_t* written);
FLYNES_API fly_session_result_v2 fly_session_view_copy_game_choices_v2(
    const fly_session_view_v2_t* view,
    uint32_t offset,
    fly_session_game_choice_v2* out_choices,
    uint32_t capacity,
    uint32_t* written);
FLYNES_API void fly_session_approval_token_retain_v2(
    fly_session_approval_token_v2_t* token);
FLYNES_API void fly_session_approval_token_release_v2(
    fly_session_approval_token_v2_t* token);
FLYNES_API void fly_session_view_release_v2(fly_session_view_v2_t* view);
FLYNES_API fly_session_result_v2 fly_session_begin_shutdown_v2(
    fly_session_v2_t* engine,
    uint64_t request_id);
FLYNES_API fly_session_result_v2 fly_session_destroy_v2(fly_session_v2_t* engine);
FLYNES_API void fly_session_task_run_v2(fly_session_task_v2_t* task);
FLYNES_API void fly_session_task_release_v2(fly_session_task_v2_t* task);

#define FLY_SESSION_CONFIG_VERSION_1 UINT32_C(1)
#define FLY_FRAME_CURSOR_VERSION_1 UINT32_C(1)
#define FLY_EVIDENCE_CURSOR_VERSION_1 UINT32_C(1)
#define FLY_SESSION_EVENT_VERSION_1 UINT32_C(1)
#define FLY_SESSION_COMMAND_VERSION_1 UINT32_C(1)
#define FLY_SESSION_COMMAND_RESULT_VERSION_1 UINT32_C(1)
#define FLY_SESSION_SNAPSHOT_VERSION_1 UINT32_C(1)

enum fly_session_quic_channel
{
    FLY_SESSION_QUIC_CONTROL = 1,
    FLY_SESSION_QUIC_INPUT = 2,
    FLY_SESSION_QUIC_STATE_COMMIT = 3,
    FLY_SESSION_QUIC_BULK = 4,
    FLY_SESSION_QUIC_ROM = 5,
    FLY_SESSION_QUIC_VIDEO = 6,
    FLY_SESSION_QUIC_AUDIO = 7
};

enum fly_session_event_kind
{
    FLY_SESSION_EVENT_USER = 1,
    FLY_SESSION_EVENT_LIFECYCLE = 2,
    FLY_SESSION_EVENT_CAPABILITY = 3,
    FLY_SESSION_EVENT_RUNTIME = 4,
    FLY_SESSION_EVENT_MEDIA = 5,
    FLY_SESSION_EVENT_STORAGE = 6,
    FLY_SESSION_EVENT_TRANSPORT = 7
};

enum fly_session_command_kind
{
    FLY_SESSION_COMMAND_NONE = 0,
    /*
     * 2026-09-13 invite-code amendment (additive v1 values). The invite route
     * issues these through poll; the executor effect is:
     *  - INVITE_CODE_LOOKUP: transmit INVITE_CODE_LOOKUP_REQUEST_V1 (kind
     *    0x0214) for the live joiner attempt named by the command generation.
     *  - INVITE_CODE_CANCEL: stop the live lookup connection.
     *  - INVITE_REGENERATE: switch advertisement and displayed code/QR to the
     *    new invitation generation. The old generation is already dead.
     */
    FLY_SESSION_COMMAND_INVITE_CODE_LOOKUP = 1,
    FLY_SESSION_COMMAND_INVITE_CODE_CANCEL = 2,
    FLY_SESSION_COMMAND_INVITE_REGENERATE = 3
};

/* Wire-validated lookup outcomes accepted from a platform transport adapter. */
enum fly_session_invite_lookup_status_v1
{
    FLY_SESSION_INVITE_LOOKUP_MATCH_PENDING_HOST_APPROVAL = 1,
    FLY_SESSION_INVITE_LOOKUP_NO_MATCH = 2,
    FLY_SESSION_INVITE_LOOKUP_EXPIRED = 3,
    FLY_SESSION_INVITE_LOOKUP_RATE_LIMITED = 4
};

/*
 * Snapshot UI state. FLY_SESSION_UI_UNSPECIFIED (0) means this v1 ABI cannot
 * represent the current state (for example a live, failed or otherwise terminal
 * initial-plan attempt); it never means idle. The real lifecycle states are the
 * approved design's §2 table and require a versioned ABI addition
 * (struct_size/abi_version) instead of overloading 0.
 */
enum fly_session_ui_state
{
    FLY_SESSION_UI_UNSPECIFIED = 0,
    FLY_SESSION_UI_IDLE = 1
};

enum fly_evidence_cursor_kind
{
    FLY_EVIDENCE_CURSOR_NONE = 0,
    FLY_EVIDENCE_CURSOR_PRESENT = 1
};

/*
 * borrowed only for fly_session_create. reserved fields must be zero.
 * No BLE, QR, Wi-Fi, or friend types.
 */
typedef struct fly_session_config
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t reserved;
    uint32_t reserved1;
} fly_session_config;

#define FLY_SESSION_CONFIG_V1_SIZE \
    ((uint32_t)(offsetof(fly_session_config, reserved1) + sizeof(uint32_t)))

/*
 * Process C ABI for FrameCursorV1. has_frame is 0 (GENESIS) or 1 (FRAME).
 * GENESIS requires frame_index == 0. This is not a wire DTO.
 */
typedef struct fly_frame_cursor_v1
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t has_frame;
    uint32_t reserved_zero;
    uint64_t frame_index;
} fly_frame_cursor_v1;

#define FLY_FRAME_CURSOR_V1_SIZE \
    ((uint32_t)(offsetof(fly_frame_cursor_v1, frame_index) + sizeof(uint64_t)))

typedef struct fly_evidence_cursor_v1
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t kind;
    uint32_t reserved_zero;
    uint64_t timeline_epoch;
    fly_frame_cursor_v1 cursor;
    uint8_t evidence_hash[32];
} fly_evidence_cursor_v1;

#define FLY_EVIDENCE_CURSOR_V1_SIZE \
    ((uint32_t)(offsetof(fly_evidence_cursor_v1, evidence_hash) + 32u))

typedef struct fly_session_event
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t kind;
    uint32_t reserved;
} fly_session_event;

#define FLY_SESSION_EVENT_V1_SIZE \
    ((uint32_t)(offsetof(fly_session_event, reserved) + sizeof(uint32_t)))

typedef struct fly_session_command
{
    uint32_t struct_size;
    uint32_t version;
    uint64_t command_id;
    uint64_t transition_id;
    uint32_t kind;
    uint32_t reserved;
} fly_session_command;

#define FLY_SESSION_COMMAND_V1_SIZE \
    ((uint32_t)(offsetof(fly_session_command, reserved) + sizeof(uint32_t)))

typedef struct fly_session_command_result
{
    uint32_t struct_size;
    uint32_t version;
    uint64_t command_id;
    uint64_t transition_id;
    int32_t result;
    uint32_t reserved;
} fly_session_command_result;

/*
 * result.result follows fly_result_code: FLY_RESULT_OK (0) reports success, and
 * any other value is an executor failure. A failed or stale completion is
 * rejected as terminal and never authorizes an effect. transition_id must be 0;
 * a 128-bit wire transition id is not representable here and is never truncated
 * into this field.
 */

#define FLY_SESSION_COMMAND_RESULT_V1_SIZE \
    ((uint32_t)(offsetof(fly_session_command_result, reserved) + sizeof(uint32_t)))

typedef struct fly_session_snapshot
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t ui_state;
    uint32_t authority_role;
    uint32_t mode;
    uint32_t reserved;
    fly_frame_cursor_v1 committed_through;
    fly_evidence_cursor_v1 state_verified_through;
} fly_session_snapshot;

#define FLY_SESSION_SNAPSHOT_V1_SIZE \
    ((uint32_t)(offsetof(fly_session_snapshot, state_verified_through) + \
                sizeof(fly_evidence_cursor_v1)))

/*
 * 2026-09-13 invite-code amendment: additive snapshot of the invite-code
 * lookup route (one joiner attempt and one host invitation at most). All
 * zero values mean "no route state". join_phase values are the shared route
 * contract: 0 Idle, 1 LookingUp, 2 WaitingHostApproval, 3 WaitingSasConfirm,
 * 4 Authenticated, 5 Cancelled, 6 Expired, 7 Ambiguous. host_phase: 0 Idle,
 * 1 Active. reserved_zero must be zero on input and is written zero.
 */
#define FLY_SESSION_INVITE_SNAPSHOT_VERSION_1 UINT32_C(1)

typedef struct fly_session_invite_snapshot_v1
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t join_phase;
    uint32_t host_phase;
    uint64_t join_attempt_id;
    uint64_t host_generation;
    uint32_t host_attempts_left;
    uint32_t reserved_zero;
} fly_session_invite_snapshot_v1;

#define FLY_SESSION_INVITE_SNAPSHOT_V1_SIZE \
    ((uint32_t)(offsetof(fly_session_invite_snapshot_v1, reserved_zero) + \
                sizeof(uint32_t)))

FLYNES_API fly_result fly_session_create(const fly_session_config* config,
                                         fly_session_t** session_out);
FLYNES_API void fly_session_destroy(fly_session_t* session);

FLYNES_API fly_result fly_session_submit_event(fly_session_t* session,
                                               const fly_session_event* event);

FLYNES_API fly_result fly_session_receive_stream(fly_session_t* session,
                                                 uint32_t channel,
                                                 const uint8_t* bytes,
                                                 size_t size);
FLYNES_API fly_result fly_session_receive_datagram(fly_session_t* session,
                                                   uint32_t channel,
                                                   const uint8_t* bytes,
                                                   size_t size);

/*
 * Copies the immutable pending command, if any, into command_out.
 * A nonzero command_id together with kind == FLY_SESSION_COMMAND_NONE means a
 * command is pending whose kind has no v1 representation; command_id == 0 means
 * nothing is pending. Polling repeats the same id until it is completed.
 * transition_id is always 0: the wire transition id is not representable in
 * this 64-bit field and is never truncated into it.
 */
FLYNES_API fly_result fly_session_poll_command(fly_session_t* session,
                                               fly_session_command* command_out);
/*
 * Completes exactly the command_id returned by the last poll. Stale, duplicate,
 * unpolled or failed completions are rejected; the reducer's own generation
 * fence decides staleness.
 */
FLYNES_API fly_result fly_session_complete_command(
    fly_session_t* session,
    const fly_session_command_result* result);

FLYNES_API fly_result fly_session_tick(fly_session_t* session, uint64_t now_ns);

FLYNES_API fly_result fly_session_get_snapshot(fly_session_t* session,
                                               fly_session_snapshot* snapshot_out);

/*
 * 2026-09-13 invite-code amendment: additive projection of the invite-code
 * lookup route. Read-only; never authorizes an effect by itself.
 */
FLYNES_API fly_result fly_session_get_invite_snapshot(
    fly_session_t* session, fly_session_invite_snapshot_v1* snapshot_out);

/*
 * UI/adapter entry points for the invitation route. Code buffers are borrowed
 * for the call and must contain exactly six ASCII digits; no terminator is
 * read. IDs are caller-owned, nonzero, monotonically increasing generation
 * fences. Invalid/stale/out-of-order actions fail closed.
 */
FLYNES_API fly_result fly_session_invite_submit_code_v1(
    fly_session_t* session, uint64_t attempt_id, const uint8_t* code,
    size_t code_size, uint64_t now_ns);
FLYNES_API fly_result fly_session_invite_cancel_code_v1(
    fly_session_t* session, uint64_t attempt_id);
FLYNES_API fly_result fly_session_invite_host_publish_v1(
    fly_session_t* session, uint64_t generation, const uint8_t* code,
    size_t code_size, uint64_t now_ns);
FLYNES_API fly_result fly_session_invite_host_regenerate_v1(
    fly_session_t* session, uint64_t generation, const uint8_t* code,
    size_t code_size, uint64_t now_ns);
FLYNES_API fly_result fly_session_invite_host_cancel_v1(
    fly_session_t* session, uint64_t generation);

/* Adapter callbacks after authenticated protocol/wire validation. */
FLYNES_API fly_result fly_session_invite_report_lookup_response_v1(
    fly_session_t* session, uint64_t attempt_id, uint32_t status,
    uint64_t matched_generation, uint64_t now_ns);
FLYNES_API fly_result fly_session_invite_report_host_accepted_v1(
    fly_session_t* session, uint64_t attempt_id);
FLYNES_API fly_result fly_session_invite_confirm_local_sas_v1(
    fly_session_t* session, uint64_t attempt_id);
FLYNES_API fly_result fly_session_invite_report_peer_sas_confirmed_v1(
    fly_session_t* session, uint64_t attempt_id);

#ifdef __cplusplus
}
#endif

#endif
